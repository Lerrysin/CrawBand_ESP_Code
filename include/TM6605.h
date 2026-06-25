#pragma once
#include <Arduino.h>
#include <Wire.h>

/// TM6605 LRA haptic driver (I2C, write-only)
/// Datasheet: slave address 0x5A → 7-bit = 0x2D
class TM6605 {
public:
    static constexpr uint8_t I2C_ADDR    = 0x2D;
    static constexpr uint8_t REG_EFFECT  = 0x04;
    static constexpr uint8_t REG_CONTROL = 0x0C;

    // Built-in haptic effects (44 total)
    enum Effect : uint8_t {
        SharpClick              = 1,
        InstantClick            = 4,
        LightTap                = 7,
        DoubleClick             = 10,
        LightPulse              = 13,
        StrongAlert             = 14,
        MediumAlert             = 15,
        SharpClick2             = 17,
        MediumClick             = 21,
        FlashStrike             = 24,
        DoubleHighClickShort    = 27,
        DoubleMediumClickShort  = 31,
        DoubleFlashStrikeShort  = 34,
        DoubleInstantClickLong  = 37,
        DoubleMediumInstantLong = 41,
        DoubleFlashStrikeLong   = 44,
        Alert                   = 47,
        ToggleClick             = 58,
        LongSlowFade1           = 70,
        LongSlowFade2           = 71,
        MediumSlowFade1         = 72,
        MediumSlowFade2         = 73,
        ShortSlowFade1          = 74,
        ShortSlowFade2          = 75,
        LongFastFade1           = 76,
        LongFastFade2           = 77,
        MediumFastFade1         = 78,
        MediumFastFade2         = 79,
        ShortFastFade1          = 80,
        ShortFastFade2          = 81,
        LongSlowBoost1          = 82,
        LongSlowBoost2          = 83,
        MediumSlowBoost1        = 84,
        MediumSlowBoost2        = 85,
        ShortSlowBoost1         = 86,
        ShortSlowBoost2         = 87,
        LongFastBoost1          = 88,
        LongFastBoost2          = 89,
        MediumFastBoost1        = 90,
        MediumFastBoost2        = 91,
        ShortFastBoost1         = 92,
        ShortFastBoost2         = 93,
        LongAlert               = 118,
        SoftNoise               = 119,
        Sleep                   = 123,
    };

    TM6605(TwoWire *wire = &Wire) : _wire(wire) {}

    /// Check device presence on bus. Returns 0 on success.
    uint8_t probe() {
        _wire->beginTransmission(I2C_ADDR);
        return _wire->endTransmission();
    }

    /// Select an effect (does NOT start playback)
    void selectEffect(uint8_t effect) {
        _writeReg(REG_EFFECT, effect);
    }

    /// Start playback of the selected effect
    void play() {
        _writeReg(REG_CONTROL, 0x01);
    }

    /// Stop playback
    void stop() {
        _writeReg(REG_CONTROL, 0x00);
    }

    /// Select an effect and immediately start playback
    void fire(uint8_t effect) {
        selectEffect(effect);
        play();
    }

private:
    void _writeReg(uint8_t reg, uint8_t val) {
        _wire->beginTransmission(I2C_ADDR);
        _wire->write(reg);
        _wire->write(val);
        _wire->endTransmission();
    }

    TwoWire *_wire;
};
