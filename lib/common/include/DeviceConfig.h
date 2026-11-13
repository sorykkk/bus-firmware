#pragma once
#include <Arduino.h>
#include <string>

#ifndef DEFAULT_ROUTE_ID
#define DEFAULT_ROUTE_ID 7
#endif
#ifndef DEFAULT_BUS_ID
#define DEFAULT_BUS_ID 420
#endif

/// Everything a node needs at runtime. Sources, lowest to highest priority:
///   1. compiled defaults below (+ -D flags from platformio.ini)
///   2. /config.ini   on LittleFS  <- env/config.ini
///   3. /secrets.env  on LittleFS  <- env/node_a/.env or env/node_b/.env
/// Files are put on flash by `pio run -e <env> -t uploadfs` (see tools/prepare_fs.py).
struct DeviceConfig {
    // [identity]
    uint16_t routeId = DEFAULT_ROUTE_ID;
    uint16_t busId = DEFAULT_BUS_ID;

    // .env
    String wifiSsid;
    String wifiPass;
    String serverBaseUrl = "http://192.168.1.50:8000";
    String hmacSecret = "change-me-per-bus";

    // [ble]
    String systemUuid = "d22b117c-2f6e-4d74-9e9f-6f09213c3dd0";
    int8_t txPower1m = -59;
    int8_t radioPowerDbm = 3;
    uint16_t advIntervalMs = 150;
    uint16_t tokenSlotS = 30;

    // [timing]
    uint32_t telemetryIntervalMs = 2000;
    uint32_t frameIntervalMs = 10000;

    // [pins]
    int pinButton = 0;
    int pinGpsRx = 16;
    int pinGpsTx = 17;
    uint32_t gpsBaud = 9600;

    // [sim]
    String simScenario;

    // [camera]
    String camFrameSize = "SVGA";
    int camJpegQuality = 12;
    bool camVflip = false;
    bool camHmirror = false;
    bool camFlash = false;

    bool fsMounted = false;
    bool iniLoaded = false;
    bool envLoaded = false;

    /// Mounts LittleFS and applies the files. Missing files are not fatal (defaults stay).
    bool load();
    void print(Print &out) const;
    bool hasWifiCredentials() const { return wifiSsid.length() > 0; }

    static bool mountFs();
    static bool readFile(const char *path, std::string &out);
    static bool writeFile(const char *path, const std::string &data);
};

constexpr const char *kConfigIniPath = "/config.ini";
constexpr const char *kSecretsEnvPath = "/secrets.env";
constexpr const char *kRouteJsonPath = "/route.json";
constexpr const char *kScenariosJsonPath = "/scenarios.json";
