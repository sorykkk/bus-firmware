#include "Identity.h"
#include <Preferences.h>

static constexpr const char *kNs = "busid";

void Identity::load(uint16_t defRoute, uint16_t defBus) {
    char buf[13];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)ESP.getEfuseMac());
    deviceId = String(buf);

    Preferences p;
    // read-write so the namespace is created on first boot (read-only begin fails otherwise)
    p.begin(kNs, false);
    fromNvs = p.isKey("bus") || p.isKey("route");
    busId = p.getUShort("bus", defBus);
    routeId = p.getUShort("route", defRoute);
    p.end();
}

void Identity::save() {
    Preferences p;
    p.begin(kNs, false);
    p.putUShort("bus", busId);
    p.putUShort("route", routeId);
    p.end();
}

void Identity::clear() {
    Preferences p;
    p.begin(kNs, false);
    p.clear();
    p.end();
}
