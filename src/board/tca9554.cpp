#include "tca9554.h"

#include <Wire.h>

namespace board {

bool Tca9554::write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool Tca9554::read(uint8_t reg, uint8_t& val) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom((int)addr_, 1) != 1) return false;
    val = Wire.read();
    return true;
}

bool Tca9554::begin(uint8_t configMask) {
    // Read back the current output latch so we don't glitch pins that the
    // bootloader / previous firmware already set.
    if (!read(REG_OUTPUT, outputShadow_)) return false;
    return write(REG_CONFIG, configMask);
}

bool Tca9554::setOutput(uint8_t pin, bool high) {
    if (pin < 1 || pin > 8) return false;
    uint8_t mask = 1u << (pin - 1);
    uint8_t next = high ? (outputShadow_ | mask) : (outputShadow_ & ~mask);
    if (!write(REG_OUTPUT, next)) return false;
    outputShadow_ = next;
    return true;
}

bool Tca9554::readInput(uint8_t pin, bool& high) {
    if (pin < 1 || pin > 8) return false;
    uint8_t v;
    if (!read(REG_INPUT, v)) return false;
    high = (v >> (pin - 1)) & 1;
    return true;
}

}  // namespace board
