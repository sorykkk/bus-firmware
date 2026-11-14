// On-device test of the BLE beacon (Node A hardware, needs a radio).
//
// With a phone running a BLE scanner (e.g. nRF Connect) you should see:
//   advert:        iBeacon, UUID = system_uuid, major = route, minor = bus
//   scan response: manufacturer data FF FF 02 ..., 24 bytes (4-byte slot + 16-byte token)
// The scan response is absent until a clock-synced token is installed, and it
// goes away again when the token is cleared. The iBeacon advert stays up either way.
#include <Arduino.h>
#include <string.h>
#include <unity.h>
#include <NimBLEDevice.h>
#include "Beacon.h"
#include "BeaconFrame.h"
#include "Crypto.h"
#include "DeviceConfig.h"

static DeviceConfig cfg;

void test_config_has_system_uuid() {
    TEST_ASSERT_TRUE_MESSAGE(cfg.iniLoaded, "config.ini missing: pio run -t uploadfs");
    uint8_t uuid[16];
    TEST_ASSERT_TRUE_MESSAGE(beaconframe::parseUuid(cfg.systemUuid.c_str(), uuid), "bad system_uuid");
}

void test_presence_token_matches_hmac() {
    // Token is the first 16 bytes of HMAC-SHA256(secret, "<bus>.<slot>"), which is
    // what the server recomputes. The HMAC itself is checked against RFC 4231 in the wifi test.
    uint8_t tok[beaconframe::kTokenLen];
    crypto::presenceToken("Jefe", 101, 30, tok);
    String mac = crypto::hmacSha256Hex("Jefe", "101.30");
    TEST_ASSERT_EQUAL(64, mac.length());
    TEST_ASSERT_EQUAL_STRING(mac.substring(0, 32).c_str(), crypto::toHex(tok, 16).c_str());

    uint8_t again[beaconframe::kTokenLen];
    crypto::presenceToken("Jefe", 101, 30, again);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tok, again, beaconframe::kTokenLen);

    uint8_t other[beaconframe::kTokenLen];
    crypto::presenceToken("Jefe", 101, 31, other);
    TEST_ASSERT_FALSE(memcmp(tok, other, sizeof tok) == 0);
    crypto::presenceToken("Jefe", 102, 30, other);
    TEST_ASSERT_FALSE(memcmp(tok, other, sizeof tok) == 0);
    crypto::presenceToken("other", 101, 30, other);
    TEST_ASSERT_FALSE(memcmp(tok, other, sizeof tok) == 0);
}

void test_beacon_advertises() {
    NimBLEDevice::init("beacon-test");
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);

    beacon::Config bad{cfg.systemUuid.c_str(), cfg.routeId, cfg.busId, cfg.txPower1m, cfg.advIntervalMs};
    bad.systemUuid = "zz";
    TEST_ASSERT_FALSE(beacon::begin(bad));

    bool up = beacon::begin(beacon::Config{cfg.systemUuid.c_str(), cfg.routeId, cfg.busId, cfg.txPower1m,
                                           cfg.advIntervalMs});
    TEST_ASSERT_TRUE(up);
    TEST_ASSERT_TRUE(beacon::advertising());

    // No synced token yet. Clearing must leave the iBeacon advert running.
    beacon::clearToken();
    TEST_ASSERT_TRUE(beacon::advertising());
    uint8_t tok[beaconframe::kTokenLen];
    memset(tok, 0xAB, sizeof tok);
    beacon::updateToken(58234123u, tok, false);   // clock not synced: scan response stays off
    TEST_ASSERT_TRUE_MESSAGE(beacon::advertising(), "advertising died while clearing an unsynced token");

    beacon::updateToken(58234123u, tok, true);
    TEST_ASSERT_TRUE_MESSAGE(beacon::advertising(), "advertising died after token refresh");
    memset(tok, 0xCD, sizeof tok);
    beacon::updateToken(58234124u, tok, true);    // next 30 s slot
    TEST_ASSERT_TRUE_MESSAGE(beacon::advertising(), "advertising died on the second token");

    beacon::updateToken(58234124u, tok, false);
    TEST_ASSERT_TRUE_MESSAGE(beacon::advertising(), "advertising died when the token was removed");
    beacon::clearToken();
    TEST_ASSERT_TRUE(beacon::advertising());

    bad.systemUuid = "not-a-uuid";
    TEST_ASSERT_FALSE_MESSAGE(beacon::begin(bad), "a bad UUID must not be accepted");
    TEST_ASSERT_TRUE_MESSAGE(beacon::advertising(), "a rejected UUID update stopped the iBeacon");
}

void setup() {
    Serial.begin(115200);
    delay(800);
    cfg.load();

    UNITY_BEGIN();
    RUN_TEST(test_config_has_system_uuid);
    RUN_TEST(test_presence_token_matches_hmac);
    RUN_TEST(test_beacon_advertises);
    UNITY_END();
}

void loop() {}
