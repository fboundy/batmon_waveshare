#include "button.h"

#include <Arduino.h>

namespace board {

static constexpr uint32_t DEBOUNCE_MS = 30;

void Button::begin(int pin, uint32_t longMs) {
    pin_ = pin;
    longMs_ = longMs;
    if (pin_ >= 0) pinMode(pin_, INPUT_PULLUP);
}

ButtonEvent Button::poll() {
    if (pin_ < 0) return ButtonEvent::None;
    uint32_t now = millis();
    bool pressed = digitalRead(pin_) == LOW;

    if (pressed != down_) {
        if (now - lastChangeMs_ < DEBOUNCE_MS) return ButtonEvent::None;
        lastChangeMs_ = now;
        down_ = pressed;
        if (pressed) {
            downMs_ = now;
            longFired_ = false;
        } else if (!longFired_) {
            return ButtonEvent::Short;
        }
        return ButtonEvent::None;
    }
    if (down_ && !longFired_ && now - downMs_ >= longMs_) {
        longFired_ = true;
        return ButtonEvent::Long;
    }
    return ButtonEvent::None;
}

}  // namespace board
