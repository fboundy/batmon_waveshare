// PCF85063 real-time clock (I2C 0x51).
//
// There is no time source on the display (no Wi-Fi/NTP) and the RTC battery
// header is unpopulated, so the RTC is used only as a monotonic seconds
// counter that survives resets but not power loss.  On power loss the chip
// sets its OS (oscillator stop) flag; begin() then restarts it at 2000-01-01
// and reports that continuity was lost.
#pragma once

#include <stdint.h>

namespace board {

class Rtc {
public:
    bool begin();

    // Seconds since 2000-01-01 00:00:00 as counted by the chip.
    bool now(uint32_t& secs);

    // true if the oscillator had stopped since the last begin() (power loss),
    // i.e. the count is not comparable with values read before the outage.
    bool lostContinuity() const { return lost_; }

private:
    bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);
    bool writeRegs(uint8_t reg, const uint8_t* buf, uint8_t len);

    bool lost_ = true;
    bool ok_ = false;
};

}  // namespace board
