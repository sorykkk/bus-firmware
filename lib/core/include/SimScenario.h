#pragma once
#include <stdint.h>
#include <string>
#include <vector>

/// Data model of the simulated GPS, loaded from env/node_a/route.json and env/node_a/scenarios.json.

struct RoutePoint {
    double lat = 0, lon = 0;
    std::string name;           // non-empty => this point is a bus stop

    RoutePoint(double lt, double ln, const std::string& label): lat(lt), lon(ln), name(label) {}
    RoutePoint() = default;
};

struct Route {
    uint16_t routeId = 0;
    std::string name;
    std::vector<RoutePoint> points;
};

struct DistanceRange {
    float fromM = 0, toM = 0;   // metres along the route

    DistanceRange(float f, float t): fromM(f), toM(t) {}
};

struct SpeedZone {
    float fromM = 0, toM = 0, kmh = 0;

    SpeedZone(float f, float t, float kmh): fromM(f), toM(t), kmh(kmh) {}
};

struct Scenario {
    std::string name = "default";
    std::string description;
    float cruiseKmh = 30;
    float accelMps2 = 1.0f;
    float decelMps2 = 1.3f;
    float dwellS = 15;
    float timeScale = 1;        // 10 = ten times faster than real time
    float gpsJitterM = 0;       // random position noise amplitude
    bool loop = true;           // closed route: keep lapping; open route: jump back to the start
    bool autostart = true;
    int startStop = 0;
    std::vector<SpeedZone> slowZones;
    std::vector<DistanceRange> gpsDropouts;  // fix_ok = false inside (tunnels, urban canyons)
    std::vector<std::string> skipStops;      // stop names the bus drives through
};

struct ScenarioSet {
    std::string defaultName;
    std::vector<Scenario> scenarios;

    int indexOf(const std::string &name) const;
};

/// JSON: { "route_id": 7, "name": "E2", "points": [ {"lat":..,"lon":..,"name":"Stop"}, ... ] }
bool parseRouteJson(const std::string &json, Route &out, std::string *err = nullptr);

/// JSON: { "default": "normal", "defaults": {..scenario keys..}, "scenarios": [ {..}, .. ] }
/// Every scenario starts from "defaults" and overrides what it lists.
bool parseScenariosJson(const std::string &json, ScenarioSet &out, std::string *err = nullptr);
