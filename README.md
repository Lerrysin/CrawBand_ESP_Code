# CrawlBand ESP32 Firmware

CrawlBand ESP32 firmware for driving a wearable haptic and thermal actuator band. The ESP32 acts as a transport-agnostic actuator controller: it receives JSON commands over WebSocket, Bluetooth SPP, or USB Serial, then dispatches them to LRA vibration motors and Peltier thermal modules.

## Features

- 6 LRA haptic motors through a PCA9548A I2C multiplexer and TM6605 LRA drivers
- 2 Peltier TEC channels through a DRV8833 dual H-bridge module
- WebSocket control server on port `81`
- Bluetooth SPP device named `CrawlBand`
- USB Serial JSON command input at `115200`
- Single-controller claim/release model for Unity and experiment clients
- Safety stop on controller disconnect and 60 second command watchdog
- TEC startup protection, voltage duty cap, and heating duty cap

## Hardware Overview

| Subsystem | Hardware | ESP32 pins |
| --- | --- | --- |
| LRA vibration | PCA9548A at `0x70` + 6x TM6605 at `0x2D` | SDA `GPIO21`, SCL `GPIO22` |
| TEC 0 | DRV8833 channel A | `GPIO25`, `GPIO26` |
| TEC 1 | DRV8833 channel B | `GPIO27`, `GPIO14` |
| DRV8833 enable | EEP / nSLEEP | `GPIO13` |

PCA9548A channels `CH2` through `CH7` map to motor indices `0` through `5`. See [HARDWARE_DOC.md](HARDWARE_DOC.md) for wiring details, power notes, and component specs.

## Project Layout

```text
.
├── platformio.ini              # PlatformIO build config
├── HARDWARE_DOC.md             # Hardware wiring and subsystem notes
├── include/
│   ├── PCA9548A.h              # I2C mux driver
│   ├── TM6605.h                # TM6605 LRA driver
│   ├── LRAManager.h            # 6-motor LRA manager
│   ├── PeltierController.h     # DRV8833 + TEC controller
│   └── secrets.example.h       # Local Wi-Fi config template
├── src/
│   ├── ws_server.cpp           # Active firmware used by PlatformIO
│   ├── main.cpp                # Earlier/alternate firmware entry
│   ├── test_minimal.cpp
│   └── test_tec_dual.cpp
├── test/
└── tools/
```

`platformio.ini` uses `build_src_filter = -<*> +<ws_server.cpp>`, so the active firmware entry point is `src/ws_server.cpp`.

## Setup

Install PlatformIO, then configure local Wi-Fi:

```powershell
Copy-Item include\secrets.example.h include\secrets.h
```

Edit `include/secrets.h`:

```cpp
#define USE_AP_MODE 0
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
```

For AP mode, set `USE_AP_MODE` to `1`. In station mode, the firmware tries to join the configured router. If Wi-Fi is unavailable, USB Serial and Bluetooth control still work.

## Build And Upload

```powershell
pio run -e ws_server
pio run -e ws_server -t upload
pio device monitor -b 115200
```

After boot, the serial monitor prints the WebSocket endpoint, for example:

```text
ws://192.168.1.123:81
```

## Control Transports

- WebSocket: connect to `ws://<esp32-ip>:81` and send JSON text frames.
- Bluetooth: pair with `CrawlBand`, then send newline-terminated JSON over the assigned COM port.
- USB Serial: send newline-terminated JSON at `115200`. Lines must start with `{`.

## JSON Command Flow

Commands include a `cmd` field and should include a `seq` number. Actuator commands require a controller claim first.

```json
{"cmd":"claim","role":"unity","seq":1}
{"cmd":"lra.fire","motor":0,"effect":14,"seq":2}
{"cmd":"tec.cool","tec":0,"percent":40,"seq":3}
{"cmd":"stopAll","seq":4}
{"cmd":"release","seq":5}
```

Valid claim roles:

- `unity`
- `experiment_ab`

Commands that do not require claim:

| Command | Purpose |
| --- | --- |
| `ping` | Returns `{"ack":"pong"}` |
| `status` | Returns readiness, controller, uptime, RSSI, and client state |
| `stopAll` | Stops all LRA motors and TEC channels immediately |
| `claim` | Claims controller ownership |
| `release` | Releases controller ownership |

Actuator commands after claim:

| Command | Fields | Purpose |
| --- | --- | --- |
| `lra.fire` | `motor`, `effect` | Fire one motor |
| `lra.fireAll` | `effect` | Fire all motors |
| `lra.fireGroup` | `mask`, `effect` | Fire selected motors, bit 0 = motor 0 |
| `lra.fireEach` | `effects` | Fire per-motor effect array |
| `lra.wave` | `effect`, `interval`, `reverse` | Sequential motor wave |
| `lra.stop` | none | Stop all LRA motors |
| `lra.stopMotor` | `motor` | Stop one LRA motor |
| `tec.cool` | `tec`, `percent` | Cool TEC 0 or 1 |
| `tec.heat` | `tec`, `percent` | Heat TEC 0 or 1, capped for skin safety |
| `tec.stop` | optional `tec` | Stop one TEC, or all if omitted |

Responses are compact JSON acknowledgements such as:

```json
{"ack":"ok","seq":2}
{"ack":"error","seq":2,"msg":"not claimed"}
```

## Safety Notes

- `stopAll` should be sent before disconnecting an experiment or Unity client.
- If the active WebSocket or Bluetooth controller disconnects, the firmware stops all actuators.
- A global watchdog stops all actuators after 60 seconds without incoming messages.
- TEC heating is capped in firmware. Still verify skin temperature and current limits during experiments.
- Power TECs from a suitable external supply and share ground with the ESP32. Do not power TECs from the ESP32 3.3V pin.

## Notes

`tools/test_udp_sender.py` is a UDP packet simulator from an earlier test flow. The active `ws_server` firmware listens on WebSocket, Bluetooth SPP, and USB Serial, not UDP.
