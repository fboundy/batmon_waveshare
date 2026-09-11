// TCA9554PWR 8-bit I2C IO expander.  On this board it drives the LCD reset /
// chip-select, touch reset, SD chip-select and buzzer; the IMU/RTC interrupts
// come in on it as inputs.  None of these pins are brought out to a header.
#pragma once

#include <stdint.h>

namespace board {

class Tca9554 {
public:
    static constexpr uint8_t REG_INPUT    = 0x00;
    static constexpr uint8_t REG_OUTPUT   = 0x01;
    static constexpr uint8_t REG_POLARITY = 0x02;
    static constexpr uint8_t REG_CONFIG   = 0x03;  // bit=1 -> input

    explicit Tca9554(uint8_t addr) : addr_(addr) {}

    // Configure all 8 pins at once (bit=1 input, bit=0 output).
    bool begin(uint8_t configMask = 0x00);

    // Pin numbers are 1-based to match the Waveshare EXIOn naming.
    bool setOutput(uint8_t pin, bool high);
    bool readInput(uint8_t pin, bool& high);

private:
    bool write(uint8_t reg, uint8_t val);
    bool read(uint8_t reg, uint8_t& val);

    uint8_t addr_;
    uint8_t outputShadow_ = 0xFF;
};

}  // namespace board
