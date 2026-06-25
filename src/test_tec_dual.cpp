/**
 * TEC 热度调试工具
 *
 * 串口命令（115200，发送需带换行）：
 *   h <0-100>   — 设置制热功率，例如 "h 30"
 *   c <0-100>   — 设置制冷功率，例如 "c 80"
 *   q           — 停止所有
 *
 * 两片 TEC 同步运行同一模式（便于双腕对比感受）。
 * 制热从低功率开始，逐步往上调，找到舒适上限。
 */

#include <Arduino.h>
#include "PeltierController.h"

static constexpr uint32_t REPORT_MS = 4000;

PeltierController peltier;

enum class Mode : uint8_t { IDLE, COOL, HEAT };
static Mode    mode    = Mode::IDLE;
static uint8_t percent = 0;

static void applyState() {
    switch (mode) {
        case Mode::COOL:
            peltier.cool(0, percent);
            peltier.cool(1, percent);
            Serial.printf(">> 制冷 %u%%  (duty ~%u/255)\n",
                          percent, (uint16_t)percent * 194 / 100);
            break;
        case Mode::HEAT:
            peltier.heat(0, percent);
            peltier.heat(1, percent);
            Serial.printf(">> 制热 %u%%  (duty ~%u/255)\n",
                          percent, (uint16_t)percent * 194 / 100);
            break;
        case Mode::IDLE:
            peltier.stopAll();
            Serial.println(">> 停止");
            break;
    }
}

static void printHelp() {
    Serial.println(F(
        "\n=== TEC 热度调试 ===\n"
        "  h <0-100>  — 制热，例如 'h 30'\n"
        "  c <0-100>  — 制冷，例如 'c 80'\n"
        "  q          — 停止\n"
        "  ?          — 显示此帮助\n"
        "\n建议制热起始值：20-30，再逐步加\n"
    ));
}

void setup() {
    PeltierController::earlyPinInit();  // TEC 引脚立即拉低，防上电毛刺

    Serial.begin(115200);
    delay(500);

    Serial.println("\n==============================");
    Serial.println("  TEC 热度调试工具");
    Serial.println("==============================");
    peltier.begin();
    printHelp();
}

static uint32_t lastReport = 0;
static String   inputBuf   = "";

void loop() {
    // 读取串口命令（逐字符拼到换行）
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            inputBuf.trim();
            if (inputBuf.length() == 0) { inputBuf = ""; continue; }

            char cmd = tolower(inputBuf[0]);
            int  val = inputBuf.substring(2).toInt();

            if (cmd == 'q') {
                mode = Mode::IDLE;
                applyState();
            } else if (cmd == 'h') {
                percent = constrain(val, 0, 100);
                mode    = Mode::HEAT;
                applyState();
            } else if (cmd == 'c') {
                percent = constrain(val, 0, 100);
                mode    = Mode::COOL;
                applyState();
            } else if (cmd == '?') {
                printHelp();
            } else {
                Serial.printf("未知命令: %s\n", inputBuf.c_str());
            }
            inputBuf = "";
        } else {
            inputBuf += c;
        }
    }

    // 定期打印当前状态
    if (mode != Mode::IDLE && millis() - lastReport >= REPORT_MS) {
        lastReport = millis();
        const char* modeStr = (mode == Mode::HEAT) ? "制热" : "制冷";
        Serial.printf("[%lus] %s %u%%\n", millis() / 1000, modeStr, percent);
    }
}
