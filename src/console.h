// Line-oriented command console on the USB serial port.  The only way to
// change settings on boards without touch, and handy for scripting on the
// others.  Type `help`.
#pragma once

namespace console {

void begin();
// Call from loop(); executes any complete line received.
void poll();

}  // namespace console
