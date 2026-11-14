// Unit tests for the .ini / .env / JSON parsing and the BLE frame layout.
// PC only: `pio test -e native`. The board targets ignore logic_* .
#include <string>
#include <unity.h>
#include "BeaconFrame.h"
#include "DotEnv.h"
#include "IniFile.h"
#include "SimScenario.h"

static const char *kIni = R"(
; header comment
[identity]
route_id = 7
BUS_ID = 101          ; inline comment, uppercase key

[ble]
system_uuid = d22b117c-2f6e-4d74-9e9f-6f09213c3dd0
tx_power_1m = -59
radio_power_dbm = 0x3

[tuning]
gain = 2.7
label = "6.5"
autostart = yes

[bad
oops
)";

void test_ini_sections_keys_comments() {
    IniFile ini;
    TEST_ASSERT_FALSE(ini.parse(kIni));          // one malformed line is reported...
    TEST_ASSERT_EQUAL(2, ini.errorCount());
    TEST_ASSERT_EQUAL(7, ini.getInt("identity", "route_id", 0));
    TEST_ASSERT_EQUAL(101, ini.getInt("identity", "bus_id", 0));   // case-insensitive
    TEST_ASSERT_EQUAL_STRING("d22b117c-2f6e-4d74-9e9f-6f09213c3dd0",
                             ini.get("BLE", "System_UUID").c_str());
    TEST_ASSERT_EQUAL(-59, ini.getInt("ble", "tx_power_1m", 0));
    TEST_ASSERT_EQUAL(3, ini.getInt("ble", "radio_power_dbm", 0));  // hex
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.7f, ini.getFloat("tuning", "gain", 0));
    TEST_ASSERT_EQUAL_STRING("6.5", ini.get("tuning", "label").c_str());
    TEST_ASSERT_TRUE(ini.getBool("tuning", "autostart", false));
    TEST_ASSERT_EQUAL(42, ini.getInt("missing", "key", 42));
}

static const char *kEnv = R"(
# comment
WIFI_SSID="bus-hotspot"
WIFI_PASS='p a s s #1'
export SERVER_BASE_URL=http://10.0.0.5:8000   # trailing comment
DEVICE_HMAC_SECRET="line1\nline2"
bad line
)";

void test_dotenv() {
    DotEnv env;
    TEST_ASSERT_FALSE(env.parse(kEnv));
    TEST_ASSERT_EQUAL(1, env.errorCount());
    TEST_ASSERT_EQUAL_STRING("bus-hotspot", env.get("WIFI_SSID").c_str());
    TEST_ASSERT_EQUAL_STRING("p a s s #1", env.get("WIFI_PASS").c_str());   // single quotes: literal
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8000", env.get("SERVER_BASE_URL").c_str());
    TEST_ASSERT_EQUAL_STRING("line1\nline2", env.get("DEVICE_HMAC_SECRET").c_str());
    TEST_ASSERT_FALSE(env.has("MISSING"));
    TEST_ASSERT_EQUAL_STRING("fallback", env.get("MISSING", "fallback").c_str());
}

static const char *kRoute = R"({
  "route_id": 7, "name": "E2",
  "points": [
    {"lat": 45.75, "lon": 21.22, "name": "Central"},
    {"lat": 45.75, "lon": 21.226},
    {"lat": 45.753, "lon": 21.229, "name": "Market"}
  ]
})";

void test_route_json() {
    Route r;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(parseRouteJson(kRoute, r, &err), err.c_str());
    TEST_ASSERT_EQUAL(7, r.routeId);
    TEST_ASSERT_EQUAL(3, r.points.size());
    TEST_ASSERT_EQUAL_STRING("Market", r.points[2].name.c_str());
    TEST_ASSERT_TRUE(r.points[1].name.empty());

    TEST_ASSERT_FALSE(parseRouteJson("{\"points\":[{\"lat\":1,\"lon\":2}]}", r, &err));  // < 2 points
    TEST_ASSERT_FALSE(err.empty());
    TEST_ASSERT_FALSE(parseRouteJson("{ broken", r, &err));
}

static const char *kScenarios = R"({
  "default": "rush",
  "defaults": {"cruise_kmh": 30, "dwell_s": 15, "loop": true},
  "scenarios": [
    {"name": "normal"},
    {"name": "rush", "dwell_s": 40, "slow_zones": [{"from_m": 100, "to_m": 400, "kmh": 6}],
     "skip_stops": ["Hospital"], "gps_dropouts": [{"from_m": 10, "to_m": 20}]}
  ]
})";

void test_scenarios_json() {
    ScenarioSet set;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(parseScenariosJson(kScenarios, set, &err), err.c_str());
    TEST_ASSERT_EQUAL_STRING("rush", set.defaultName.c_str());
    TEST_ASSERT_EQUAL(1, set.indexOf("rush"));

    const Scenario &n = set.scenarios[0];
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, n.cruiseKmh);   // inherited from defaults
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, n.dwellS);

    const Scenario &r = set.scenarios[1];
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, r.dwellS);      // overridden
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, r.cruiseKmh);   // still inherited
    TEST_ASSERT_EQUAL(1, r.slowZones.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.0f, r.slowZones[0].kmh);
    TEST_ASSERT_EQUAL(1, r.gpsDropouts.size());
    TEST_ASSERT_EQUAL_STRING("Hospital", r.skipStops[0].c_str());

    TEST_ASSERT_FALSE(parseScenariosJson("{\"default\":\"nope\",\"scenarios\":[{\"name\":\"x\"}]}", set, &err));
    TEST_ASSERT_FALSE(parseScenariosJson("{\"scenarios\":[]}", set, &err));
    TEST_ASSERT_FALSE(parseScenariosJson("{\"scenarios\":[{\"name\":\"a\"},{\"name\":\"a\"}]}", set, &err));
    TEST_ASSERT_FALSE(parseScenariosJson("{\"scenarios\":[{\"name\":\"a\",\"cruise_kmh\":0}]}", set, &err));
    TEST_ASSERT_FALSE(parseScenariosJson("{\"scenarios\":[{\"cruise_kmh\":10}]}", set, &err));

    TEST_ASSERT_TRUE_MESSAGE(
        parseScenariosJson("{\"defaults\":{\"accel_mps2\":2.5,\"decel_mps2\":3},\"scenarios\":[{\"name\":\"a\"}]}",
                           set, &err),
        err.c_str());
    TEST_ASSERT_EQUAL_STRING("a", set.defaultName.c_str());   // omitted "default" uses the first scenario
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, set.scenarios[0].accelMps2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, set.scenarios[0].decelMps2);
}

void test_ini_bools_duplicates_and_crlf() {
    IniFile ini;
    const char *text = "[camera]\r\nvflip = no\r\nhmirror = OFF\r\nflash = 0\r\n"
                       "again = true\r\nagain = false\r\njpeg = 12abc\r\n";
    TEST_ASSERT_TRUE(ini.parse(text));
    TEST_ASSERT_FALSE(ini.getBool("camera", "vflip", true));
    TEST_ASSERT_FALSE(ini.getBool("camera", "hmirror", true));
    TEST_ASSERT_FALSE(ini.getBool("camera", "flash", true));
    TEST_ASSERT_FALSE(ini.getBool("camera", "again", true));          // later line wins
    TEST_ASSERT_TRUE(ini.getBool("camera", "missing", true));         // unknown stays at the default
    TEST_ASSERT_EQUAL(9, ini.getInt("camera", "jpeg", 9));            // "12abc" is not an integer
    TEST_ASSERT_EQUAL(9, ini.getInt("camera", "missing", 9));
}

void test_dotenv_duplicate_and_crlf() {
    DotEnv env;
    TEST_ASSERT_TRUE(env.parse("WIFI_SSID=first\r\nWIFI_SSID=\"second\"\r\n# note\r\n"));
    TEST_ASSERT_EQUAL_STRING("second", env.get("WIFI_SSID").c_str());
}

void test_route_alias_and_rejections() {
    Route r;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(
        parseRouteJson("{\"route_id\":3,\"name\":\"X\",\"route\":[{\"lat\":1,\"lon\":2},{\"lat\":1.01,\"lon\":2,\"name\":\"Z\"}]}",
                       r, &err),
        err.c_str());
    TEST_ASSERT_EQUAL(3, r.routeId);
    TEST_ASSERT_EQUAL_STRING("X", r.name.c_str());
    TEST_ASSERT_EQUAL_STRING("Z", r.points[1].name.c_str());

    TEST_ASSERT_FALSE(parseRouteJson("{\"points\":[{\"lat\":1,\"lon\":2},{\"lon\":4}]}", r, &err));
    TEST_ASSERT_FALSE(err.empty());
}

void test_beacon_frame() {
    uint8_t uuid[16];
    TEST_ASSERT_TRUE(beaconframe::parseUuid("D22B117C-2f6e-4d74-9e9f-6f09213c3dd0", uuid));
    TEST_ASSERT_EQUAL_HEX8(0xd2, uuid[0]);
    TEST_ASSERT_EQUAL_HEX8(0xd0, uuid[15]);
    TEST_ASSERT_FALSE(beaconframe::parseUuid("zz", uuid));
    TEST_ASSERT_FALSE(beaconframe::parseUuid("d22b117c2f6e4d749e9f6f09213c3d", uuid));   // too short

    uint8_t d[beaconframe::kIBeaconLen];
    beaconframe::buildIBeacon(uuid, 0x0007, 0x0065, -59, d);
    TEST_ASSERT_EQUAL_HEX8(0x4C, d[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, d[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, d[2]);
    TEST_ASSERT_EQUAL_HEX8(0x15, d[3]);
    TEST_ASSERT_EQUAL_HEX8(0xd2, d[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, d[20]);
    TEST_ASSERT_EQUAL_HEX8(0x07, d[21]);
    TEST_ASSERT_EQUAL_HEX8(0x00, d[22]);
    TEST_ASSERT_EQUAL_HEX8(0x65, d[23]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)-59, d[24]);

    uint8_t tok[beaconframe::kTokenLen];
    for(size_t i = 0; i < sizeof tok; i++)
        tok[i] = (uint8_t)(0x10 + i);
    uint8_t p[beaconframe::kPresenceLen];
    TEST_ASSERT_EQUAL(24, sizeof p);
    beaconframe::buildPresence(0x0379A1B2u, tok, true, p);
    TEST_ASSERT_EQUAL_HEX8(0x02, p[2]);
    TEST_ASSERT_EQUAL_HEX8(0x03, p[4]);            // slot is big-endian
    TEST_ASSERT_EQUAL_HEX8(0xB2, p[7]);
    TEST_ASSERT_EQUAL_HEX8(0x10, p[8]);
    uint32_t slot;
    uint8_t back[beaconframe::kTokenLen];
    bool synced;
    TEST_ASSERT_TRUE(beaconframe::parsePresence(p, sizeof p, slot, back, synced));
    TEST_ASSERT_EQUAL_UINT32(0x0379A1B2u, slot);
    TEST_ASSERT_TRUE(synced);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tok, back, beaconframe::kTokenLen);
    TEST_ASSERT_FALSE(beaconframe::parsePresence(p, sizeof p - 1, slot, back, synced));   // truncated

    p[2] = 0x99;                                   // wrong version
    TEST_ASSERT_FALSE(beaconframe::parsePresence(p, sizeof p, slot, back, synced));
    TEST_ASSERT_EQUAL(3, beaconframe::tokenSlot(95, 30));
    TEST_ASSERT_EQUAL(0, beaconframe::tokenSlot(0, 30));
    TEST_ASSERT_EQUAL(0, beaconframe::tokenSlot(29, 30));
    TEST_ASSERT_EQUAL(1, beaconframe::tokenSlot(30, 30));
    TEST_ASSERT_EQUAL(60, beaconframe::tokenSlot(60, 0));   // 0 slot length means 1 second

    TEST_ASSERT_FALSE(beaconframe::parseUuid(nullptr, uuid));
    TEST_ASSERT_TRUE(beaconframe::parseUuid("d22b117c2f6e4d749e9f6f09213c3dd0", uuid));
    TEST_ASSERT_EQUAL_HEX8(0xd2, uuid[0]);
    TEST_ASSERT_FALSE(beaconframe::parseUuid("d22b117c2f6e4d749e9f6f09213c3dd0aa", uuid));

    beaconframe::buildIBeacon(uuid, 65535, 65535, 0, d);
    TEST_ASSERT_EQUAL_HEX8(0xFF, d[20]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, d[21]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, d[22]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, d[23]);

    beaconframe::buildPresence(1, tok, false, p);
    TEST_ASSERT_EQUAL_HEX8(0x00, p[3]);            // clock not synced: flag bit clear
    synced = true;
    TEST_ASSERT_TRUE(beaconframe::parsePresence(p, sizeof p, slot, back, synced));
    TEST_ASSERT_FALSE(synced);
    TEST_ASSERT_EQUAL_UINT32(1, slot);
}

void setUp() {}
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ini_sections_keys_comments);
    RUN_TEST(test_ini_bools_duplicates_and_crlf);
    RUN_TEST(test_dotenv);
    RUN_TEST(test_dotenv_duplicate_and_crlf);
    RUN_TEST(test_route_json);
    RUN_TEST(test_route_alias_and_rejections);
    RUN_TEST(test_scenarios_json);
    RUN_TEST(test_beacon_frame);
    return UNITY_END();
}

#ifndef ARDUINO
void setup() {}
void loop() {}
#endif
