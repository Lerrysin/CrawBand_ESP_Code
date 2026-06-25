# CrawlBand Haptic + Thermal Controller — Hardware & Firmware Documentation

## 1. System Overview

CrawlBand uses an ESP-WROOM-32 microcontroller to drive:
- **6 LRA haptic motors** via PCA9548A (I2C mux) + 6x TM6605 drivers
- **2 Peltier (TEC) cooling/heating elements** via DRV8833 dual H-bridge module (external board)

```
                        I2C Bus (400kHz)
  ESP-WROOM-32  ──────────────────────────  PCA9548A (0x70)
   GPIO21 = SDA                              │
   GPIO22 = SCL                              ├── CH2 → TM6605 #0 (0x2D) → LRA Motor 0
                                             ├── CH3 → TM6605 #1 (0x2D) → LRA Motor 1
                    PWM (25kHz)              ├── CH4 → TM6605 #2 (0x2D) → LRA Motor 2
                ┌─────────────── DRV8833     ├── CH5 → TM6605 #3 (0x2D) → LRA Motor 3
   GPIO25 = IN1 ───┤  Ch A → TEC 0           ├── CH6 → TM6605 #4 (0x2D) → LRA Motor 4
   GPIO26 = IN2 ───┤  (OUT1/OUT2)            └── CH7 → TM6605 #5 (0x2D) → LRA Motor 5
   GPIO27 = IN3 ───┤  Ch B → TEC 1
   GPIO14 = IN4 ───┤  (OUT3/OUT4)
   GPIO13 = EEP ───┘
```

### Why No GPIO Selector Lines?

PCA9548A is **I2C-controlled**, not GPIO-controlled. Channel selection is done by writing a
control byte to the PCA9548A's I2C address (0x70). There are no external select pins.

- **A0, A1, A2 pins**: These are static **address pins**, not channel selectors.
  They set the PCA9548A's own I2C slave address. All tied to GND → address = 0x70.
- **Channel selection**: ESP32 sends a single byte over I2C to 0x70. Each bit enables
  a channel (bit 2 = CH2, bit 3 = CH3, ..., bit 7 = CH7).
- **Advantage**: Only 2 wires (SDA + SCL) needed from ESP32 to control all 6 motors.

---

## 2. Wiring Diagram

### ESP32 ↔ PCA9548A

| ESP32 Pin  | PCA9548A Pin | Description        |
|------------|-------------|---------------------|
| GPIO21     | SDA         | I2C data line       |
| GPIO22     | SCL         | I2C clock line      |
| 3.3V       | VCC         | Power supply        |
| GND        | GND         | Ground              |
| GND        | A0          | Address bit 0 = LOW |
| GND        | A1          | Address bit 1 = LOW |
| GND        | A2          | Address bit 2 = LOW |
| (optional) | RESET       | Active-low reset (internal pull-up, can tie to 3.3V or ESP32 GPIO) |

### ESP32 ↔ DRV8833 Module

| ESP32 Pin  | Module Pin | Description                        |
|------------|------------|------------------------------------|
| GPIO25     | IN1        | TEC 0 PWM direction A              |
| GPIO26     | IN2        | TEC 0 PWM direction B              |
| GPIO27     | IN3        | TEC 1 PWM direction A              |
| GPIO14     | IN4        | TEC 1 PWM direction B              |
| GPIO13     | EEP        | Sleep control (LOW=sleep, HIGH=active) |
| GND        | GND        | Common ground                      |
| 5V         | VCC        | Module + motor power supply        |
| —          | ULT        | Fault alarm output (LOW on over-temp/over-current) |

**EEP (Sleep) Pin**: LOW = sleep mode, HIGH = active. The module has a J1 solder jumper
on the back that, when connected, ties EEP to VCC (always-on). On the actual board the
J1 pads are **not bridged**, so EEP must be driven by GPIO13. The firmware drives it HIGH
at startup via `wake()`.

**ULT Pin**: Open-drain fault output. Pulls LOW when over-temperature or over-current
protection activates. Currently unconnected to ESP32 — can be wired to any GPIO with
an internal pull-up for fault detection.

**VCC Power Supply**: Connect VCC to an external 5V supply (NOT from ESP32's 3.3V pin).
The TEC max voltage is 3.8V; the firmware limits PWM duty to ~76% when VCC=5V to keep
average output voltage within safe range.

### DRV8833 Module ↔ Peltier TEC

| Module Pin | Connection     |
|------------|----------------|
| OUT1       | TEC 0 (+)      |
| OUT2       | TEC 0 (−)      |
| OUT3       | TEC 1 (+)      |
| OUT4       | TEC 1 (−)      |

> **Note**: Peltier polarity determines cool vs heat direction.
> If cooling/heating is reversed, swap the OUT wires on that TEC.

### PCA9548A Channel ↔ TM6605

Each TM6605 board connects to one PCA9548A channel output:

| PCA9548A Channel | TM6605 Board | Motor Index |
|-----------------|-------------|-------------|
| CH2 (SD2/SC2)   | TM6605 #0   | Motor 0     |
| CH3 (SD3/SC3)   | TM6605 #1   | Motor 1     |
| CH4 (SD4/SC4)   | TM6605 #2   | Motor 2     |
| CH5 (SD5/SC5)   | TM6605 #3   | Motor 3     |
| CH6 (SD6/SC6)   | TM6605 #4   | Motor 4     |
| CH7 (SD7/SC7)   | TM6605 #5   | Motor 5     |

### TM6605 ↔ LRA Motor

Each TM6605 board:

| TM6605 Pin| Connection       |
|-----------|-----------------|
| SDA       | PCA9548A SDx    |
| SCL       | PCA9548A SCx    |
| VDD       | 3.3V            |
| GND       | GND             |
| OUTP      | LRA motor (+)   |
| OUTN      | LRA motor (−)   |

### Pull-up Resistors

- **10kΩ pull-ups on SDA & SCL** on the main I2C bus (ESP32 side only).
- PCA9548A channel outputs do NOT need additional pull-ups
  (the switch connects upstream pull-ups through to downstream devices).

### PCA9548A Address Configuration

| A2 | A1 | A0 | I2C Address |
|----|----|----|-------------|
| GND | GND | GND | 0x70     |
| GND | GND | VCC | 0x71     |
| GND | VCC | GND | 0x72     |
| ... | ... | ... | ...      |
| VCC | VCC | VCC | 0x77     |

Current config: A0=A1=A2=GND → **address 0x70**.

### RESET Pin

- Active-low, with internal pull-up.
- Tie to VCC (3.3V) for normal operation, or connect to an ESP32 GPIO for software-controlled bus recovery.
- Asserting RESET (pulling LOW) clears the channel selection register.

---

## 3. Project Structure

```
ESP_Code/
├── platformio.ini              # PlatformIO build config
├── HARDWARE_DOC.md             # This file
├── .gitignore
├── include/
│   ├── PCA9548A.h              # I2C multiplexer driver       ┐
│   ├── TM6605.h                # LRA haptic driver            ├─ LRA subsystem
│   ├── LRAManager.h            # 6-motor manager              ┘
│   └── PeltierController.h     # DRV8833 + 2x TEC driver     ── TEC subsystem
├── src/
│   └── main.cpp                # Mode manager + serial command router
├── lib/                        # (for future external libs)
└── test/                       # (for future unit tests)
```

### Architecture: Two Independent Subsystems

```
main.cpp (mode manager + command router)
    │
    ├── LRA Subsystem  (vibration feedback)
    │     PCA9548A.h → TM6605.h → LRAManager.h
    │     Comm: I2C (GPIO21/22)
    │     Can run independently
    │
    └── TEC Subsystem  (thermal feedback)
          PeltierController.h
          Comm: PWM (GPIO25/26/27/14) + enable (GPIO13)
          Can run independently
```

**Mode system**: runtime switching via `mode <lra|tec|all>` command.
When switching modes, inactive subsystem is automatically stopped.
Both subsystems are initialized at boot regardless of mode.

---

## 4. IC Specifications

### PCA9548A (Texas Instruments)

| Parameter         | Value                |
|-------------------|----------------------|
| Type              | 8-channel I2C switch |
| VCC               | 2.3V – 5.5V          |
| I2C Speed         | Up to 400 kHz        |
| I2C Address Range | 0x70 – 0x77          |
| Channels          | 8 (CH0 – CH7)        |
| Package           | TSSOP-24 / SOIC-24   |
| Datasheet         | `PCA9548A/`          |

### TM6605 (Titan Micro Electronics)

| Parameter         | Value                           |
|-------------------|---------------------------------|
| Type              | LRA haptic driver               |
| VDD               | 2.7V – 5.2V                     |
| I2C Address       | 0x5A (write) / 0x2D (7-bit)     |
| Built-in Effects  | 44 effects (ID 1–123)           |
| Resonance Range   | 140 – 220 Hz (auto-tracking)    |
| Control Registers | 0x04 (effect), 0x0C (play/stop) |
| Datasheet         | `TM6605_LRA_Driver/`            |

### DRV8833 Module (external board, TI DRV8833 chip)

| Parameter          | Value                              |
|--------------------|------------------------------------|
| Type               | Dual H-bridge driver module        |
| Supply voltage     | 2.7V – 10.8V (via VCC pin)         |
| Per-channel current| 1.5A continuous                    |
| Control inputs     | IN1/IN2 (Ch A), IN3/IN4 (Ch B)     |
| Outputs            | OUT1/OUT2 (Ch A), OUT3/OUT4 (Ch B) |
| Sleep pin          | EEP (LOW=sleep, HIGH=active)       |
| Fault output       | ULT (LOW on over-temp/over-current)|
| J1 jumper          | On back of board; ties EEP to VCC when bridged. **Not bridged** on actual board — EEP must be driven externally |

### Peltier TEC (x2)

| Parameter         | Value          |
|-------------------|----------------|
| Max voltage       | 3.8V           |
| Max current       | 1.4A           |
| Max cooling power | 2.9W           |
| Resistance        | 2.2 Ω          |
| Max ΔT            | 67°C           |

### ESP-WROOM-32

| Parameter    | Value                            |
|--------------|----------------------------------|
| MCU          | ESP32 dual-core                  |
| Framework    | Arduino (PlatformIO)             |
| I2C SDA      | GPIO21                           |
| I2C SCL      | GPIO22                           |
| I2C Clock    | 400 kHz                          |
| PWM pins     | GPIO25, 26, 27, 14 (TEC control) |
| Digital out  | GPIO13 (DRV8833 module EEP)      |
| Serial Baud  | 115200                           |

---

## 5. Firmware API

### Class: `PCA9548A` (include/PCA9548A.h)

```cpp
PCA9548A(uint8_t addr = 0x70, TwoWire *wire = &Wire);
void begin();                    // Clear all channels
void selectChannel(uint8_t ch);  // Enable one channel (0-7), disable others
void selectChannels(uint8_t mask); // Enable multiple channels (bitmask)
void selectNone();               // Disable all channels
uint8_t currentMask() const;     // Get current channel selection
```

### Class: `TM6605` (include/TM6605.h)

```cpp
TM6605(TwoWire *wire = &Wire);
uint8_t probe();                 // Check device presence (0 = found)
void selectEffect(uint8_t id);   // Write effect ID to register 0x04
void play();                     // Start playback (register 0x0C = 0x01)
void stop();                     // Stop playback  (register 0x0C = 0x00)
void fire(uint8_t effect);       // selectEffect + play (convenience)
```

### Class: `LRAManager` (include/LRAManager.h)

```cpp
LRAManager(PCA9548A &mux, TM6605 &driver);

// Initialization
uint8_t begin();                 // Probe all 6 motors, returns bitmask of found

// Single motor
void fire(uint8_t motor, uint8_t effect);   // Fire effect on motor 0-5
void stopMotor(uint8_t motor);              // Stop one motor

// Broadcast (simultaneous)
void fireAll(uint8_t effect);               // Same effect on all 6 motors
void fireGroup(uint8_t mask, uint8_t effect); // Same effect on subset (bitmask)
void stopAll();                             // Stop all motors

// Per-motor different effects
void fireEach(const uint8_t effects[6]);    // Different effect per motor (0 = skip)

// Sequential patterns
void wave(uint8_t effect, uint32_t intervalMs, bool reverse = false);
```

**Motor index mapping:**
- Motor 0 → PCA9548A CH2
- Motor 1 → PCA9548A CH3
- Motor 2 → PCA9548A CH4
- Motor 3 → PCA9548A CH5
- Motor 4 → PCA9548A CH6
- Motor 5 → PCA9548A CH7

### Class: `PeltierController` (include/PeltierController.h)

```cpp
PeltierController();

void begin();                       // Init PWM, enable DRV8833
void cool(uint8_t tec, uint8_t %);  // Cool TEC 0-1 at 0-100%
void heat(uint8_t tec, uint8_t %);  // Heat TEC 0-1 at 0-100%
void stop(uint8_t tec);             // Stop one TEC
void stopAll();                     // Stop both TECs
void sleep();                       // DRV8833 low-power mode
void wake();                        // Wake DRV8833
```

**TEC index mapping:**
- TEC 0 → DRV8833 module Channel A: IN1(GPIO25)/IN2(GPIO26) → OUT1/OUT2
- TEC 1 → DRV8833 module Channel B: IN3(GPIO27)/IN4(GPIO14) → OUT3/OUT4

**Safety**: PWM duty is capped at ~76% (MAX_DUTY=194) when VM=5V to keep
average voltage under 3.8V. Change `MAX_DUTY` to 255 if using VM=3.3V.

---

## 6. Serial Command Interface

Baud rate: **115200**. Commands are newline-terminated.

### Mode & System Commands (always available)

| Command | Syntax | Description |
|---------|--------|-------------|
| mode    | `mode <lra\|tec\|all>` | Switch active subsystem |
| status  | `status` | Show current mode and subsystem state |
| help    | `help` | Print command help |

### LRA Commands (requires mode: `lra` or `all`)

| Command | Syntax | Description |
|---------|--------|-------------|
| fire    | `fire <motor> <effect>` | Fire effect on one motor (0-5) |
| all     | `all <effect>` | Fire same effect on all motors |
| group   | `group <hex_mask> <effect>` | Fire on motor subset (e.g. `group 0F 1`) |
| each    | `each <e0> <e1> <e2> <e3> <e4> <e5>` | Different effect per motor |
| wave    | `wave <effect> <ms>` | Wave pattern, motor 0→5 |
| waver   | `waver <effect> <ms>` | Wave pattern, motor 5→0 |
| stop    | `stop [motor]` | Stop one motor or all |
| scan    | `scan` | Re-probe all motors |

### TEC Commands (requires mode: `tec` or `all`)

| Command | Syntax | Description |
|---------|--------|-------------|
| cool    | `cool <tec> <percent>` | Cool TEC 0-1 at 0-100% |
| heat    | `heat <tec> <percent>` | Heat TEC 0-1 at 0-100% |
| tecstop | `tecstop [tec]` | Stop one TEC or both |

### Common Effect IDs

| ID  | Name            | Description              |
|-----|-----------------|--------------------------|
| 1   | SharpClick      | Short, sharp click       |
| 4   | InstantClick    | Immediate click          |
| 7   | LightTap        | Gentle tap               |
| 10  | DoubleClick     | Double-tap pattern       |
| 13  | LightPulse      | Soft pulse               |
| 14  | StrongAlert     | Strong alert vibration   |
| 17  | SharpClick2     | Sharp click variant      |
| 21  | MediumClick     | Medium-intensity click   |
| 24  | FlashStrike     | Quick flash strike       |
| 47  | Alert           | Standard alert           |
| 58  | ToggleClick     | Toggle feedback click    |
| 118 | LongAlert       | Extended alert vibration |
| 119 | SoftNoise       | Soft background noise    |
| 123 | Sleep           | Enter low-power mode     |
| 70–93 | Fade/Boost    | Transition effects       |

---

## 7. Build & Upload

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- USB cable connected to ESP-WROOM-32

### Commands

```bash
# Build firmware
cd ESP_Code
pio run

# Upload to ESP32
pio run -t upload

# Open serial monitor
pio device monitor
```

### Build Output (reference)

- RAM usage: ~6.7%
- Flash usage: ~22.2%

---

## 8. I2C Communication Flow

### Selecting a motor (e.g., Motor 2)

```
ESP32 → [I2C WRITE to 0x70] → data: 0b00010000  (bit 4 = CH4 = Motor 2)
         PCA9548A routes SDA/SCL to CH4
ESP32 → [I2C WRITE to 0x2D] → reg 0x04, data: effect_id
ESP32 → [I2C WRITE to 0x2D] → reg 0x0C, data: 0x01  (play)
```

### Broadcasting to all motors

```
ESP32 → [I2C WRITE to 0x70] → data: 0b11111100  (bits 2-7 = CH2-CH7)
         PCA9548A routes SDA/SCL to all 6 channels simultaneously
ESP32 → [I2C WRITE to 0x2D] → reg 0x04, data: effect_id  (all TM6605s receive)
ESP32 → [I2C WRITE to 0x2D] → reg 0x0C, data: 0x01       (all TM6605s play)
```

Note: Broadcasting works because all 6 TM6605 chips share the same I2C address (0x2D).
When multiple channels are enabled, the PCA9548A connects the upstream bus to all
selected downstream buses in parallel, so a single I2C write reaches all devices.
