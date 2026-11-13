// Node A: BLE beacon + GPS telemetry.
//
//   phone app <--BLE--> [Node A] --Wi-Fi/HTTPS--> server
//     iBeacon (route/bus) wakes the app; its scan response carries a rolling signed token.
//     The app sends its signed-token sightings to the server under the user's account.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <NimBLEDevice.h>
#include <esp_task_wdt.h>
#include "Beacon.h"
#include "BeaconFrame.h"
#include "Crypto.h"
#include "DeviceConfig.h"
#include "Identity.h"
#include "NetClient.h"
#include "SimScenario.h"
#include "StoreForward.h"

#if defined(USE_SIM_GPS)
#include "SimGps.h"
static SimGps *sim = nullptr;
#elif defined(USE_REAL_GPS)
#include "RealGps.h"
#else
#error "Define USE_SIM_GPS or USE_REAL_GPS in platformio.ini"
#endif

static DeviceConfig cfg;
static Identity id;
static NetClient net;
static StoreForward telemetryQ(60);
static GpsProvider *gps = nullptr;
static uint32_t seqNo = 0;
static String bootId;                        // new random value every boot; seq restarts under it
static uint32_t tokenSlot = UINT32_MAX;

static String busPath(const char *leaf) {
    return "/api/v1/buses/" + String(id.busId) + "/" + leaf;
}

// ------------------------------------------------------------------ uplink
static void uplinkTask(void *) {
    esp_task_wdt_add(NULL);
    uint32_t backoff = 0;
    for(;;) {
        esp_task_wdt_reset();
        StoreForward::Msg m;
        if(!telemetryQ.peek(m)) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        int code = net.postJson(m.path, m.body);
        if(code >= 200 && code < 300) {
            telemetryQ.pop();
            backoff = 0;
        } else if(code >= 400 && code < 500) {      // server rejected it: retrying won't help
            Serial.printf("[up] HTTP %d on %s, dropping message\n", code, m.path.c_str());
            telemetryQ.pop();
        } else {                                    // offline / 5xx: keep it, back off
            backoff = min<uint32_t>(backoff * 2 + 500, 5000);
            vTaskDelay(pdMS_TO_TICKS(backoff));
        }
    }
}

// ------------------------------------------------------------------ telemetry
static void fillCommon(JsonDocument &d) {
    d["bus_id"] = id.busId;
    d["route_id"] = id.routeId;
    d["device_id"] = id.deviceId;
    d["boot_id"] = bootId;
    d["seq"] = seqNo++;
    d["ts"] = net.unixNow();
    d["uptime_ms"] = millis();
    d["sim"] = gps->simulated() ? 1 : 0;     // 1 = lat/lon come from SimGps, not a receiver
}

static String buildTelemetry() {
    GpsFix f = gps->fix();
    JsonDocument d;
    fillCommon(d);
    d["lat"] = serialized(String(f.lat, 6));
    d["lon"] = serialized(String(f.lon, 6));
    d["speed_kmh"] = serialized(String(f.speed_kmh, 1));
    d["heading"] = (int)f.heading_deg;
    d["fix_ok"] = f.fix_ok;
    d["sats"] = f.sats;
    d["stop_index"] = f.stop_index;
    d["last_stop_index"] = f.last_stop_index;
    d["wifi_rssi"] = net.rssi();
#ifdef USE_SIM_GPS
    d["scenario"] = sim->engine().scenarioName();
    d["sim_state"] = sim->engine().stateName();
#else
    d["scenario"] = "real";
#endif
    String out;
    serializeJson(d, out);
    return out;
}

// ------------------------------------------------------------------ BLE
static esp_power_level_t powerLevel(int dbm) {
    int idx = (dbm + 12) / 3;               // ESP_PWR_LVL_N12 (0) .. ESP_PWR_LVL_P9 (7), 3 dB steps
    return (esp_power_level_t)constrain(idx, 0, 7);
}

static void refreshToken() {
    uint32_t unix = net.unixNow();
    if(!unix) {
        if(tokenSlot != UINT32_MAX) {
            tokenSlot = UINT32_MAX;
            beacon::clearToken();
            Serial.println("[ble] clock not synced, token off");
        }
        return;
    }
    uint32_t slot = beaconframe::tokenSlot(unix, cfg.tokenSlotS);
    if(slot == tokenSlot)
        return;
    bool first = tokenSlot == UINT32_MAX;
    tokenSlot = slot;
    uint8_t tok[beaconframe::kTokenLen];
    crypto::presenceToken(cfg.hmacSecret.c_str(), id.busId, slot, tok);
    beacon::updateToken(slot, tok, true);
    if(first)
        Serial.printf("[ble] clock synced, token on (slot %u)\n", slot);
}

// ------------------------------------------------------------------ route sync
/// Downloads the route this bus is configured to run and caches it on flash.
static bool syncRoute() {
    String body;
    int code = net.get("/api/v1/routes/" + String(id.routeId), &body);
    Route r;
    std::string err;
    if(code != 200 || !parseRouteJson(body.c_str(), r, &err)) {
        Serial.printf("[route] download failed (HTTP %d %s), keeping cached %s\n", code, err.c_str(), kRouteJsonPath);
        return false;
    }
    if(r.routeId && r.routeId != id.routeId) {
        Serial.printf("[route] server sent route %u, expected %u: ignored\n", r.routeId, id.routeId);
        return false;
    }
    bool ok = DeviceConfig::writeFile(kRouteJsonPath, body.c_str());
    Serial.printf("[route] '%s' (%u points) %s\n", r.name.c_str(), (unsigned)r.points.size(),
                  ok ? "saved" : "NOT saved (flash write failed)");
    return ok;
}

// ------------------------------------------------------------------ demo controls
static void printStatusRoute() {
    GpsFix f = gps->fix();
    Serial.printf("[st-route] bus=%u route=%u wifi=%s q=%u ",
                  id.busId, id.routeId, net.online() ? "up" : "DOWN", (unsigned)telemetryQ.size());
#ifdef USE_SIM_GPS
    SimEngine &e = sim->engine();
    Serial.printf("scn=%s state=%s x%.1f cruise=%.0fkm/h pos=%.0f/%.0fm stop=%s ", e.scenarioName(), e.stateName(),
                  e.timeScale(), e.cruiseKmh(), e.progressM(), e.totalM(), e.stopName(f.stop_index));
#endif
    Serial.printf("lat=%.5f lon=%.5f v=%.1f fix=%d\n", f.lat, f.lon, f.speed_kmh, f.fix_ok);
}

static void printStatusBle() {
    Serial.printf("[st-ble] uuid=%s major(route)=%u minor(bus)=%u adv=%ums slot=%us wifi=%s rssi=%d ntp=%s advert=%s ",
                cfg.systemUuid.c_str(), id.routeId, id.busId, cfg.advIntervalMs, cfg.tokenSlotS,
                net.online() ? "up" : "DOWN", net.rssi(), net.timeSynced() ? "ok" : "no",
                beacon::advertising() ? "on" : "off");
    if(tokenSlot != UINT32_MAX && net.timeSynced()) {
        uint8_t tok[beaconframe::kTokenLen];
        crypto::presenceToken(cfg.hmacSecret.c_str(), id.busId, tokenSlot, tok);
        Serial.printf("ble_slot=%u token=%s\n", tokenSlot, crypto::toHex(tok, beaconframe::kTokenLen).c_str());
    } else {
        Serial.println("ble_slot=off token=off");
    }
}

static void printHelp() {
    Serial.println(F("commands: status route | status ble | config | help | set bus N | set route N | clear id | reboot | route sync"));
#ifdef USE_SIM_GPS
    Serial.println(F("   sim:   start | pause | reset | scn N | next | speed KMH | scale X"));
    SimEngine &e = sim->engine();
    Serial.print(F("   scenarios: "));
    for(size_t i = 0; i < e.scenarioCount(); i++)
        Serial.printf("%u=%s ", (unsigned)i, e.scenarioNameAt(i));
    Serial.println();
#endif
}

static void rebootAfterSave() {
    id.save();
    Serial.println("saved, rebooting");
    delay(200);
    ESP.restart();
}

static void handleCommand(String cmd) {
    cmd.toLowerCase();
    if(cmd == "status route") 
        printStatusRoute();
    else if(cmd == "status ble")
        printStatusBle();
    else if(cmd == "status") {
        printStatusRoute();
        printStatusBle();
    }
    else if(cmd == "help") printHelp();
    else if(cmd == "config") cfg.print(Serial);
    else if(cmd == "reboot") ESP.restart();
    else if(cmd.startsWith("set bus ")) { id.busId = cmd.substring(8).toInt(); rebootAfterSave(); }
    else if(cmd.startsWith("set route ")) { id.routeId = cmd.substring(10).toInt(); rebootAfterSave(); }
    else if(cmd == "clear id") { Identity::clear(); Serial.println("NVS identity cleared, rebooting"); delay(200); ESP.restart(); }
    else if(cmd == "route sync") {
        if(syncRoute()) {
#ifdef USE_SIM_GPS
            sim->reload();
#endif
        }
    }
#ifdef USE_SIM_GPS
    else if(cmd == "start") sim->engine().start();
    else if(cmd == "pause") sim->engine().pause();
    else if(cmd == "reset") sim->engine().reset();
    else if(cmd == "next") { sim->engine().nextScenario(); Serial.printf("scenario: %s\n", sim->engine().scenarioName()); }
    else if(cmd.startsWith("scn ")) { sim->engine().setScenario((size_t)cmd.substring(4).toInt()); Serial.printf("scenario: %s\n", sim->engine().scenarioName()); }
    else if(cmd.startsWith("speed ")) sim->engine().setCruiseKmh(cmd.substring(6).toFloat());
    else if(cmd.startsWith("scale ")) sim->engine().setTimeScale(cmd.substring(6).toFloat());
#endif
    else Serial.println(F("unknown command, try `help`"));
}

static void handleSerial() {
    static String line;
    while(Serial.available()) {
        char c = Serial.read();
        if(c != '\n' && c != '\r') {
            line += c;
            continue;
        }
        line.trim();
        if(line.length())
            handleCommand(line);
        line = "";
    }
}

static void handleButton(uint32_t now) {
#ifdef USE_SIM_GPS
    if(cfg.pinButton < 0)
        return;
    static bool prev = true;
    static uint32_t t0 = 0;
    bool cur = digitalRead(cfg.pinButton);
    if(prev && !cur)
        t0 = now;
    if(!prev && cur) {
        uint32_t held = now - t0;
        if(held > 1000) {
            sim->engine().nextScenario();
            Serial.printf("[btn] scenario: %s\n", sim->engine().scenarioName());
        } else if(held > 30) {
            sim->engine().toggle();
            Serial.printf("[btn] %s\n", sim->engine().stateName());
        }
    }
    prev = cur;
#endif
}

// ------------------------------------------------------------------ main
void setup() {
    Serial.begin(115200);
    delay(300);
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    cfg.load();
    id.load(cfg.routeId, cfg.busId);
    char bootHex[9];
    snprintf(bootHex, sizeof bootHex, "%08lx", (unsigned long)esp_random());
    bootId = bootHex;
    Serial.printf("\n[boot] node A device=%s boot=%s bus=%u route=%u%s\n", id.deviceId.c_str(), bootId.c_str(),
                  id.busId, id.routeId, id.fromNvs ? " (NVS override)" : "");
    cfg.print(Serial);
    if(cfg.pinButton >= 0)
        pinMode(cfg.pinButton, INPUT_PULLUP);

    NimBLEDevice::init(("BUS-" + String(id.busId)).c_str());
    NimBLEDevice::setPower(powerLevel(cfg.radioPowerDbm));
    beacon::begin(beacon::Config{cfg.systemUuid.c_str(), id.routeId, id.busId, cfg.txPower1m, cfg.advIntervalMs});
    refreshToken();

    net.begin(NetConfig{cfg.wifiSsid, cfg.wifiPass, cfg.serverBaseUrl, cfg.hmacSecret, id.deviceId});

    if(net.waitOnline(8000))
        syncRoute();
    else
        Serial.println("[route] offline at boot, using cached route");

#ifdef USE_SIM_GPS
    sim = new SimGps(cfg.simScenario.c_str());
    gps = sim;
#else
    gps = new RealGps(cfg.pinGpsRx, cfg.pinGpsTx, cfg.gpsBaud);
#endif
    gps->begin();

    xTaskCreatePinnedToCore(uplinkTask, "uplink", 8192, nullptr, 1, nullptr, 1);
    printHelp();
}

void loop() {
    static uint32_t lastTelem = 0, lastTick = 0;
    uint32_t now = millis();
    esp_task_wdt_reset();

    handleButton(now);
    handleSerial();
    gps->update(now);

    if(now - lastTelem >= cfg.telemetryIntervalMs) {
        lastTelem = now;
        telemetryQ.push(busPath("telemetry"), buildTelemetry());
    }
    if(now - lastTick >= 1000) {
        lastTick = now;
        net.tick();
        refreshToken();
    }
    delay(20);
}
