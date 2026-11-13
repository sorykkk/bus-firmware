// Unit tests for the simulated GPS engine: kinematics, stops, slow zones, dropouts, jitter.
#include <math.h>
#include <stdio.h>
#include <unity.h>
#include "Geo.h"
#include "SimEngine.h"

static const char *kRoute = R"({
  "route_id": 7, "name": "test",
  "points": [
    {"lat": 45.0000, "lon": 21.0000, "name": "A"},
    {"lat": 45.0090, "lon": 21.0000},
    {"lat": 45.0180, "lon": 21.0000, "name": "B"},
    {"lat": 45.0090, "lon": 21.0000, "name": "C"},
    {"lat": 45.0000, "lon": 21.0000, "name": "A"}
  ]
})";

static Route gRoute;
static Scenario gBase;
static uint32_t gNow = 1000;

static void load(SimEngine &e, const char *json) {
    ScenarioSet set;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(parseScenariosJson(json, set, &err), err.c_str());
    TEST_ASSERT_TRUE(e.load(gRoute, set));
}

/// One-line engine snapshot for assertion messages.
static const char *describe(const SimEngine &e) {
    static char buf[160];
    SimFix f = e.fix();
    snprintf(buf, sizeof buf, "state=%s pos=%.1f/%.1f stop=%d last=%d v=%.1f", e.stateName(), e.progressM(),
             e.totalM(), f.stopIndex, f.lastStopIndex, f.speedKmh);
    return buf;
}

/// Advances simulated time in wall-clock slices. The clock carries across calls:
/// restarting it would wrap uint32 time and the engine would swallow the gap as 120 s.
static void run(SimEngine &e, float seconds, float step = 1.0f) {
    e.update(gNow);                            // primes the clock on the first call
    for(float s = 0; s < seconds; s += step)
        e.update(gNow += (uint32_t)(step * 1000));
}

void test_route_geometry() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"autostart\":false}]}");
    // 0.009 deg latitude ~ 1001 m, four segments, route closes on itself
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 4004.0f, e.totalM());
    TEST_ASSERT_EQUAL(3, e.stopCount());       // closing "A" is the same stop as the first
    TEST_ASSERT_EQUAL_STRING("idle", e.stateName());
    TEST_ASSERT_EQUAL_STRING("A", e.stopName(e.fix().stopIndex));
}

void test_drives_and_stops() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"dwell_s\":10,\"accel_mps2\":2,\"decel_mps2\":2}]}");
    TEST_ASSERT_EQUAL_STRING("at_stop", e.stateName());

    run(e, 30);
    TEST_ASSERT_EQUAL_STRING("driving", e.stateName());
    TEST_ASSERT_EQUAL(-1, e.fix().stopIndex);
    TEST_ASSERT_TRUE(e.fix().speedKmh > 20.0f);
    TEST_ASSERT_TRUE(e.fix().speedKmh <= 36.0f + 0.1f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, e.fix().headingDeg);   // route goes due north

    run(e, 400);
    // 430 s is past the dwell at B, so the bus is moving again. lastStopIndex
    // stays on the last stop visited; A -> B is ~2002 m at 36 km/h.
    TEST_ASSERT_TRUE_MESSAGE(e.fix().lastStopIndex >= 1, describe(e));
}

void test_slow_zone_caps_speed() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":72,\"accel_mps2\":5,\"decel_mps2\":5,"
            "\"slow_zones\":[{\"from_m\":300,\"to_m\":900,\"kmh\":18}]}]}");
    run(e, 60);
    float pos = e.progressM();
    TEST_ASSERT_TRUE(pos > 300.0f && pos < 900.0f);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 18.0f, e.fix().speedKmh);
}

void test_skipped_stop() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":72,\"accel_mps2\":4,\"decel_mps2\":4,"
            "\"dwell_s\":5,\"skip_stops\":[\"B\"]}]}");
    bool passedB = false;
    e.update(gNow);
    for(int i = 0; i < 300 && e.fix().stopIndex != 2; i++) {
        e.update(gNow += 1000);
        if(e.progressM() > 1900.0f && e.progressM() < 2100.0f && e.stateName() == std::string("driving"))
            passedB = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(passedB, "bus should drive through skipped stop B");
    TEST_ASSERT_EQUAL_MESSAGE(2, e.fix().stopIndex, describe(e));   // it does stop at C
    TEST_ASSERT_EQUAL_STRING("C", e.stopName(2));
}

void test_gps_dropout_keeps_last_fix() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"accel_mps2\":4,\"decel_mps2\":4,"
            "\"gps_dropouts\":[{\"from_m\":600,\"to_m\":1200}]}]}");
    run(e, 40);
    TEST_ASSERT_TRUE(e.fix().fixOk);
    double latBefore = e.fix().lat;

    run(e, 60);
    TEST_ASSERT_FALSE(e.fix().fixOk);
    // frozen at the last good fix, i.e. just before the dropout starts at 600 m
    double frozenLat = e.fix().lat, frozenLon = e.fix().lon;
    TEST_ASSERT_TRUE(frozenLat > latBefore);
    TEST_ASSERT_FLOAT_WITHIN(10.0, 595.0, geo::distanceM(45.0, 21.0, frozenLat, frozenLon));

    run(e, 10);
    TEST_ASSERT_FALSE(e.fix().fixOk);
    TEST_ASSERT_FLOAT_WITHIN(1e-9, frozenLat, e.fix().lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-9, frozenLon, e.fix().lon);

    run(e, 120);
    TEST_ASSERT_TRUE(e.fix().fixOk);
    TEST_ASSERT_TRUE(e.fix().lat > frozenLat);
}

void test_jitter_stays_bounded() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"autostart\":false,\"gps_jitter_m\":12}]}");
    double baseLat = e.fix().lat, baseLon = e.fix().lon;
    e.update(gNow);
    double maxD = 0;
    for(int i = 0; i < 30; i++) {
        e.update(gNow += 1000);
        maxD = fmax(maxD, geo::distanceM(baseLat, baseLon, e.fix().lat, e.fix().lon));
    }
    TEST_ASSERT_TRUE(maxD > 1.0);       // noise is actually applied
    TEST_ASSERT_TRUE(maxD < 20.0);      // but stays within the configured amplitude
}

void test_pause_and_time_scale() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"accel_mps2\":4,\"decel_mps2\":4}]}");
    run(e, 20);
    e.pause();
    TEST_ASSERT_EQUAL_STRING("paused", e.stateName());
    float pos = e.progressM();
    run(e, 30);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pos, e.progressM());
    e.start();
    TEST_ASSERT_EQUAL_STRING("driving", e.stateName());

    e.setTimeScale(10);
    float p0 = e.progressM();
    run(e, 5);
    TEST_ASSERT_TRUE(e.progressM() - p0 > 100.0f);   // 5 wall seconds * 10 ~= 50 simulated seconds
}

void test_one_lap_finishes() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":72,\"accel_mps2\":5,\"decel_mps2\":5,"
            "\"dwell_s\":1,\"loop\":false,\"start_stop\":0}]}");
    run(e, 400, 2.0f);
    TEST_ASSERT_EQUAL_STRING("finished", e.stateName());
    TEST_ASSERT_EQUAL(0, e.fix().stopIndex);    // parked back at A
    float pos = e.progressM();
    run(e, 20);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pos, e.progressM());
}

void test_scenario_switch_and_manual_overrides() {
    SimEngine e;
    load(e, "{\"default\":\"a\",\"scenarios\":[{\"name\":\"a\",\"cruise_kmh\":30},{\"name\":\"b\",\"cruise_kmh\":50}]}");
    TEST_ASSERT_EQUAL_STRING("a", e.scenarioName());
    e.nextScenario();
    TEST_ASSERT_EQUAL_STRING("b", e.scenarioName());
    e.nextScenario();                           // wraps back to the first
    TEST_ASSERT_EQUAL_STRING("a", e.scenarioName());
    TEST_ASSERT_TRUE(e.setScenario("b"));
    TEST_ASSERT_FALSE(e.setScenario("nope"));
    TEST_ASSERT_FALSE(e.setScenario((size_t)5));
    TEST_ASSERT_EQUAL_STRING("", e.scenarioNameAt(9));

    e.setCruiseKmh(40);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, e.cruiseKmh());
    e.setCruiseKmh(0);                          // rejected
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, e.cruiseKmh());
    e.setCruiseKmh(120);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, e.cruiseKmh());
    e.setCruiseKmh(121);                        // rejected (cap 120)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, e.cruiseKmh());
    e.setTimeScale(1000);                       // rejected (cap 100)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, e.timeScale());
    e.setTimeScale(100);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, e.timeScale());
}

void test_geo_distance_bearing_and_offset() {
    TEST_ASSERT_FLOAT_WITHIN(1.0, 0.0, (float)geo::distanceM(45.0, 21.0, 45.0, 21.0));
    // 1 degree of latitude on a 6371 km sphere is 111195 m.
    TEST_ASSERT_FLOAT_WITHIN(50.0f, 111195.0f, (float)geo::distanceM(45.0, 21.0, 46.0, 21.0));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, (float)geo::bearingDeg(45.0, 21.0, 46.0, 21.0));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 180.0f, (float)geo::bearingDeg(46.0, 21.0, 45.0, 21.0));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 90.0f, (float)geo::bearingDeg(45.0, 21.0, 45.0, 21.01));

    double lat = 45.0, lon = 21.0;
    geo::offsetM(lat, lon, 1000.0, 0.0);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 1000.0f, (float)geo::distanceM(45.0, 21.0, lat, lon));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, (float)geo::bearingDeg(45.0, 21.0, lat, lon));

    lat = 45.0;
    lon = 21.0;
    geo::offsetM(lat, lon, 0.0, 1000.0);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 1000.0f, (float)geo::distanceM(45.0, 21.0, lat, lon));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 90.0f, (float)geo::bearingDeg(45.0, 21.0, lat, lon));
}

void test_load_rejects_unusable_input() {
    SimEngine e;
    ScenarioSet empty;
    TEST_ASSERT_FALSE(e.load(gRoute, empty));

    Route one;
    one.points.push_back(RoutePoint{45.0, 21.0, "A"});
    ScenarioSet set;
    Scenario s;
    s.name = "n";
    set.scenarios.push_back(s);
    TEST_ASSERT_FALSE(e.load(one, set));
}

void test_dwell_duration_and_braking() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"dwell_s\":10,\"accel_mps2\":4,\"decel_mps2\":2}]}");
    run(e, 9);
    TEST_ASSERT_EQUAL_STRING("at_stop", e.stateName());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, e.fix().speedKmh);

    run(e, 2);
    TEST_ASSERT_EQUAL_STRING("driving", e.stateName());

    // Cruise is 10 m/s and braking is 2 m/s^2, so speed must already be well
    // under cruise in the last ~15 m before stop B (~2002 m).
    bool braking = false;
    // Stop B is ~2002 m away. At 36 km/h this takes just over 200 s after 
    // the initial dwell; 800 x 200 ms only covers 160 s and never reaches it.
    for(int i = 0; i < 1200 && e.fix().lastStopIndex < 1; i++) {
        e.update(gNow += 200);
        if(e.progressM() > 1988.0f && e.progressM() < 2000.0f && e.fix().speedKmh < 25.0f && e.fix().speedKmh > 0.3f)
            braking = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(braking, describe(e));
}

void test_controls_reset_toggle_and_restart() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"autostart\":false,\"dwell_s\":30}]}");
    TEST_ASSERT_EQUAL_STRING("idle", e.stateName());
    e.pause();                                  // ignored: nothing is in progress
    TEST_ASSERT_EQUAL_STRING("idle", e.stateName());
    e.toggle();                                 // BOOT short-press: idle -> dwelling
    TEST_ASSERT_EQUAL_STRING("at_stop", e.stateName());
    e.toggle();
    TEST_ASSERT_EQUAL_STRING("paused", e.stateName());
    float held = e.progressM();
    run(e, 5);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, held, e.progressM());
    e.toggle();                                 // paused -> resume dwelling
    TEST_ASSERT_EQUAL_STRING("at_stop", e.stateName());

    run(e, 40);
    TEST_ASSERT_EQUAL_STRING("driving", e.stateName());
    TEST_ASSERT_TRUE(e.progressM() > 10.0f);
    e.reset();
    TEST_ASSERT_EQUAL_STRING("idle", e.stateName());
    TEST_ASSERT_EQUAL(0, e.fix().stopIndex);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, e.progressM());

    SimEngine lap;
    load(lap, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":72,\"accel_mps2\":5,\"decel_mps2\":5,"
              "\"dwell_s\":1,\"loop\":false}]}");
    run(lap, 400, 2.0f);
    TEST_ASSERT_EQUAL_STRING("finished", lap.stateName());
    lap.start();                                // console `start` after a one-lap parks and goes again
    TEST_ASSERT_EQUAL_STRING("at_stop", lap.stateName());
    run(lap, 8);
    TEST_ASSERT_EQUAL_STRING("driving", lap.stateName());
    TEST_ASSERT_TRUE(lap.progressM() > 10.0f);
}

void test_start_stop_and_open_route() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"autostart\":false,\"start_stop\":2}]}");
    TEST_ASSERT_EQUAL(2, e.fix().stopIndex);
    TEST_ASSERT_EQUAL_STRING("C", e.stopName(e.fix().stopIndex));
    TEST_ASSERT_FLOAT_WITHIN(30.0f, 3003.0f, e.progressM());

    load(e, "{\"scenarios\":[{\"name\":\"n\",\"autostart\":false,\"start_stop\":99}]}");
    TEST_ASSERT_EQUAL(2, e.fix().stopIndex);    // past the end clamps to the last stop

    Route open;
    std::string err;
    const char *openJson = R"({
      "points": [
        {"lat": 45.0000, "lon": 21.0000, "name": "A"},
        {"lat": 45.0010, "lon": 21.0000, "name": "B"}
      ]
    })";
    TEST_ASSERT_TRUE_MESSAGE(parseRouteJson(openJson, open, &err), err.c_str());

    ScenarioSet onceSet;
    TEST_ASSERT_TRUE(parseScenariosJson(
        "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"accel_mps2\":4,\"decel_mps2\":4,\"dwell_s\":1,\"loop\":false}]}",
        onceSet, &err));
    SimEngine once;
    TEST_ASSERT_TRUE(once.load(open, onceSet));
    run(once, 40);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("finished", once.stateName(), describe(once));
    TEST_ASSERT_EQUAL(1, once.fix().lastStopIndex);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, once.totalM(), once.progressM());
    float parked = once.progressM();
    run(once, 5);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, parked, once.progressM());

    ScenarioSet loopSet;
    TEST_ASSERT_TRUE(parseScenariosJson(
        "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"accel_mps2\":4,\"decel_mps2\":4,\"dwell_s\":1,\"loop\":true}]}",
        loopSet, &err));
    SimEngine looped;
    TEST_ASSERT_TRUE(looped.load(open, loopSet));
    bool sawB = false, wrapped = false;
    float maxPos = 0;
    looped.update(gNow);
    for(int i = 0; i < 80 && !wrapped; i++) {
        looped.update(gNow += 1000);
        maxPos = fmax(maxPos, looped.progressM());
        if(looped.fix().stopIndex == 1 || looped.fix().lastStopIndex == 1)
            sawB = true;
        if(sawB && looped.progressM() < 40.0f && maxPos > 80.0f)
            wrapped = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawB, describe(looped));
    TEST_ASSERT_TRUE_MESSAGE(wrapped, describe(looped));
    TEST_ASSERT_TRUE(std::string(looped.stateName()) != "finished");
}

void test_heading_follows_the_route() {
    Route route;
    std::string err;
    TEST_ASSERT_TRUE(parseRouteJson(R"({
      "points": [
        {"lat": 45.0000, "lon": 21.0000, "name": "A"},
        {"lat": 45.0090, "lon": 21.0000},
        {"lat": 45.0090, "lon": 21.0120, "name": "B"}
      ]
    })", route, &err));
    ScenarioSet set;
    TEST_ASSERT_TRUE(parseScenariosJson(
        "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":72,\"accel_mps2\":5,\"decel_mps2\":5,\"dwell_s\":1}]}",
        set, &err));
    SimEngine e;
    TEST_ASSERT_TRUE(e.load(route, set));
    run(e, 8);
    TEST_ASSERT_TRUE(e.progressM() > 50.0f && e.progressM() < 900.0f);
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 0.0f, e.fix().headingDeg);

    run(e, 80);
    TEST_ASSERT_TRUE_MESSAGE(e.progressM() > 1100.0f, describe(e));
    TEST_ASSERT_FLOAT_WITHIN(2.0f, 90.0f, e.fix().headingDeg);
}

void test_stricter_slow_zone_wins() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":72,\"accel_mps2\":8,\"decel_mps2\":8,\"dwell_s\":1,"
            "\"slow_zones\":[{\"from_m\":0,\"to_m\":800,\"kmh\":40},{\"from_m\":0,\"to_m\":800,\"kmh\":18}]}]}");
    run(e, 20);
    TEST_ASSERT_TRUE_MESSAGE(e.progressM() > 40.0f && e.progressM() < 800.0f, describe(e));
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 18.0f, e.fix().speedKmh);
}

void test_time_gap_is_clamped_and_clock_wraps() {
    SimEngine e;
    load(e, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"accel_mps2\":20,\"decel_mps2\":20,\"dwell_s\":0}]}");
    run(e, 5);
    float before = e.progressM();
    TEST_ASSERT_TRUE(before > 20.0f && before < 80.0f);
    e.update(gNow += 1000000);                  // 1000 s must count as at most 120 s
    float moved = e.progressM() - before;
    TEST_ASSERT_TRUE_MESSAGE(moved > 900.0f && moved < 1400.0f, describe(e));

    SimEngine wrapped;
    load(wrapped, "{\"scenarios\":[{\"name\":\"n\",\"cruise_kmh\":36,\"accel_mps2\":20,\"decel_mps2\":20,\"dwell_s\":0}]}");
    wrapped.update(0xFFFFFFF0u);                // prime just before the uint32 clock rolls over
    wrapped.update(0x00000BB8u);                // 3000 ms later, across the wrap
    TEST_ASSERT_EQUAL_STRING("driving", wrapped.stateName());
    TEST_ASSERT_TRUE_MESSAGE(wrapped.progressM() > 15.0f && wrapped.progressM() < 50.0f, describe(wrapped));
}

void test_real_config_files() {
    // The same files that get flashed to the board must load and produce a moving bus.
    FILE *rf = fopen("env/node_a/route.json", "r");
    FILE *sf = fopen("env/node_a/scenarios.json", "r");
    if(!rf || !sf) {
        TEST_IGNORE_MESSAGE("run from the project root to exercise env/*.json");
    }
    std::string rj, sj;
    char buf[1024];
    size_t n;
    while((n = fread(buf, 1, sizeof buf, rf))) rj.append(buf, n);
    while((n = fread(buf, 1, sizeof buf, sf))) sj.append(buf, n);
    fclose(rf);
    fclose(sf);

    Route route;
    ScenarioSet set;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(parseRouteJson(rj, route, &err), err.c_str());
    TEST_ASSERT_TRUE_MESSAGE(parseScenariosJson(sj, set, &err), err.c_str());

    SimEngine e;
    TEST_ASSERT_TRUE(e.load(route, set));
    TEST_ASSERT_EQUAL_STRING("normal", e.scenarioName());
    TEST_ASSERT_TRUE(e.totalM() > 1000.0f);
    TEST_ASSERT_TRUE(set.scenarios.size() >= 7);
    for(size_t i = 0; i < set.scenarios.size(); i++) {
        TEST_ASSERT_TRUE(e.setScenario(set.scenarios[i].name));
        TEST_ASSERT_EQUAL_STRING(set.scenarios[i].name.c_str(), e.scenarioName());
    }
    TEST_ASSERT_TRUE(e.setScenario("tunnel"));
    TEST_ASSERT_TRUE(e.setScenario("one_lap"));
    TEST_ASSERT_EQUAL_STRING("idle", e.stateName());   // autostart = false
    e.start();
    run(e, 5);
    TEST_ASSERT_TRUE(e.fix().fixOk);
}

void setUp() { gNow = 1000; }
void tearDown() {}

int main() {
    std::string err;
    if(!parseRouteJson(kRoute, gRoute, &err))
        return 1;
    UNITY_BEGIN();
    RUN_TEST(test_route_geometry);
    RUN_TEST(test_drives_and_stops);
    RUN_TEST(test_slow_zone_caps_speed);
    RUN_TEST(test_skipped_stop);
    RUN_TEST(test_gps_dropout_keeps_last_fix);
    RUN_TEST(test_jitter_stays_bounded);
    RUN_TEST(test_pause_and_time_scale);
    RUN_TEST(test_one_lap_finishes);
    RUN_TEST(test_scenario_switch_and_manual_overrides);
    RUN_TEST(test_geo_distance_bearing_and_offset);
    RUN_TEST(test_load_rejects_unusable_input);
    RUN_TEST(test_dwell_duration_and_braking);
    RUN_TEST(test_controls_reset_toggle_and_restart);
    RUN_TEST(test_start_stop_and_open_route);
    RUN_TEST(test_heading_follows_the_route);
    RUN_TEST(test_stricter_slow_zone_wins);
    RUN_TEST(test_time_gap_is_clamped_and_clock_wraps);
    RUN_TEST(test_real_config_files);
    return UNITY_END();
}

#ifndef ARDUINO
void setup() {}
void loop() {}
#endif
