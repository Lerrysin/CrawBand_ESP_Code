#pragma once
#include "PCA9548A.h"
#include "TM6605.h"

/// Manages 6 LRA motors connected via PCA9548A channels 2-7.
///
/// Motor index:  0   1   2   3   4   5
/// PCA channel:  2   3   4   5   6   7
///
class LRAManager {
public:
    static constexpr uint8_t NUM_MOTORS       = 6;
    static constexpr uint8_t FIRST_CHANNEL    = 2;   // PCA9548A channel for motor 0
    static constexpr uint8_t ALL_MOTORS_MASK  = 0b11111100; // channels 2-7

    LRAManager(PCA9548A &mux, TM6605 &driver)
        : _mux(mux), _driver(driver) {}

    /// Initialize and probe all 6 TM6605 chips. Returns bitmask of found motors.
    uint8_t begin() {
        uint8_t found = 0;
        for (uint8_t i = 0; i < NUM_MOTORS; i++) {
            _mux.selectChannel(FIRST_CHANNEL + i);
            if (_driver.probe() == 0) {
                found |= (1 << i);
                Serial.printf("  LRA %u (ch %u): OK\n", i, FIRST_CHANNEL + i);
            } else {
                Serial.printf("  LRA %u (ch %u): NOT FOUND\n", i, FIRST_CHANNEL + i);
            }
        }
        _mux.selectNone();
        return found;
    }

    // ── Single motor control ───────────────────────────────────

    /// Fire an effect on one motor (0-5)
    void fire(uint8_t motor, uint8_t effect) {
        if (motor >= NUM_MOTORS) return;
        _mux.selectChannel(FIRST_CHANNEL + motor);
        _driver.fire(effect);
    }

    /// Stop one motor
    void stopMotor(uint8_t motor) {
        if (motor >= NUM_MOTORS) return;
        _mux.selectChannel(FIRST_CHANNEL + motor);
        _driver.stop();
    }

    // ── Broadcast (same command to multiple/all motors) ────────

    /// Fire the same effect on ALL motors simultaneously
    void fireAll(uint8_t effect) {
        _mux.selectChannels(ALL_MOTORS_MASK);
        _driver.fire(effect);
    }

    /// Fire the same effect on a subset of motors (bitmask, bit0=motor0)
    void fireGroup(uint8_t motorMask, uint8_t effect) {
        uint8_t chMask = _motorMaskToChannelMask(motorMask);
        _mux.selectChannels(chMask);
        _driver.fire(effect);
    }

    /// Stop all motors
    void stopAll() {
        _mux.selectChannels(ALL_MOTORS_MASK);
        _driver.stop();
    }

    // ── Per-motor different effects (sequential, ~1ms total) ───

    /// Fire different effects on each motor. Array of 6 effect IDs.
    /// Pass 0 to skip a motor.
    void fireEach(const uint8_t effects[NUM_MOTORS]) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) {
            if (effects[i] == 0) continue;
            _mux.selectChannel(FIRST_CHANNEL + i);
            _driver.fire(effects[i]);
        }
    }

    // ── Sequential pattern playback ────────────────────────────

    /// Play a wave pattern: fire each motor in order with a delay between them
    void wave(uint8_t effect, uint32_t intervalMs, bool reverse = false) {
        for (uint8_t i = 0; i < NUM_MOTORS; i++) {
            uint8_t idx = reverse ? (NUM_MOTORS - 1 - i) : i;
            fire(idx, effect);
            if (i < NUM_MOTORS - 1) delay(intervalMs);
        }
    }

private:
    /// Convert motor bitmask (bit0=motor0) to PCA9548A channel bitmask
    uint8_t _motorMaskToChannelMask(uint8_t motorMask) {
        return (motorMask & 0x3F) << FIRST_CHANNEL;
    }

    PCA9548A &_mux;
    TM6605   &_driver;
};
