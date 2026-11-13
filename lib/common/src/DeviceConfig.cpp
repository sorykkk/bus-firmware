#include "DeviceConfig.h"
#include <LittleFS.h>
#include "DotEnv.h"
#include "IniFile.h"

bool DeviceConfig::mountFs() {
    static bool mounted = false;
    if(!mounted)
        mounted = LittleFS.begin(false);
    return mounted;
}

bool DeviceConfig::readFile(const char *path, std::string &out) {
    if(!mountFs())
        return false;
    File f = LittleFS.open(path, "r");
    if(!f || f.isDirectory())
        return false;
    out.clear();
    out.reserve(f.size());
    uint8_t buf[256];
    while(f.available()) {
        size_t n = f.read(buf, sizeof buf);
        out.append((const char *)buf, n);
    }
    f.close();
    return true;
}

bool DeviceConfig::writeFile(const char *path, const std::string &data) {
    if(!mountFs())
        return false;
    File f = LittleFS.open(path, "w");
    if(!f)
        return false;
    size_t n = f.write((const uint8_t *)data.data(), data.size());
    f.close();
    return n == data.size();
}

static String str(const IniFile &ini, const char *sec, const char *key, const String &def) {
    return ini.has(sec, key) ? String(ini.get(sec, key).c_str()) : def;
}

static String str(const DotEnv &env, const char *key, const String &def) {
    return env.has(key) ? String(env.get(key).c_str()) : def;
}

bool DeviceConfig::load() {
    fsMounted = mountFs();
    if(!fsMounted)
        Serial.println("[cfg] LittleFS not mounted: run `pio run -t uploadfs` (using compiled defaults)");

    std::string text;
    if(readFile(kConfigIniPath, text)) {
        IniFile ini;
        if(!ini.parse(text))
            Serial.printf("[cfg] %s: %d malformed line(s) skipped\n", kConfigIniPath, ini.errorCount());
        iniLoaded = true;

        routeId = ini.getInt("identity", "route_id", routeId);
        busId = ini.getInt("identity", "bus_id", busId);

        systemUuid = str(ini, "ble", "system_uuid", systemUuid);
        txPower1m = ini.getInt("ble", "tx_power_1m", txPower1m);
        radioPowerDbm = ini.getInt("ble", "radio_power_dbm", radioPowerDbm);
        advIntervalMs = ini.getInt("ble", "adv_interval_ms", advIntervalMs);
        tokenSlotS = ini.getInt("ble", "token_slot_s", tokenSlotS);

        telemetryIntervalMs = ini.getInt("timing", "telemetry_interval_ms", telemetryIntervalMs);
        frameIntervalMs = ini.getInt("timing", "frame_default_interval_ms", frameIntervalMs);

        pinButton = ini.getInt("pins", "button", pinButton);
        pinGpsRx = ini.getInt("pins", "gps_rx", pinGpsRx);
        pinGpsTx = ini.getInt("pins", "gps_tx", pinGpsTx);
        gpsBaud = ini.getInt("pins", "gps_baud", gpsBaud);

        simScenario = str(ini, "sim", "scenario", simScenario);

        camFrameSize = str(ini, "camera", "frame_size", camFrameSize);
        camJpegQuality = ini.getInt("camera", "jpeg_quality", camJpegQuality);
        camVflip = ini.getBool("camera", "vflip", camVflip);
        camHmirror = ini.getBool("camera", "hmirror", camHmirror);
        camFlash = ini.getBool("camera", "flash", camFlash);
    } else {
        Serial.printf("[cfg] %s missing, using defaults\n", kConfigIniPath);
    }

    if(readFile(kSecretsEnvPath, text)) {
        DotEnv env;
        if(!env.parse(text))
            Serial.printf("[cfg] %s: %d malformed line(s) skipped\n", kSecretsEnvPath, env.errorCount());
        envLoaded = true;
        wifiSsid = str(env, "WIFI_SSID", wifiSsid);
        wifiPass = str(env, "WIFI_PASS", wifiPass);
        serverBaseUrl = str(env, "SERVER_BASE_URL", serverBaseUrl);
        hmacSecret = str(env, "DEVICE_HMAC_SECRET", hmacSecret);
    } else {
        Serial.printf("[cfg] %s missing: no Wi-Fi credentials (create env/node_a/.env or env/node_b/.env)\n",
                      kSecretsEnvPath);
    }

    while(serverBaseUrl.endsWith("/"))
        serverBaseUrl.remove(serverBaseUrl.length() - 1);
    return iniLoaded && envLoaded;
}

void DeviceConfig::print(Print &out) const {
    out.printf("[cfg] files: fs=%d ini=%d env=%d\n", fsMounted, iniLoaded, envLoaded);
    out.printf("[cfg] route=%u bus=%u server=%s wifi=\"%s\" secret=%s\n", routeId, busId, serverBaseUrl.c_str(),
               wifiSsid.c_str(), hmacSecret.length() ? "set" : "EMPTY");
    out.printf("[cfg] ble uuid=%s tx1m=%d pwr=%ddBm adv=%ums slot=%us\n", systemUuid.c_str(), txPower1m,
               radioPowerDbm, advIntervalMs, tokenSlotS);
}
