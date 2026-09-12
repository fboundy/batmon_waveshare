#include "settings.h"

#include <Preferences.h>
#include <string.h>

Settings g_settings;

static const char* NS = "batmon";

void Settings::load() {
    Preferences p;
    if (!p.begin(NS, true)) return;   // nothing stored yet
    capacityAh     = p.getFloat("cap", capacityAh);
    p.getString("addr", deviceAddr, sizeof deviceAddr);
    deviceAddrType = p.getUChar("addrt", deviceAddrType);
    brightness     = p.getUChar("bright", brightness);
    fahrenheit     = p.getBool("degf", fahrenheit);
    pollMs         = p.getUShort("poll", pollMs);
    chartMask      = p.getUChar("cmask", chartMask);
    chartRange     = p.getUChar("crange", chartRange);
    relayFollowsPhone = p.getBool("relayph", relayFollowsPhone);
    presenceTimeoutS = p.getUShort("phto", presenceTimeoutS);
    p.end();
}

void Settings::save() const {
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putFloat("cap", capacityAh);
    p.putString("addr", deviceAddr);
    p.putUChar("addrt", deviceAddrType);
    p.putUChar("bright", brightness);
    p.putBool("degf", fahrenheit);
    p.putUShort("poll", pollMs);
    p.putUChar("cmask", chartMask);
    p.putUChar("crange", chartRange);
    p.putBool("relayph", relayFollowsPhone);
    p.putUShort("phto", presenceTimeoutS);
    p.end();
}

void Settings::reset() {
    *this = Settings{};
    Preferences p;
    if (p.begin(NS, false)) {
        p.clear();
        p.end();
    }
}
