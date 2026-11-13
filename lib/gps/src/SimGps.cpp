#ifdef USE_SIM_GPS
#include "SimGps.h"
#include <Arduino.h>
#include "DeviceConfig.h"

static const char *kFallbackRoute = R"({"route_id":0,"name":"fallback","points":[
  {"lat":45.7500,"lon":21.2200,"name":"A"},{"lat":45.7500,"lon":21.2260,"name":"B"},
  {"lat":45.7530,"lon":21.2260,"name":"C"},{"lat":45.7500,"lon":21.2200,"name":"A"}]})";
static const char *kFallbackScenarios = R"({"scenarios":[{"name":"fallback","dwell_s":10}]})";

bool SimGps::begin() {
    return reload();
}

bool SimGps::reload() {
    Route route;
    ScenarioSet set;
    std::string text, err;
    fallback_ = false;

    if(!DeviceConfig::readFile(kRouteJsonPath, text) || !parseRouteJson(text, route, &err)) {
        Serial.printf("[sim] %s unusable (%s), using built-in route\n", kRouteJsonPath,
                      err.empty() ? "missing" : err.c_str());
        parseRouteJson(kFallbackRoute, route);
        fallback_ = true;
    }
    err.clear();
    if(!DeviceConfig::readFile(kScenariosJsonPath, text) || !parseScenariosJson(text, set, &err)) {
        Serial.printf("[sim] %s unusable (%s), using built-in scenario\n", kScenariosJsonPath,
                      err.empty() ? "missing" : err.c_str());
        parseScenariosJson(kFallbackScenarios, set);
        fallback_ = true;
    }

    if(!engine_.load(route, set))
        return false;
    if(!initial_.empty() && !engine_.setScenario(initial_))
        Serial.printf("[sim] scenario '%s' not found, using '%s'\n", initial_.c_str(), engine_.scenarioName());

    Serial.printf("[sim] route '%s' %.0f m, %u stops, %u scenarios, active '%s'\n", route.name.c_str(),
                  engine_.totalM(), (unsigned)engine_.stopCount(), (unsigned)engine_.scenarioCount(),
                  engine_.scenarioName());
    return true;
}

GpsFix SimGps::fix() const {
    SimFix s = engine_.fix();
    GpsFix f;
    f.lat = s.lat;
    f.lon = s.lon;
    f.speed_kmh = s.speedKmh;
    f.heading_deg = s.headingDeg;
    f.fix_ok = s.fixOk;
    f.sats = s.fixOk ? 9 : 0;
    f.stop_index = s.stopIndex;
    f.last_stop_index = s.lastStopIndex;
    f.ts_ms = millis();
    return f;
}

#endif // USE_SIM_GPS
