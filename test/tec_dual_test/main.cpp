/**
 * TEC Dual-Mode Test
 *
 * TEC 0 — cool(0, 80)  →  制冷
 * TEC 1 — cool(1, 80)  →  制冷（验证两片硬件均正常）
 *
 * 上传后打开串口监视器（115200）观察输出。
 * 发送 'q' 停止两片制冷片。
 */

#include <Arduino.h>
#include "PeltierController.h"

static constexpr uint8_t  DUTY_PERCENT = 80;   // 功率百分比 0-100
static constexpr uint32_t REPORT_MS    = 3000;  // 状态打印间隔 (ms)

PeltierController peltier;

// ── setup ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=============================");
    Serial.println("  TEC Dual-Mode Test");
    Serial.println("  TEC0: cool  |  TEC1: cool");
    Serial.println("=============================\n");

    peltier.begin();

    // TEC 0 & TEC 1: 均正向制冷，验证硬件是否正常
    peltier.cool(0, DUTY_PERCENT);
    Serial.printf("[TEC 0] cool() at %u%%\n", DUTY_PERCENT);

    peltier.cool(1, DUTY_PERCENT);
    Serial.printf("[TEC 1] cool() at %u%%\n\n", DUTY_PERCENT);

    Serial.println("发送 'q' 停止所有制冷片。");
}

// ── loop ─────────────────────────────────────────────────────

static uint32_t lastReport = 0;
static bool     running    = true;

void loop() {
    // 停止命令
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'q' || c == 'Q') {
            peltier.stopAll();
            running = false;
            Serial.println("\n[STOP] 两片制冷片已停止。");
        }
    }

    // 定期打印状态
    if (running && (millis() - lastReport >= REPORT_MS)) {
        lastReport = millis();
        Serial.printf("[%6lus]  TEC0: cool %u%%  |  TEC1: cool %u%%\n",
                      millis() / 1000, DUTY_PERCENT, DUTY_PERCENT);
    }
}
