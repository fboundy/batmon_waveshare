#include "qmi8658.h"

#include <Wire.h>

#include "esp_log.h"

static const char* TAG = "qmi8658";

namespace board {

static constexpr uint8_t REG_WHO_AM_I = 0x00;   // reads 0x05
static constexpr uint8_t REG_CTRL1    = 0x02;   // bit6: address auto-increment
static constexpr uint8_t REG_CTRL2    = 0x03;   // accel: [6:4] range, [3:0] ODR
static constexpr uint8_t REG_CTRL7    = 0x08;   // bit0: accel enable
static constexpr uint8_t REG_AX_L     = 0x35;   // ax, ay, az int16 LE

bool Qmi8658::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((int)addr_, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool Qmi8658::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr_);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission(true) == 0;
}

bool Qmi8658::begin(uint8_t addr) {
    addr_ = addr;
    uint8_t id = 0;
    if (!readRegs(REG_WHO_AM_I, &id, 1) || id != 0x05) {
        ESP_LOGW(TAG, "not found (id 0x%02x)", id);
        return false;
    }
    uint8_t ctrl1 = 0;
    readRegs(REG_CTRL1, &ctrl1, 1);
    writeReg(REG_CTRL1, (ctrl1 & 0xFE) | 0x40);   // auto-increment, oscillator on
    writeReg(REG_CTRL2, 0x00 | 0x05);             // +/-2 g, ~125 Hz
    writeReg(REG_CTRL7, 0x01);                    // accelerometer on, gyro off
    ok_ = true;
    ESP_LOGI(TAG, "accelerometer enabled");
    return true;
}

bool Qmi8658::read(float& ax, float& ay, float& az) {
    if (!ok_) return false;
    uint8_t b[6];
    if (!readRegs(REG_AX_L, b, 6)) return false;
    const float scale = 2.0f / 32768.0f;
    ax = (int16_t)(b[0] | (b[1] << 8)) * scale;
    ay = (int16_t)(b[2] | (b[3] << 8)) * scale;
    az = (int16_t)(b[4] | (b[5] << 8)) * scale;
    return true;
}

}  // namespace board
