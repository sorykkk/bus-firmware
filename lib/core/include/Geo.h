#pragma once

namespace geo {

/// Great-circle distance in metres (haversine).
double distanceM(double lat1, double lon1, double lat2, double lon2);

/// Initial bearing from point 1 to point 2, degrees clockwise from north [0, 360).
double bearingDeg(double lat1, double lon1, double lat2, double lon2);

/// Moves a point by small north/east offsets in metres (flat-earth approximation, fine < 10 km).
void offsetM(double &lat, double &lon, double northM, double eastM);

} // namespace geo
