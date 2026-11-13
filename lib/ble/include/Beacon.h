#pragma once
#include <stdint.h>

/// BLE advertiser of Node A (layout documented in BeaconFrame.h):
///   advert        = iBeacon  UUID / major = route / minor = bus   -> wakes the Android app
///   scan response = rolling presence token -> proves the phone heard this bus recently
/// NimBLEDevice::init() must have been called before begin().
namespace beacon {

struct Config {
    const char *systemUuid;
    uint16_t major;             // route id
    uint16_t minor;             // bus id
    int8_t txPowerAt1m;
    uint16_t intervalMs;
};

bool begin(const Config &cfg);

/// Puts the token in the scan response. A false clockSynced clears it: an unsynced
/// slot is not unix time, so the server cannot check it.
void updateToken(uint32_t slot, const uint8_t token[16], bool clockSynced);

/// Scan response off. The iBeacon advert keeps running.
void clearToken();

bool advertising();

} // namespace beacon
