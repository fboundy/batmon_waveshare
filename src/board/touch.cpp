#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "esp_log.h"

#include "../config.h"
#include "tca9554.h"

static const char* TAG = "touch";

namespace board {

// Register map (from Waveshare Touch_CST820.h)
static constexpr uint8_t REG_GESTURE   = 0x01;   // gesture, then points, xh, xl, yh, yl
static constexpr uint8_t REG_VERSION   = 0x15;
static constexpr uint8_t REG_CHIP_ID   = 0xA7;   // chip, proj, fw
static constexpr uint8_t REG_DIS_SLEEP = 0xFE;   // 0xFF = never auto-sleep

bool Touch::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(CST820_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) return false;
    if (Wire.requestFrom((int)CST820_ADDR, (int)len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool Touch::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(CST820_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission(true) == 0;
}

bool Touch::begin(Tca9554& io) {
    pinMode(PIN_TP_INT, INPUT_PULLUP);

    io.setOutput(EXIO_TP_RST, false);
    delay(10);
    io.setOutput(EXIO_TP_RST, true);
    delay(50);

    // Keep the controller awake; otherwise it sleeps after a few seconds and
    // the first touch afterwards is swallowed.
    writeReg(REG_DIS_SLEEP, 0xFF);

    uint8_t id[3] = {0};
    if (!readRegs(REG_CHIP_ID, id, 3)) {
        ESP_LOGW(TAG, "CST820 not responding");
        return false;
    }
    ESP_LOGI(TAG, "CST820 chip=0x%02x proj=0x%02x fw=0x%02x", id[0], id[1], id[2]);
    return true;
}

TouchPoint Touch::read() {
    TouchPoint tp{};
    uint8_t buf[6];
    if (!readRegs(REG_GESTURE, buf, sizeof buf)) return tp;
    tp.gesture = (Gesture)buf[0];
    uint8_t points = buf[1];
    if (points) {
        tp.pressed = true;
        tp.x = ((buf[2] & 0x0F) << 8) | buf[3];
        tp.y = ((buf[4] & 0x0F) << 8) | buf[5];
        if (tp.x >= LCD_H_RES) tp.x = LCD_H_RES - 1;
        if (tp.y >= LCD_V_RES) tp.y = LCD_V_RES - 1;
    }
    return tp;
}

}  // namespace board
