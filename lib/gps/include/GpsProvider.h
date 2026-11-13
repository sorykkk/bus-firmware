#pragma once
#include <stdint.h>

/// One interface, two implementations (SimGps / RealGps). The rest of the firmware
/// never knows which one it is talking to.
struct GpsFix {
    double lat = 0, lon = 0;
    float speed_kmh = 0;
    float heading_deg = 0;
    bool fix_ok = false;
    uint8_t sats = 0;
    int16_t stop_index = -1;        // sim only: stop the bus is standing at, else -1
    int16_t last_stop_index = -1;   // sim only: last stop visited, else -1
    uint32_t ts_ms = 0;             // local millis() when the fix was produced
};

class GpsProvider {
public:
    virtual ~GpsProvider() {}
    virtual bool begin() = 0;
    virtual void update(uint32_t nowMs) = 0;    // call every 20-100 ms
    virtual GpsFix fix() const = 0;
    virtual bool simulated() const = 0;
};
