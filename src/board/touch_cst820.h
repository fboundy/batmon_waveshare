// CST820 capacitive touch controller (I2C 0x15).  Single touch point plus a
// hardware gesture ID.  Reset is on the IO expander (EXIO2), INT on GPIO16.
#pragma once

#include <stdint.h>

namespace board {

class Tca9554;

enum class Gesture : uint8_t {
    None        = 0x00,
    SwipeUp     = 0x01,
    SwipeDown   = 0x02,
    SwipeLeft   = 0x03,
    SwipeRight  = 0x04,
    SingleClick = 0x05,
    DoubleClick = 0x0B,
    LongPress   = 0x0C,
};

struct TouchPoint {
    bool     pressed;
    uint16_t x;
    uint16_t y;
    Gesture  gesture;
};

class Touch {
public:
    bool begin(Tca9554& io);
    // Polls the controller; returns the current point (pressed=false if none).
    TouchPoint read();

private:
    bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);
    bool writeReg(uint8_t reg, uint8_t val);
};

}  // namespace board
