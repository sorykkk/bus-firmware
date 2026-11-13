#include "Geo.h"
#include <cmath>

namespace geo {

static constexpr double kEarthR = 6371000.0;
static constexpr double kDeg = 3.14159265358979323846 / 180.0;

double distanceM(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * kDeg, dLon = (lon2 - lon1) * kDeg;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * kDeg) * std::cos(lat2 * kDeg) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * kEarthR * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double p1 = lat1 * kDeg, p2 = lat2 * kDeg, dl = (lon2 - lon1) * kDeg;
    double y = std::sin(dl) * std::cos(p2);
    double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    double b = std::atan2(y, x) / kDeg;
    return b < 0 ? b + 360.0 : b;
}

void offsetM(double &lat, double &lon, double northM, double eastM) {
    lat += (northM / kEarthR) / kDeg;
    lon += (eastM / (kEarthR * std::cos(lat * kDeg))) / kDeg;
}

} // namespace geo
