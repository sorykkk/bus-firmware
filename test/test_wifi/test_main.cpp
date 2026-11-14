// On-device test of config files, the signature math, the telemetry queue, and the
// Wi-Fi + signed HTTP stack (Node A or Node B hardware).
// Needs the node's .env flashed first:  pio run -e node_a_sim -t uploadfs
// (env/node_a/.env, or env/node_b/.env for the camera)
//
// Association, RSSI, NTP and a round trip need the network. The hash and queue
// tests do not. Point SERVER_BASE_URL at tools/mock_server.py so a bad signature
// comes back as 401; any other HTTP server still validates transport.
#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>
#include "Crypto.h"
#include "DeviceConfig.h"
#include "NetClient.h"
#include "SimScenario.h"
#include "StoreForward.h"

static DeviceConfig cfg;
static NetClient net;

void test_config_files_present() {
    TEST_ASSERT_TRUE_MESSAGE(cfg.fsMounted, "LittleFS missing: pio run -t uploadfs");
    TEST_ASSERT_TRUE_MESSAGE(cfg.iniLoaded, "config.ini missing: pio run -t uploadfs");
    TEST_ASSERT_TRUE_MESSAGE(cfg.envLoaded, "secrets.env missing: copy env/.env.example to env/node_a/.env or env/node_b/.env, then uploadfs");
    TEST_ASSERT_TRUE_MESSAGE(cfg.hasWifiCredentials(), "WIFI_SSID empty in the node's .env");
    TEST_ASSERT_TRUE_MESSAGE(cfg.hmacSecret.length() > 0, "DEVICE_HMAC_SECRET empty");
    TEST_ASSERT_TRUE_MESSAGE(cfg.serverBaseUrl.startsWith("http"), "SERVER_BASE_URL must start with http:// or https://");
    TEST_ASSERT_FALSE_MESSAGE(cfg.serverBaseUrl.endsWith("/"), "SERVER_BASE_URL should not keep a trailing slash");
    TEST_ASSERT_TRUE_MESSAGE(cfg.tokenSlotS > 0, "token_slot_s must be at least 1");
}

void test_flashed_route_and_scenarios() {
    // Node A images carry both files. Node B's image has neither, so this is skipped there.
    std::string routeText, scenarioText;
    bool hasRoute = DeviceConfig::readFile(kRouteJsonPath, routeText);
    bool hasScenarios = DeviceConfig::readFile(kScenariosJsonPath, scenarioText);
    if(!hasRoute && !hasScenarios)
        TEST_IGNORE_MESSAGE("no route.json: this is a Node B image, or uploadfs did not include Node A files");

    TEST_ASSERT_TRUE_MESSAGE(hasRoute, "/route.json missing from the Node A filesystem");
    TEST_ASSERT_TRUE_MESSAGE(hasScenarios, "/scenarios.json missing from the Node A filesystem");

    Route route;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(parseRouteJson(routeText, route, &err), err.c_str());
    TEST_ASSERT_TRUE_MESSAGE(route.points.size() >= 2, "route needs at least 2 points");

    ScenarioSet set;
    TEST_ASSERT_TRUE_MESSAGE(parseScenariosJson(scenarioText, set, &err), err.c_str());
    TEST_ASSERT_TRUE_MESSAGE(!set.scenarios.empty(), "scenarios.json has no scenarios");
    TEST_ASSERT_TRUE_MESSAGE(set.indexOf(set.defaultName) >= 0, "default scenario is not in the file");
}

void test_sha256_and_hmac_known_answers() {
    // GET signs a zero-length body. SHA-256("") is the empty digest.
    const uint8_t empty[1] = {0};
    const char *emptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    TEST_ASSERT_EQUAL_STRING(emptyHash, crypto::sha256Hex(empty, 0).c_str());
    TEST_ASSERT_EQUAL_STRING(emptyHash, crypto::sha256Hex(nullptr, 0).c_str());

    const uint8_t abc[] = {'a', 'b', 'c'};
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             crypto::sha256Hex(abc, sizeof abc).c_str());

    // RFC 4231 test case 2. The key is a C string, which is how hmacSha256 reads it.
    TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                             crypto::hmacSha256Hex("Jefe", "what do ya want for nothing?").c_str());
}

void test_telemetry_queue_drops_oldest() {
    StoreForward q(2);
    StoreForward::Msg m;
    TEST_ASSERT_FALSE(q.peek(m));
    q.pop();                                    // empty pop must not crash

    q.push("/a", "1");
    q.push("/b", "2");
    q.push("/c", "3");                          // cap is 2, so "/a" is dropped
    TEST_ASSERT_EQUAL(2, q.size());
    TEST_ASSERT_TRUE(q.peek(m));
    TEST_ASSERT_EQUAL_STRING("/b", m.path.c_str());
    TEST_ASSERT_EQUAL_STRING("2", m.body.c_str());
    q.pop();
    TEST_ASSERT_TRUE(q.peek(m));
    TEST_ASSERT_EQUAL_STRING("/c", m.path.c_str());
    TEST_ASSERT_EQUAL_STRING("3", m.body.c_str());
    q.pop();
    TEST_ASSERT_EQUAL(0, q.size());
    TEST_ASSERT_FALSE(q.peek(m));
}

void test_wifi_connects() {
    uint32_t t0 = millis();
    bool up = net.waitOnline(20000);
    Serial.printf("[wifi] %s in %lu ms, rssi=%d ip=%s\n", up ? "up" : "DOWN",
                  (unsigned long)(millis() - t0), net.rssi(), WiFi.localIP().toString().c_str());
    TEST_ASSERT_TRUE_MESSAGE(up, "no association in 20 s (check SSID/password and 2.4 GHz)");
    TEST_ASSERT_INT_WITHIN(50, -70, net.rssi());   // a connected STA reports something in [-120, -20]
}

void test_ntp_syncs() {
    uint32_t t0 = millis();
    while(!net.timeSynced() && millis() - t0 < 15000)
        delay(200);
    TEST_ASSERT_TRUE_MESSAGE(net.timeSynced(), "NTP did not sync in 15 s");
    TEST_ASSERT_TRUE(net.unixNow() > 1700000000);
    TEST_ASSERT_TRUE(net.unixNow() < 2200000000ul);   // a stuck or garbage clock fails this

    net.tick();                                 // online path must not tear the link down
    TEST_ASSERT_TRUE(net.online());
}

void test_http_roundtrip() {
    String body;
    int code = net.get("/", &body);
    Serial.printf("[http] GET / -> %d (%u bytes)\n", code, (unsigned)body.length());
    TEST_ASSERT_TRUE_MESSAGE(code > 0, "no HTTP response (is the server running and reachable?)");
    TEST_ASSERT_LESS_THAN(500, code);

    const char *json = "{\"ping\":true}";
    code = net.post("/api/v1/buses/0/telemetry", "application/json",
                    (const uint8_t *)json, strlen(json), &body);
    Serial.printf("[http] signed POST -> %d\n", code);
    TEST_ASSERT_TRUE_MESSAGE(code > 0, "signed POST failed at transport level");
    TEST_ASSERT_LESS_THAN(500, code);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(401, code, "server rejected the signature (board secret != server secret)");
}

void setup() {
    Serial.begin(115200);
    delay(800);
    cfg.load();
    cfg.print(Serial);
    net.begin(NetConfig{cfg.wifiSsid, cfg.wifiPass, cfg.serverBaseUrl, cfg.hmacSecret, "wifi-test"});

    UNITY_BEGIN();
    RUN_TEST(test_config_files_present);
    RUN_TEST(test_flashed_route_and_scenarios);
    RUN_TEST(test_sha256_and_hmac_known_answers);
    RUN_TEST(test_telemetry_queue_drops_oldest);
    if(cfg.hasWifiCredentials()) {
        RUN_TEST(test_wifi_connects);
        if(net.online()) {
            RUN_TEST(test_ntp_syncs);
            RUN_TEST(test_http_roundtrip);
        }
    }
    UNITY_END();
}

void loop() {}
