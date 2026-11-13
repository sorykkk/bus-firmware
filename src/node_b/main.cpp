// Node B: ESP32-CAM-MB. Periodically uploads a JPEG of the cabin; the server runs person
// counting and compares it with the riders it identified from app sightings (paid / refused / unknown).
// The server steers the cadence through the response: {"next_capture_ms": 3000}.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "Camera.h"
#include "DeviceConfig.h"
#include "Identity.h"
#include "NetClient.h"

static DeviceConfig cfg;
static Identity id;
static NetClient net;
static Camera cam;
static uint32_t intervalMs = 10000;
static uint32_t frameSeq = 0;

static void applyServerHints(const String &resp) {
    JsonDocument d;
    if(deserializeJson(d, resp))
        return;
    uint32_t v = d["next_capture_ms"] | 0u;
    if(v >= 1000 && v <= 60000)
        intervalMs = v;
}

static void captureAndUpload() {
    static uint32_t fails = 0;
    uint32_t t0 = millis();
    camera_fb_t *fb = cam.capture();
    if(!fb) {
        Serial.println("[cam] capture failed");
        if(++fails > 10)
            ESP.restart();
        return;
    }
    fails = 0;

    HttpHeader extra[] = {
        {"X-Bus-Id", String(id.busId)},
        {"X-Route-Id", String(id.routeId)},
        {"X-Frame-Seq", String(frameSeq++)},
        {"X-Capture-Ts", String(net.unixNow())},
        {"X-Frame-Size", String(fb->width) + "x" + String(fb->height)},
    };
    String resp;
    size_t len = fb->len;
    int code = net.post("/api/v1/buses/" + String(id.busId) + "/frames", "image/jpeg", fb->buf, len, &resp, extra,
                        sizeof extra / sizeof extra[0]);
    cam.release(fb);                             // always give the buffer back

    if(code >= 200 && code < 300)
        applyServerHints(resp);
    Serial.printf("[cam] %u bytes -> HTTP %d in %lu ms (next in %lu ms)\n", (unsigned)len, code,
                  (unsigned long)(millis() - t0), (unsigned long)intervalMs);
}

void setup() {
    Serial.begin(115200);
    delay(300);
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    cfg.load();
    id.load(cfg.routeId, cfg.busId);
    intervalMs = cfg.frameIntervalMs;
    Serial.printf("\n[boot] node B cam device=%s bus=%u route=%u psram=%d\n", id.deviceId.c_str(), id.busId,
                  id.routeId, psramFound());
    cfg.print(Serial);

    CameraSettings cs;
    cs.frameSize = CameraSettings::parseFrameSize(cfg.camFrameSize);
    cs.jpegQuality = cfg.camJpegQuality;
    cs.vflip = cfg.camVflip;
    cs.hmirror = cfg.camHmirror;
    cs.flash = cfg.camFlash;
    if(!cam.begin(cs)) {
        delay(2000);
        ESP.restart();
    }
    cam.warmup();

    net.begin(NetConfig{cfg.wifiSsid, cfg.wifiPass, cfg.serverBaseUrl, cfg.hmacSecret, id.deviceId});
}

void loop() {
    static uint32_t lastTick = 0, nextShot = 0;
    uint32_t now = millis();
    esp_task_wdt_reset();

    if(now - lastTick >= 1000) {
        lastTick = now;
        net.tick();
    }
    if((int32_t)(now - nextShot) < 0 || !net.online()) {
        delay(50);
        return;
    }
    nextShot = now + intervalMs;
    captureAndUpload();
}
