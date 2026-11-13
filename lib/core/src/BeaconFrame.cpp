#include "BeaconFrame.h"
#include <string.h>

namespace beaconframe {

static int hexVal(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseUuid(const char *s, uint8_t out[16]) {
    if(!s)
        return false;
    int n = 0;
    for(; *s; ++s) {
        if(*s == '-')
            continue;
        int v = hexVal(*s);
        if(v < 0 || n >= 32)
            return false;
        if(n % 2 == 0)
            out[n / 2] = (uint8_t)(v << 4);
        else
            out[n / 2] |= (uint8_t)v;
        n++;
    }
    return n == 32;
}

void buildIBeacon(const uint8_t uuid[16], uint16_t major, uint16_t minor, int8_t tx, uint8_t d[kIBeaconLen]) {
    d[0] = kAppleCompanyId & 0xFF;
    d[1] = kAppleCompanyId >> 8;
    d[2] = 0x02;                        // iBeacon type
    d[3] = 0x15;                        // 21 bytes follow
    memcpy(d + 4, uuid, 16);
    d[20] = major >> 8;
    d[21] = major & 0xFF;
    d[22] = minor >> 8;
    d[23] = minor & 0xFF;
    d[24] = (uint8_t)tx;
}

void buildPresence(uint32_t slot, const uint8_t token[kTokenLen], bool clockSynced, uint8_t d[kPresenceLen]) {
    d[0] = kTestCompanyId & 0xFF;
    d[1] = kTestCompanyId >> 8;
    d[2] = kPresenceVersion;
    d[3] = clockSynced ? kFlagClockSynced : 0;
    d[4] = slot >> 24;
    d[5] = (slot >> 16) & 0xFF;
    d[6] = (slot >> 8) & 0xFF;
    d[7] = slot & 0xFF;
    memcpy(d + 8, token, kTokenLen);
}

bool parsePresence(const uint8_t *d, size_t n, uint32_t &slot, uint8_t token[kTokenLen], bool &clockSynced) {
    if(n < kPresenceLen || d[0] != (kTestCompanyId & 0xFF) || d[1] != (kTestCompanyId >> 8) ||
       d[2] != kPresenceVersion)
        return false;
    clockSynced = (d[3] & kFlagClockSynced) != 0;
    slot = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 8) | d[7];
    memcpy(token, d + 8, kTokenLen);
    return true;
}

} // namespace beaconframe
