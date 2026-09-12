// QMI8658 6-axis IMU (I2C 0x6B) - accelerometer only, used to work out
// which way up the board is mounted.
#pragma once

#include <stdint.h>

namespace board {

class Qmi8658 {
public:
    bool begin(uint8_t addr = 0x6B);
    // Acceleration in g.  Returns false if the chip is absent or the read failed.
    bool read(float& ax, float& ay, float& az);
    bool present() const { return ok_; }

private:
    bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);
    bool writeReg(uint8_t reg, uint8_t val);

    uint8_t addr_ = 0x6B;
    bool ok_ = false;
};

}  // namespace board
