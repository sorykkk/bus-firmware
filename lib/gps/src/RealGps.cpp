#ifdef USE_REAL_GPS
#include "RealGps.h"

RealGps::RealGps(int rxPin, int txPin, uint32_t baud) : rx_(rxPin), tx_(txPin), baud_(baud), ser_(2) {}

bool RealGps::begin() {
    ser_.setRxBufferSize(1024);
    ser_.begin(baud_, SERIAL_8N1, rx_, tx_);
    return true;
}

void RealGps::update(uint32_t nowMs) {
    while(ser_.available())
        gps_.encode(ser_.read());

    fix_.fix_ok = gps_.location.isValid() && gps_.location.age() < 3000;
    if(fix_.fix_ok) {
        fix_.lat = gps_.location.lat();
        fix_.lon = gps_.location.lng();
        fix_.speed_kmh = gps_.speed.isValid() ? gps_.speed.kmph() : 0;
        if(gps_.course.isValid())
            fix_.heading_deg = gps_.course.deg();
    }
    fix_.sats = gps_.satellites.isValid() ? gps_.satellites.value() : 0;
    fix_.ts_ms = nowMs;
}

#endif // USE_REAL_GPS
