#pragma once
#include <Arduino.h>

/// Controls 2 Peltier (TEC) modules via DRV8833 dual H-bridge driver.
///
/// DRV8833 channel mapping:
///   Channel A (AIN1/AIN2) → TEC 0
///   Channel B (BIN1/BIN2) → TEC 1
///
/// PWM control:
///   Cool: IN1 = PWM, IN2 = LOW   (current flows one direction)
///   Heat: IN1 = LOW, IN2 = PWM   (current flows reverse)
///   Stop: IN1 = LOW, IN2 = LOW
///
class PeltierController {
public:
    static constexpr uint8_t NUM_TECS = 2;

    // ── Pin assignments (ESP32 GPIOs) ──────────────────────
    static constexpr uint8_t PIN_AIN1   = 25;   // TEC 0, direction A
    static constexpr uint8_t PIN_AIN2   = 26;   // TEC 0, direction B
    static constexpr uint8_t PIN_BIN1   = 27;   // TEC 1, direction A
    static constexpr uint8_t PIN_BIN2   = 14;   // TEC 1, direction B
    static constexpr uint8_t PIN_NSLEEP = 13;   // DRV8833 enable (active-high)

    // ── PWM settings ───────────────────────────────────────
    static constexpr uint32_t PWM_FREQ       = 25000;  // 25 kHz (inaudible)
    static constexpr uint8_t  PWM_RESOLUTION = 8;      // 8-bit → 0-255

    // ── Safety limits ──────────────────────────────────────
    // TEC max voltage = 3.8V. With VM = 5V:
    //   max duty = 3.8 / 5.0 = 76% → 194/255
    // If VM = 3.3V, change MAX_DUTY to 255.
    static constexpr uint8_t MAX_DUTY = 194;           // 76% — TEC voltage limit (both directions)

    // Heating skin-safety limit: lower than MAX_DUTY to prevent burns.
    // Skin pain threshold ~43°C; at 50% of MAX_DUTY the TEC heating
    // stays within a comfortable warm range (~38-41°C on skin).
    static constexpr uint8_t MAX_HEAT_DUTY = 128;      // 50% of 255 — heating skin-safety cap
    static constexpr uint8_t MAX_HEAT_PERCENT = 65;     // UI-facing cap: 65% of slider range

    PeltierController() {}

    /// 在 setup() 最开头调用，比 Serial.begin() 更早。
    /// 立即把所有 TEC 引脚拉低 + DRV8833 休眠，防止上电/复位瞬间的浮空脉冲。
    static void earlyPinInit() {
        // nSLEEP LOW = DRV8833 休眠，即使 IN 引脚有毛刺也不会输出
        pinMode(PIN_NSLEEP, OUTPUT);
        digitalWrite(PIN_NSLEEP, LOW);
        // 所有 H-bridge 输入强制 LOW
        const uint8_t pins[] = { PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2 };
        for (auto p : pins) {
            pinMode(p, OUTPUT);
            digitalWrite(p, LOW);
        }
    }

    void begin() {
        // Store pin pairs
        _in1[0] = PIN_AIN1;  _in2[0] = PIN_AIN2;
        _in1[1] = PIN_BIN1;  _in2[1] = PIN_BIN2;

        // Step 1: 确保 DRV8833 处于休眠（earlyPinInit 已执行，这里再保险一次）
        digitalWrite(PIN_NSLEEP, LOW);

        // Step 2: 先配置 LEDC PWM 通道，duty=0，但还不 attach 到引脚
        for (uint8_t i = 0; i < NUM_TECS; i++) {
            ledcSetup(i * 2,     PWM_FREQ, PWM_RESOLUTION);
            ledcSetup(i * 2 + 1, PWM_FREQ, PWM_RESOLUTION);
            ledcWrite(i * 2,     0);
            ledcWrite(i * 2 + 1, 0);
        }

        // Step 3: Attach PWM 到引脚（可能产生瞬间毛刺，但 DRV8833 仍在休眠，不会输出）
        for (uint8_t i = 0; i < NUM_TECS; i++) {
            ledcAttachPin(_in1[i], i * 2);
            ledcAttachPin(_in2[i], i * 2 + 1);
        }

        // Step 4: 再次确认 duty=0（清除 attach 过程中可能的残留）
        for (uint8_t i = 0; i < NUM_TECS; i++) {
            ledcWrite(i * 2,     0);
            ledcWrite(i * 2 + 1, 0);
        }

        // Step 5: 短暂等待 PWM 输出稳定
        delayMicroseconds(100);

        // Step 6: 现在 IN 全为 LOW PWM，安全唤醒 DRV8833
        digitalWrite(PIN_NSLEEP, HIGH);

        Serial.println("  DRV8833 Peltier controller: OK (safe init)");
    }

    /// Cool TEC at given percentage (0-100)
    void cool(uint8_t tec, uint8_t percent) {
        if (tec >= NUM_TECS) return;
        uint8_t duty = _percentToDuty(percent);
        ledcWrite(tec * 2,     duty);
        ledcWrite(tec * 2 + 1, 0);
    }

    /// Heat TEC at given percentage (0-100), capped at MAX_HEAT_PERCENT for skin safety
    void heat(uint8_t tec, uint8_t percent) {
        if (tec >= NUM_TECS) return;
        if (percent > MAX_HEAT_PERCENT) percent = MAX_HEAT_PERCENT;
        uint8_t duty = _percentToDuty(percent);
        if (duty > MAX_HEAT_DUTY) duty = MAX_HEAT_DUTY;
        ledcWrite(tec * 2,     0);
        ledcWrite(tec * 2 + 1, duty);
    }

    /// Stop one TEC
    void stop(uint8_t tec) {
        if (tec >= NUM_TECS) return;
        ledcWrite(tec * 2,     0);
        ledcWrite(tec * 2 + 1, 0);
    }

    /// Stop all TECs
    void stopAll() {
        for (uint8_t i = 0; i < NUM_TECS; i++) stop(i);
    }

    /// Enter DRV8833 low-power sleep
    void sleep() {
        stopAll();
        digitalWrite(PIN_NSLEEP, LOW);
    }

    /// Wake DRV8833 from sleep
    void wake() {
        digitalWrite(PIN_NSLEEP, HIGH);
    }

private:
    uint8_t _in1[NUM_TECS];
    uint8_t _in2[NUM_TECS];

    uint8_t _percentToDuty(uint8_t percent) {
        if (percent >= 100) return MAX_DUTY;
        return (uint8_t)((uint16_t)percent * MAX_DUTY / 100);
    }
};
