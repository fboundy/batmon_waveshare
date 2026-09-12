// Debounced active-low push button with short / long press detection.
#pragma once

#include <stdint.h>

#include "board.h"

namespace board {

class Button {
public:
    void begin(int pin, uint32_t longMs);
    // Poll frequently (every few ms).  Returns an event on release (short)
    // or as soon as the hold exceeds longMs (long, once).
    ButtonEvent poll();

private:
    int pin_ = -1;
    uint32_t longMs_ = 800;
    bool down_ = false;
    bool longFired_ = false;
    uint32_t downMs_ = 0;
    uint32_t lastChangeMs_ = 0;
};

}  // namespace board
