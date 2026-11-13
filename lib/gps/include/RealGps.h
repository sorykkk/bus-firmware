#pragma once
#ifdef USE_REAL_GPS
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include "GpsProvider.h"

/// NEO-6M / NEO-M8N on UART2 (NMEA, 9600 baud by default).
class RealGps : public GpsProvider {
public:
    RealGps(int rxPin, int txPin, uint32_t baud = 9600);
    bool begin() override;
    void update(uint32_t nowMs) override;
    GpsFix fix() const override { return fix_; }
    bool simulated() const override { return false; }

private:
    int rx_, tx_;
    uint32_t baud_;
    HardwareSerial ser_;
    TinyGPSPlus gps_;
    GpsFix fix_;
};

#endif // USE_REAL_GPS
