#include <Arduino.h>
#include <Wire.h>
#include "PCA9548A.h"
#include "TM6605.h"
#include "LRAManager.h"
#include "PeltierController.h"

// ══════════════════════════════════════════════════════════════
//  Pin Assignment (ESP-WROOM-32)
// ══════════════════════════════════════════════════════════════
//
//  [I2C Bus → LRA subsystem]
//  ESP32 GPIO21 (SDA) ──┬── PCA9548A SDA
//  ESP32 GPIO22 (SCL) ──┬── PCA9548A SCL
//  PCA9548A CH2-CH7 → 6x TM6605 → 6x LRA
//
//  [PWM → Peltier/TEC subsystem]
//  ESP32 GPIO25 → DRV8833 AIN1 (TEC 0)
//  ESP32 GPIO26 → DRV8833 AIN2 (TEC 0)
//  ESP32 GPIO27 → DRV8833 BIN1 (TEC 1)
//  ESP32 GPIO14 → DRV8833 BIN2 (TEC 1)
//  ESP32 GPIO13 → DRV8833 nSLEEP
//
// ══════════════════════════════════════════════════════════════

static constexpr int PIN_SDA = 21;
static constexpr int PIN_SCL = 22;

// ── Subsystem: LRA (vibration) ───────────────────────────────

PCA9548A   mux(0x70, &Wire);
TM6605     driver(&Wire);
LRAManager lra(mux, driver);

// ── Subsystem: Peltier/TEC (thermal) ─────────────────────────

PeltierController peltier;

// ── Mode management ──────────────────────────────────────────

enum class SystemMode : uint8_t {
    LRA_ONLY,   // Only vibration
    TEC_ONLY,   // Only thermal
    ALL         // Both subsystems active
};

static SystemMode currentMode = SystemMode::ALL;
static bool lraReady  = false;
static bool tecReady  = false;

static const char* modeName(SystemMode m) {
    switch (m) {
        case SystemMode::LRA_ONLY: return "lra";
        case SystemMode::TEC_ONLY: return "tec";
        case SystemMode::ALL:      return "all";
    }
    return "?";
}

static bool lraActive() {
    return lraReady && (currentMode == SystemMode::LRA_ONLY || currentMode == SystemMode::ALL);
}

static bool tecActive() {
    return tecReady && (currentMode == SystemMode::TEC_ONLY || currentMode == SystemMode::ALL);
}

// ── Subsystem init ───────────────────────────────────────────

static uint8_t initLRA() {
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);
    mux.begin();

    Serial.println("Probing LRA motors...");
    uint8_t found = lra.begin();
    Serial.printf("  LRA ready: %u/6 motors found\n", __builtin_popcount(found));
    lraReady = true;
    return found;
}

static void initTEC() {
    Serial.println("Initializing Peltier TECs...");
    peltier.begin();
    tecReady = true;
}

// ── Serial command parser ────────────────────────────────────

static void printHelp() {
    Serial.println(F(
        "\n=== CrawlBand Control ===\n"
        "Commands:\n"
        " [Mode]\n"
        "  mode <lra|tec|all>     — switch active subsystem\n"
        "  status                 — show current mode and subsystem state\n"
        " [LRA Motors]  (requires mode: lra or all)\n"
        "  fire <motor> <effect>  — fire effect on one motor (0-5)\n"
        "  all <effect>           — fire effect on all motors\n"
        "  group <mask> <effect>  — fire effect on motor group (hex bitmask)\n"
        "  each <e0>..<e5>        — different effect per motor\n"
        "  wave <effect> <ms>     — wave pattern, forward\n"
        "  waver <effect> <ms>    — wave pattern, reverse\n"
        "  stop [motor]           — stop one motor or all\n"
        "  scan                   — re-probe all motors\n"
        "  test [effect] [ms]     — sequential test LRA 0-5 (default: effect 1, 500ms)\n"
        " [Peltier TEC]  (requires mode: tec or all)\n"
        "  cool <tec> <percent>   — cool TEC 0-1 at 0-100%%\n"
        "  heat <tec> <percent>   — heat TEC 0-1 at 0-100%%\n"
        "  tecstop [tec]          — stop one TEC or both\n"
        " [System]\n"
        "  help                   — show this help\n"
        " [Wave Loop]\n"
        "  pause                  — stop the auto wave loop\n"
        "  resume                 — restart the auto wave loop\n"
        "\n"
        "Effect IDs: 1=SharpClick 4=InstantClick 7=LightTap 10=DoubleClick\n"
        "  13=LightPulse 14=StrongAlert 17=SharpClick2 21=MediumClick\n"
        "  24=FlashStrike 47=Alert 58=ToggleClick 118=LongAlert\n"
        "  70-93=Fade/Boost  119=SoftNoise  123=Sleep\n"
    ));
}

static void printStatus() {
    Serial.printf("Mode: %s\n", modeName(currentMode));
    Serial.printf("  LRA subsystem:  %s  (hw: %s)\n",
        lraActive() ? "ACTIVE" : "inactive",
        lraReady ? "ready" : "not init");
    Serial.printf("  TEC subsystem:  %s  (hw: %s)\n",
        tecActive() ? "ACTIVE" : "inactive",
        tecReady ? "ready" : "not init");
}

// ── LRA command handlers ─────────────────────────────────────

static bool handleLRA(const String &verb, String tokens[], int n) {
    if (verb == "fire" && n >= 3) {
        uint8_t motor  = tokens[1].toInt();
        uint8_t effect = tokens[2].toInt();
        Serial.printf("Fire motor %u, effect %u\n", motor, effect);
        lra.fire(motor, effect);
    }
    else if (verb == "all" && n >= 2) {
        uint8_t effect = tokens[1].toInt();
        Serial.printf("Fire ALL, effect %u\n", effect);
        lra.fireAll(effect);
    }
    else if (verb == "group" && n >= 3) {
        uint8_t mask   = strtol(tokens[1].c_str(), NULL, 16);
        uint8_t effect = tokens[2].toInt();
        Serial.printf("Fire group 0x%02X, effect %u\n", mask, effect);
        lra.fireGroup(mask, effect);
    }
    else if (verb == "each" && n >= 7) {
        uint8_t effects[6];
        for (int i = 0; i < 6; i++) effects[i] = tokens[1 + i].toInt();
        Serial.printf("Fire each: [%u %u %u %u %u %u]\n",
            effects[0], effects[1], effects[2], effects[3], effects[4], effects[5]);
        lra.fireEach(effects);
    }
    else if ((verb == "wave" || verb == "waver") && n >= 3) {
        uint8_t  effect = tokens[1].toInt();
        uint32_t ms     = tokens[2].toInt();
        bool rev = (verb == "waver");
        Serial.printf("Wave %s, effect %u, interval %ums\n",
            rev ? "reverse" : "forward", effect, ms);
        lra.wave(effect, ms, rev);
    }
    else if (verb == "stop") {
        if (n >= 2) {
            uint8_t motor = tokens[1].toInt();
            Serial.printf("Stop motor %u\n", motor);
            lra.stopMotor(motor);
        } else {
            Serial.println("Stop all LRA motors");
            lra.stopAll();
        }
    }
    else if (verb == "scan") {
        Serial.println("Scanning motors...");
        uint8_t found = lra.begin();
        Serial.printf("Found mask: 0x%02X (%u/6)\n", found, __builtin_popcount(found));
    }
    else if (verb == "test") {
        uint8_t  effect = (n >= 2) ? tokens[1].toInt() : 1;
        uint32_t ms     = (n >= 3) ? tokens[2].toInt() : 500;
        Serial.println("=== LRA Sequential Test ===");
        Serial.printf("Effect: %u, interval: %ums\n\n", effect, ms);
        for (uint8_t i = 0; i < LRAManager::NUM_MOTORS; i++) {
            uint8_t ch = LRAManager::FIRST_CHANNEL + i;
            Serial.printf("  [%u/6] Motor %u  (PCA9548A CH%u) ... ", i + 1, i, ch);
            lra.fire(i, effect);
            Serial.println("FIRED");
            delay(ms);
            lra.stopMotor(i);
        }
        Serial.println("\n=== Test complete ===");
    }
    else {
        return false;  // not an LRA command
    }
    return true;
}

// ── TEC command handlers ─────────────────────────────────────

static bool handleTEC(const String &verb, String tokens[], int n) {
    if (verb == "cool" && n >= 3) {
        uint8_t tec     = tokens[1].toInt();
        uint8_t percent = tokens[2].toInt();
        Serial.printf("Cool TEC %u at %u%%\n", tec, percent);
        peltier.cool(tec, percent);
    }
    else if (verb == "heat" && n >= 3) {
        uint8_t tec     = tokens[1].toInt();
        uint8_t percent = tokens[2].toInt();
        Serial.printf("Heat TEC %u at %u%%\n", tec, percent);
        peltier.heat(tec, percent);
    }
    else if (verb == "tecstop") {
        if (n >= 2) {
            uint8_t tec = tokens[1].toInt();
            Serial.printf("Stop TEC %u\n", tec);
            peltier.stop(tec);
        } else {
            Serial.println("Stop all TECs");
            peltier.stopAll();
        }
    }
    else {
        return false;  // not a TEC command
    }
    return true;
}

// ── Sequential wave loop state ───────────────────────────────

static uint8_t   waveMotor    = 0;
static uint32_t  waveLastMs   = 0;
static bool      waveRunning  = true;
static constexpr uint32_t WAVE_INTERVAL_MS = 300;
static constexpr uint8_t  WAVE_EFFECT      = 14;

// ── Command dispatcher ───────────────────────────────────────

static void processCommand(const String &line) {
    String cmd = line;
    cmd.trim();
    if (cmd.length() == 0) return;

    // Tokenize
    String tokens[8];
    int n = 0;
    int start = 0;
    for (int i = 0; i <= (int)cmd.length() && n < 8; i++) {
        if (i == (int)cmd.length() || cmd[i] == ' ') {
            if (i > start) {
                tokens[n++] = cmd.substring(start, i);
            }
            start = i + 1;
        }
    }
    if (n == 0) return;

    String verb = tokens[0];
    verb.toLowerCase();

    // ── System commands (always available) ──

    if (verb == "help" || verb == "?") {
        printHelp();
        return;
    }
    if (verb == "pause") {
        waveRunning = false;
        lra.stopAll();
        Serial.println("Wave loop paused. All motors stopped.");
        return;
    }
    if (verb == "resume") {
        waveRunning = true;
        waveMotor   = 0;
        waveLastMs  = millis();
        Serial.println("Wave loop resumed.");
        return;
    }
    if (verb == "status") {
        printStatus();
        return;
    }
    if (verb == "mode") {
        if (n >= 2) {
            String m = tokens[1];
            m.toLowerCase();
            if (m == "lra") {
                currentMode = SystemMode::LRA_ONLY;
                if (tecReady) peltier.stopAll();
            } else if (m == "tec") {
                currentMode = SystemMode::TEC_ONLY;
                if (lraReady) lra.stopAll();
            } else if (m == "all") {
                currentMode = SystemMode::ALL;
            } else {
                Serial.println("Usage: mode <lra|tec|all>");
                return;
            }
            Serial.printf("Mode set to: %s\n", modeName(currentMode));
        } else {
            Serial.printf("Current mode: %s\n", modeName(currentMode));
        }
        printStatus();
        return;
    }

    // ── LRA commands ──

    if (lraActive() && handleLRA(verb, tokens, n)) return;

    // ── TEC commands ──

    if (tecActive() && handleTEC(verb, tokens, n)) return;

    // ── Inactive subsystem hints ──

    if (!lraActive() && handleLRA(verb, tokens, n) == false) {
        // Check if it's a known LRA command being rejected
    }
    // Check known LRA commands when subsystem is off
    if (!lraActive() && (verb == "fire" || verb == "all" || verb == "group" ||
                         verb == "each" || verb == "wave" || verb == "waver" ||
                         verb == "stop" || verb == "scan")) {
        Serial.printf("LRA subsystem is inactive (mode: %s). Use 'mode lra' or 'mode all'.\n",
            modeName(currentMode));
        return;
    }
    // Check known TEC commands when subsystem is off
    if (!tecActive() && (verb == "cool" || verb == "heat" || verb == "tecstop")) {
        Serial.printf("TEC subsystem is inactive (mode: %s). Use 'mode tec' or 'mode all'.\n",
            modeName(currentMode));
        return;
    }

    Serial.printf("Unknown command: %s\n", cmd.c_str());
    Serial.println("Type 'help' for usage.");
}

// ── Arduino entry points ─────────────────────────────────────

void setup() {
    // ── 最先执行：TEC 引脚全部拉低 + DRV8833 休眠 ──
    // 防止上电/复位/串口连接瞬间 GPIO 浮空导致 TEC 意外发热
    PeltierController::earlyPinInit();

    Serial.begin(115200);
    delay(500);
    Serial.println("\n==============================");
    Serial.println(" CrawlBand Controller");
    Serial.println(" LRA (6x) + Peltier TEC (2x)");
    Serial.println("==============================\n");

    // Init both subsystems independently
    initLRA();
    Serial.println();
    initTEC();
    Serial.println();

    currentMode = SystemMode::ALL;
    printStatus();
    printHelp();

}

void loop() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        processCommand(line);
    }

    if (lraActive() && waveRunning) {
        uint32_t now = millis();
        if (now - waveLastMs >= WAVE_INTERVAL_MS) {
            lra.fire(waveMotor, WAVE_EFFECT);
            waveMotor = (waveMotor + 1) % LRAManager::NUM_MOTORS;
            waveLastMs = now;
        }
    }
}
