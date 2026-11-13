#pragma once
#include <Arduino.h>

/// Device identity:
///   deviceId        = chip MAC (unique, immutable), sent as X-Device-Id
///   busId / routeId = config.ini [identity], unless overridden in NVS from the serial console
struct Identity {
    uint16_t busId = 0;
    uint16_t routeId = 0;
    String deviceId;
    bool fromNvs = false;

    void load(uint16_t defRoute, uint16_t defBus);
    void save();
    static void clear();
};
