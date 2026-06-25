#pragma once
#include <Arduino.h>
#include <Wire.h>

/// PCA9548A I2C multiplexer driver
class PCA9548A {
public:
    /// @param addr 7-bit address (0x70 when A0=A1=A2=GND)
    PCA9548A(uint8_t addr = 0x70, TwoWire *wire = &Wire)
        : _addr(addr), _wire(wire), _currentMask(0) {}

    void begin() {
        // no special init needed; just clear all channels
        selectNone();
    }

    /// Enable a single channel (0-7), disable all others
    void selectChannel(uint8_t ch) {
        uint8_t mask = 1 << ch;
        if (_currentMask != mask) {
            _writeMask(mask);
        }
    }

    /// Enable multiple channels simultaneously via bitmask
    /// e.g. 0b01111100 = channels 2,3,4,5,6 (bits 2-6)
    void selectChannels(uint8_t mask) {
        if (_currentMask != mask) {
            _writeMask(mask);
        }
    }

    /// Disable all channels
    void selectNone() {
        _writeMask(0x00);
    }

    uint8_t currentMask() const { return _currentMask; }

private:
    void _writeMask(uint8_t mask) {
        _wire->beginTransmission(_addr);
        _wire->write(mask);
        _wire->endTransmission();
        _currentMask = mask;
    }

    uint8_t  _addr;
    TwoWire *_wire;
    uint8_t  _currentMask;
};
