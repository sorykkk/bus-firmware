#include "SimScenario.h"
#include <ArduinoJson.h>

static bool fail(std::string *err, const std::string &msg) {
    if(err)
        *err = msg;
    return false;
}

int ScenarioSet::indexOf(const std::string &name) const {
    for(size_t i = 0; i < scenarios.size(); i++)
        if(scenarios[i].name == name)
            return (int)i;
    return -1;
}

bool parseRouteJson(const std::string &json, Route &out, std::string *err) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    if(e)
        return fail(err, std::string("route json: ") + e.c_str());

    Route r;
    r.routeId = doc["route_id"] | 0;
    r.name = (const char *)(doc["name"] | "");
    JsonArrayConst pts = doc["points"].is<JsonArrayConst>() ? doc["points"].as<JsonArrayConst>()
                                                             : doc["route"].as<JsonArrayConst>();
    for(JsonObjectConst p : pts) {
        if(!p["lat"].is<double>() || !p["lon"].is<double>())
            return fail(err, "route json: every point needs numeric lat and lon");
        RoutePoint rp;
        rp.lat = p["lat"].as<double>();
        rp.lon = p["lon"].as<double>();
        rp.name = (const char *)(p["name"] | "");
        r.points.push_back(rp);
    }
    if(r.points.size() < 2)
        return fail(err, "route json: needs at least 2 points");
    out = r;
    return true;
}

static void readRanges(JsonArrayConst arr, std::vector<DistanceRange> &out) {
    out.clear();
    for(JsonObjectConst o : arr)
        out.push_back(DistanceRange{o["from_m"] | 0.0f, o["to_m"] | 0.0f});
}

static void applyScenario(JsonObjectConst o, Scenario &s) {
    if(o.isNull())
        return;
    s.name        = (const char *)(o["name"] | s.name.c_str());
    s.description = (const char *)(o["description"] | s.description.c_str());
    s.cruiseKmh   = o["cruise_kmh"]   | s.cruiseKmh;
    s.accelMps2   = o["accel_mps2"]   | s.accelMps2;
    s.decelMps2   = o["decel_mps2"]   | s.decelMps2;
    s.dwellS      = o["dwell_s"]      | s.dwellS;
    s.timeScale   = o["time_scale"]   | s.timeScale;
    s.gpsJitterM  = o["gps_jitter_m"] | s.gpsJitterM;
    s.loop        = o["loop"]         | s.loop;
    s.autostart   = o["autostart"]    | s.autostart;
    s.startStop   = o["start_stop"]   | s.startStop;

    if(o["slow_zones"].is<JsonArrayConst>()) {
        s.slowZones.clear();
        for(JsonObjectConst z : o["slow_zones"].as<JsonArrayConst>())
            s.slowZones.push_back(SpeedZone{z["from_m"] | 0.0f, z["to_m"] | 0.0f, z["kmh"] | 5.0f});
    }
    if(o["gps_dropouts"].is<JsonArrayConst>())
        readRanges(o["gps_dropouts"].as<JsonArrayConst>(), s.gpsDropouts);
    if(o["skip_stops"].is<JsonArrayConst>()) {
        s.skipStops.clear();
        for(const char *n : o["skip_stops"].as<JsonArrayConst>())
            if(n)
                s.skipStops.push_back(n);
    }
}

static bool validate(const Scenario &s, std::string *err) {
    if(s.name.empty())
        return fail(err, "scenario without a name");
    if(s.cruiseKmh <= 0 || s.accelMps2 <= 0 || s.decelMps2 <= 0 || s.timeScale <= 0 || s.dwellS < 0)
        return fail(err, "scenario '" + s.name + "': speeds, accelerations and time_scale must be > 0");
    return true;
}

bool parseScenariosJson(const std::string &json, ScenarioSet &out, std::string *err) {
    JsonDocument doc;
    DeserializationError e = deserializeJson(doc, json);
    if(e)
        return fail(err, std::string("scenarios json: ") + e.c_str());

    Scenario base;
    applyScenario(doc["defaults"].as<JsonObjectConst>(), base);

    ScenarioSet set;
    for(JsonObjectConst o : doc["scenarios"].as<JsonArrayConst>()) {
        Scenario s = base;
        s.name.clear();
        applyScenario(o, s);
        if(!validate(s, err))
            return false;
        if(set.indexOf(s.name) >= 0)
            return fail(err, "duplicate scenario name '" + s.name + "'");
        set.scenarios.push_back(s);
    }
    if(set.scenarios.empty())
        return fail(err, "scenarios json: \"scenarios\" array is empty");

    set.defaultName = (const char *)(doc["default"] | set.scenarios[0].name.c_str());
    if(set.indexOf(set.defaultName) < 0)
        return fail(err, "default scenario '" + set.defaultName + "' does not exist");
    out = set;
    return true;
}
