/// CrawlBand Actuator Server (WebSocket + Bluetooth SPP)
/// ESP32 acts as a "dumb" actuator controller — no experiment logic.
/// Receives JSON commands via WebSocket OR Bluetooth Serial, dispatches to LRA/TEC drivers.
/// Supports mutual-exclusion claim/release and layer-2 disconnect safety.

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <BluetoothSerial.h>
#include <ArduinoJson.h>
#include "PCA9548A.h"
#include "TM6605.h"
#include "LRAManager.h"
#include "PeltierController.h"

// Optional local Wi-Fi overrides. Copy include/secrets.example.h to
// include/secrets.h and edit it for your own network. secrets.h is ignored.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

// ── WiFi config ─────────────────────────────────────────────
#ifndef USE_AP_MODE
#define USE_AP_MODE 0
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

// ── Hardware instances ──────────────────────────────────────
static PCA9548A          mux(0x70, &Wire);
static TM6605            driver(&Wire);
static LRAManager        lra(mux, driver);
static PeltierController peltier;

// ── Communication ───────────────────────────────────────────
static WebSocketsServer ws(81);
static BluetoothSerial  SerialBT;

// Virtual client numbers for non-WS transports (WS clients are 0..N)
static const uint8_t BT_CLIENT  = 0xFE;
static const uint8_t USB_CLIENT = 0xFD;

// ── Controller mutual-exclusion state ───────────────────────
enum class ControllerRole : uint8_t { NONE, EXPERIMENT_AB, UNITY };

struct ControllerState {
    bool           active    = false;
    ControllerRole role      = ControllerRole::NONE;
    uint8_t        clientNum = 0;
};

static ControllerState controller;

// ── Safety constants ────────────────────────────────────────
static const uint32_t SAFETY_TIMEOUT_MS = 60000;  // Layer 3: 60s global watchdog (fallback)

// ── State tracking ──────────────────────────────────────────
static bool     lraReady       = false;
static uint8_t  lraMotorsMask  = 0;
static bool     tecReady       = false;
static uint32_t lastMsgMs      = 0;
static bool     btConnected    = false;
static String   btBuffer       = "";
static String   usbBuffer      = "";
static bool     usbClientActive = false;

// ── Forward declarations ────────────────────────────────────
void handleCommand(uint8_t clientNum, const char* json, size_t len);
void sendAck(uint8_t clientNum, int seq, const char* status, const char* msg = nullptr);
void sendStatus(uint8_t clientNum, int seq);
void safetyStopAll();
void releaseController(const char* reason);
void sendRaw(uint8_t clientNum, const char* buf, size_t n);

// ── Transport-agnostic send ─────────────────────────────────
void sendRaw(uint8_t clientNum, const char* buf, size_t n) {
    if (clientNum == USB_CLIENT) {
        Serial.println(buf);
    } else if (clientNum == BT_CLIENT) {
        SerialBT.println(buf);
    } else {
        ws.sendTXT(clientNum, buf, n);
    }
}

// ── WiFi setup ──────────────────────────────────────────────
static bool     wifiReady      = false;
static uint32_t wifiRetryMs    = 0;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;  // 30s between retries

void initWiFi() {
#if USE_AP_MODE
    Serial.printf("[WiFi] Starting AP: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] AP ready! IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WiFi] Connect your PC to WiFi \"%s\" (pass: %s)\n", WIFI_SSID, WIFI_PASS);
    wifiReady = true;
#else
    Serial.printf("[WiFi] Connecting to %s (non-blocking)...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // 尝试等待最多 5 秒，之后不阻塞
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 5000) {
        delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
        wifiReady = true;
        Serial.printf("\n[WiFi] Connected! IP: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("\n[WiFi] Not connected yet — will retry in background");
        Serial.println("[WiFi] USB/BT control is fully operational without WiFi");
        wifiRetryMs = millis();
    }
#endif
}

// ── Bluetooth setup ─────────────────────────────────────────
void initBluetooth() {
    SerialBT.begin("CrawlBand");
    Serial.println("[BT] Bluetooth SPP started as \"CrawlBand\"");
    Serial.println("[BT] Pair from PC → connects as COM port");
}

// ── LRA init ────────────────────────────────────────────────
void initLRA() {
    Wire.begin(21, 22);
    Wire.setClock(400000);
    Serial.println("[LRA] Probing motors...");
    lraMotorsMask = lra.begin();
    lraReady = (lraMotorsMask != 0);
    Serial.printf("[LRA] Ready: %s  Motors: 0x%02X\n",
                  lraReady ? "YES" : "NO", lraMotorsMask);
}

// ── TEC init ────────────────────────────────────────────────
void initTEC() {
    Serial.println("[TEC] Initializing...");
    peltier.begin();
    tecReady = true;
    Serial.println("[TEC] Ready: YES");
}

// ── Controller helpers ──────────────────────────────────────
static const char* roleToStr(ControllerRole r) {
    switch (r) {
        case ControllerRole::EXPERIMENT_AB: return "experiment_ab";
        case ControllerRole::UNITY:         return "unity";
        default:                            return "none";
    }
}

static ControllerRole strToRole(const char* s) {
    if (!s) return ControllerRole::NONE;
    if (strcmp(s, "experiment_ab") == 0) return ControllerRole::EXPERIMENT_AB;
    if (strcmp(s, "unity") == 0)         return ControllerRole::UNITY;
    return ControllerRole::NONE;
}

void releaseController(const char* reason) {
    if (!controller.active) return;
    Serial.printf("[CTRL] Released (was client #%u, role=%s): %s\n",
                  controller.clientNum, roleToStr(controller.role), reason);
    controller.active    = false;
    controller.role      = ControllerRole::NONE;
    controller.clientNum = 0;
}

static bool requireClaim(uint8_t clientNum, int seq) {
    if (!controller.active || controller.clientNum != clientNum) {
        sendAck(clientNum, seq, "error", "not claimed");
        return false;
    }
    return true;
}

// ── WebSocket event handler ─────────────────────────────────
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            IPAddress ip = ws.remoteIP(num);
            Serial.printf("[WS] Client #%u connected from %s\n", num, ip.toString().c_str());
            lastMsgMs = millis();
            break;
        }
        case WStype_DISCONNECTED: {
            Serial.printf("[WS] Client #%u disconnected\n", num);
            if (controller.active && controller.clientNum == num) {
                Serial.println("[SAFETY-L2] Controller disconnected — stopping all actuators");
                safetyStopAll();
                releaseController("websocket disconnected");
            }
            if (ws.connectedClients() == 0 && !btConnected) {
                Serial.println("[WS] No clients — safety stop all actuators");
                safetyStopAll();
            }
            break;
        }
        case WStype_TEXT: {
            lastMsgMs = millis();
            handleCommand(num, (const char*)payload, length);
            break;
        }
        default:
            break;
    }
}

// ── Command dispatcher ──────────────────────────────────────
void handleCommand(uint8_t clientNum, const char* json, size_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        Serial.printf("[CMD] JSON parse error: %s\n", err.c_str());
        sendAck(clientNum, -1, "error", "json parse error");
        return;
    }

    const char* cmd = doc["cmd"];
    int seq = doc["seq"] | -1;

    if (!cmd) {
        sendAck(clientNum, seq, "error", "missing cmd field");
        return;
    }

    const char* transport = (clientNum == USB_CLIENT) ? "USB" : (clientNum == BT_CLIENT) ? "BT" : "WS";
    Serial.printf("[CMD/%s] seq=%d cmd=%s\n", transport, seq, cmd);

    // ── Commands that do NOT require claim ──────────
    if (strcmp(cmd, "ping") == 0) {
        sendAck(clientNum, seq, "pong");
        return;
    }
    if (strcmp(cmd, "status") == 0) {
        sendStatus(clientNum, seq);
        return;
    }
    if (strcmp(cmd, "stopAll") == 0) {
        safetyStopAll();
        sendAck(clientNum, seq, "ok");
        return;
    }
    if (strcmp(cmd, "claim") == 0) {
        const char* roleStr = doc["role"];
        ControllerRole role = strToRole(roleStr);
        if (role == ControllerRole::NONE) {
            sendAck(clientNum, seq, "error", "invalid role (use experiment_ab or unity)");
            return;
        }
        if (controller.active) {
            if (controller.clientNum == clientNum) {
                controller.role = role;
                sendAck(clientNum, seq, "ok", "re-claimed");
                return;
            }
            // Single-user system: auto-release previous controller
            // (handles USB→WS transport switch where USB has no disconnect detection)
            Serial.printf("[CTRL] Auto-releasing client #%u for new client #%u (%s)\n",
                          controller.clientNum, clientNum, transport);
            releaseController("auto-released for new client");
        }
        controller.active    = true;
        controller.role      = role;
        controller.clientNum = clientNum;
        Serial.printf("[CTRL] Client #%u (%s) claimed as %s\n", clientNum, transport, roleToStr(role));
        sendAck(clientNum, seq, "ok");
        return;
    }
    if (strcmp(cmd, "release") == 0) {
        if (controller.active && controller.clientNum == clientNum) {
            releaseController("client released");
            sendAck(clientNum, seq, "ok");
        } else {
            sendAck(clientNum, seq, "error", "you are not the controller");
        }
        return;
    }

    // ── All remaining commands require claim ────────
    if (!requireClaim(clientNum, seq)) return;

    // ── LRA commands ────────────────────────────────
    if (strcmp(cmd, "lra.fire") == 0) {
        uint8_t motor  = doc["motor"]  | 0;
        uint8_t effect = doc["effect"] | 1;
        if (motor >= LRAManager::NUM_MOTORS) {
            sendAck(clientNum, seq, "error", "invalid motor");
            return;
        }
        lra.fire(motor, effect);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "lra.fireAll") == 0) {
        uint8_t effect = doc["effect"] | 1;
        lra.fireAll(effect);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "lra.fireGroup") == 0) {
        uint8_t mask   = doc["mask"]   | 0;
        uint8_t effect = doc["effect"] | 1;
        lra.fireGroup(mask, effect);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "lra.fireEach") == 0) {
        JsonArray arr = doc["effects"].as<JsonArray>();
        uint8_t effects[LRAManager::NUM_MOTORS] = {0};
        uint8_t i = 0;
        for (JsonVariant v : arr) {
            if (i >= LRAManager::NUM_MOTORS) break;
            effects[i++] = v.as<uint8_t>();
        }
        lra.fireEach(effects);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "lra.wave") == 0) {
        uint8_t  effect   = doc["effect"]   | 1;
        uint32_t interval = doc["interval"] | 200;
        bool     reverse  = doc["reverse"]  | false;
        lra.wave(effect, interval, reverse);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "lra.stop") == 0) {
        lra.stopAll();
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "lra.stopMotor") == 0) {
        uint8_t motor = doc["motor"] | 0;
        lra.stopMotor(motor);
        sendAck(clientNum, seq, "ok");
    }
    // ── TEC commands ────────────────────────────────
    else if (strcmp(cmd, "tec.cool") == 0) {
        uint8_t tec     = doc["tec"]     | 0;
        uint8_t percent = doc["percent"] | 0;
        if (tec >= PeltierController::NUM_TECS) {
            sendAck(clientNum, seq, "error", "invalid tec");
            return;
        }
        peltier.cool(tec, percent);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "tec.heat") == 0) {
        uint8_t tec     = doc["tec"]     | 0;
        uint8_t percent = doc["percent"] | 0;
        if (tec >= PeltierController::NUM_TECS) {
            sendAck(clientNum, seq, "error", "invalid tec");
            return;
        }
        peltier.heat(tec, percent);
        sendAck(clientNum, seq, "ok");
    }
    else if (strcmp(cmd, "tec.stop") == 0) {
        uint8_t tec = doc["tec"] | 255;
        if (tec == 255) {
            peltier.stopAll();
        } else {
            peltier.stop(tec);
        }
        sendAck(clientNum, seq, "ok");
    }
    else {
        sendAck(clientNum, seq, "error", "unknown command");
    }
}

// ── Response helpers ────────────────────────────────────────
void sendAck(uint8_t clientNum, int seq, const char* status, const char* msg) {
    JsonDocument resp;
    resp["ack"] = status;
    resp["seq"] = seq;
    if (msg) resp["msg"] = msg;

    char buf[128];
    size_t n = serializeJson(resp, buf, sizeof(buf));
    sendRaw(clientNum, buf, n);
}

void sendStatus(uint8_t clientNum, int seq) {
    JsonDocument resp;
    resp["ack"]         = "status";
    resp["seq"]         = seq;
    resp["lra_ready"]   = lraReady;
    resp["motors"]      = lraMotorsMask;
    resp["tec_ready"]   = tecReady;
    resp["rssi"]        = WiFi.RSSI();
    resp["clients"]     = ws.connectedClients();
    resp["bt_connected"]= btConnected;
    resp["uptime_s"]    = millis() / 1000;
    resp["controller"]  = roleToStr(controller.role);
    resp["ctrl_client"] = controller.active ? (int)controller.clientNum : -1;

    char buf[320];
    size_t n = serializeJson(resp, buf, sizeof(buf));
    sendRaw(clientNum, buf, n);
}

void safetyStopAll() {
    lra.stopAll();
    peltier.stopAll();
    Serial.println("[SAFETY] All actuators stopped");
}

// ── Bluetooth serial processing ─────────────────────────────
void processBluetooth() {
    // Detect BT connect/disconnect
    bool nowConnected = SerialBT.hasClient();
    if (nowConnected && !btConnected) {
        btConnected = true;
        lastMsgMs = millis();
        Serial.println("[BT] Client connected");
    } else if (!nowConnected && btConnected) {
        btConnected = false;
        Serial.println("[BT] Client disconnected");
        // Layer 2: BT controller disconnected → safety stop
        if (controller.active && controller.clientNum == BT_CLIENT) {
            Serial.println("[SAFETY-L2] BT controller disconnected — stopping all actuators");
            safetyStopAll();
            releaseController("bluetooth disconnected");
        }
    }

    // Read incoming data line by line
    while (SerialBT.available()) {
        char c = SerialBT.read();
        if (c == '\n') {
            if (btBuffer.length() > 0) {
                lastMsgMs = millis();
                handleCommand(BT_CLIENT, btBuffer.c_str(), btBuffer.length());
                btBuffer = "";
            }
        } else if (c != '\r') {
            btBuffer += c;
            // Prevent buffer overflow
            if (btBuffer.length() > 512) {
                btBuffer = "";
            }
        }
    }
}

// ── USB serial processing ────────────────────────────────────
void processUSBSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            if (usbBuffer.length() > 0 && usbBuffer[0] == '{') {
                // Only process lines that look like JSON
                lastMsgMs = millis();
                usbClientActive = true;
                handleCommand(USB_CLIENT, usbBuffer.c_str(), usbBuffer.length());
            }
            usbBuffer = "";
        } else if (c != '\r') {
            usbBuffer += c;
            if (usbBuffer.length() > 512) {
                usbBuffer = "";
            }
        }
    }
}

// ── Arduino entry points ────────────────────────────────────
void setup() {
    // ── 最先执行：TEC 引脚全部拉低 + DRV8833 休眠 ──
    // 防止上电/复位/串口连接瞬间 GPIO 浮空导致 TEC 意外发热
    PeltierController::earlyPinInit();

    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("  CrawlBand Actuator Server");
    Serial.println("  WebSocket + Bluetooth + USB Serial");
    Serial.println("========================================\n");

    initLRA();
    initTEC();
    initWiFi();
    initBluetooth();

    ws.begin();
    ws.onEvent(onWebSocketEvent);
    Serial.printf("[WS] Server started on port 81\n");
#if USE_AP_MODE
    Serial.printf("[WS] Connect to: ws://%s:81\n", WiFi.softAPIP().toString().c_str());
#else
    Serial.printf("[WS] Connect to: ws://%s:81\n", WiFi.localIP().toString().c_str());
#endif
    Serial.println("[USB] Serial command input enabled (send JSON)");
    Serial.println("\nReady. Accepting WebSocket, Bluetooth, or USB Serial connections.\n");
}

void loop() {
    ws.loop();
    processBluetooth();
    processUSBSerial();

#if !USE_AP_MODE
    // 非阻塞 WiFi 重连：不阻塞 loop()，USB/BT 始终可用
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiReady) {
            // 之前连上过，刚断开
            Serial.println("[WiFi] Lost connection");
            wifiReady = false;
            // 如果 WS 客户端是 controller，释放
            if (controller.active && controller.clientNum != BT_CLIENT &&
                controller.clientNum != USB_CLIENT) {
                releaseController("WiFi disconnected");
            }
            wifiRetryMs = millis();
        }
        // 每隔 WIFI_RETRY_INTERVAL_MS 尝试一次，不阻塞
        if (millis() - wifiRetryMs > WIFI_RETRY_INTERVAL_MS) {
            WiFi.reconnect();
            wifiRetryMs = millis();
        }
    } else if (!wifiReady) {
        // 刚重连成功
        wifiReady = true;
        Serial.printf("[WiFi] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
    }
#endif

    // Layer 3: Global watchdog
    bool hasClients = ws.connectedClients() > 0 || btConnected;
    if (hasClients && (millis() - lastMsgMs > SAFETY_TIMEOUT_MS)) {
        Serial.println("[SAFETY-L3] No messages for 60s — stopping all actuators");
        safetyStopAll();
        lastMsgMs = millis();
    }
}
