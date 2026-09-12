#include "rtc.h"

#include <Wire.h>

#include "esp_log.h"

static const char* TAG = "rtc";

namespace board {

static constexpr uint8_t ADDR        = 0x51;
static constexpr uint8_t REG_CTRL1   = 0x00;
static constexpr uint8_t REG_SECONDS = 0x04;   // then min, hour, day, weekday, month, year
static constexpr uint8_t CTRL1_STOP  = 0x20;
static constexpr uint8_t CTRL1_CAPSEL = 0x01;  // 12.5 pF crystal load, as Waveshare sets
static constexpr uint8_t SEC_OS      = 0x80;   // oscillator stopped since last write

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t dec2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool Rtc::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((int)ADDR, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool Rtc::writeRegs(uint8_t reg, const uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(ADDR);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
    return Wire.endTransmission(true) == 0;
}

bool Rtc::begin() {
    uint8_t ctrl = CTRL1_CAPSEL;   // running, 24 h mode
    if (!writeRegs(REG_CTRL1, &ctrl, 1) || !readRegs(REG_CTRL1, &ctrl, 1)) {
        ESP_LOGW(TAG, "PCF85063 not responding");
        return false;
    }
    ok_ = true;
    uint8_t sec;
    if (!readRegs(REG_SECONDS, &sec, 1)) return false;
    lost_ = (sec & SEC_OS) != 0;
    if (lost_) {
        // Power was lost: restart the count at 2000-01-01 00:00:00 (clears OS).
        const uint8_t t[7] = {0, 0, 0, dec2bcd(1), 6 /*Sat*/, dec2bcd(1), 0};
        writeRegs(REG_SECONDS, t, sizeof t);
        ESP_LOGW(TAG, "oscillator had stopped; clock restarted at 2000-01-01");
    } else {
        uint32_t s;
        if (now(s)) ESP_LOGI(TAG, "clock running, %lu s since 2000-01-01", (unsigned long)s);
    }
    return true;
}

// Days from 2000-01-01 to y-m-d (y = 2000..2099).
static uint32_t daysFromCivil(int y, int m, int d) {
    static const uint16_t cum[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int yy = y - 2000;
    uint32_t days = (uint32_t)yy * 365 + (yy + 3) / 4;   // leap days before this year (2000 is leap)
    days += cum[m - 1] + (d - 1);
    bool leap = (yy % 4) == 0;
    if (leap && m > 2) days += 1;
    return days;
}

bool Rtc::now(uint32_t& secs) {
    if (!ok_) return false;
    uint8_t b[7];
    if (!readRegs(REG_SECONDS, b, sizeof b)) return false;
    if (b[0] & SEC_OS) { lost_ = true; return false; }
    int sec = bcd2dec(b[0] & 0x7F), min = bcd2dec(b[1] & 0x7F), hour = bcd2dec(b[2] & 0x3F);
    int day = bcd2dec(b[3] & 0x3F), mon = bcd2dec(b[5] & 0x1F), year = 2000 + bcd2dec(b[6]);
    if (mon < 1 || mon > 12 || day < 1 || day > 31) return false;
    secs = daysFromCivil(year, mon, day) * 86400UL + hour * 3600UL + min * 60UL + sec;
    return true;
}

}  // namespace board
