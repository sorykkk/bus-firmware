// On-device test of the camera (ESP32-CAM-MB only: `pio test -e node_b_cam`).
// Checks init, frame format, repeated capture stability and (optionally) one upload.
#include <Arduino.h>
#include <unity.h>
#include "Camera.h"
#include "DeviceConfig.h"
#include "NetClient.h"

static DeviceConfig cfg;
static Camera cam;

void test_camera_inits() {
    CameraSettings s;
    s.frameSize = CameraSettings::parseFrameSize(cfg.camFrameSize);
    s.jpegQuality = cfg.camJpegQuality;
    s.vflip = cfg.camVflip;
    s.hmirror = cfg.camHmirror;
    TEST_ASSERT_TRUE_MESSAGE(cam.begin(s), "esp_camera_init failed (camera module seated? PSRAM?)");
    Serial.printf("[cam] psram=%d frame=%s\n", cam.hasPsram(), cfg.camFrameSize.c_str());
    cam.warmup(2);
}

void test_capture_is_jpeg() {
    camera_fb_t *fb = cam.capture();
    TEST_ASSERT_NOT_NULL_MESSAGE(fb, "capture returned null");
    TEST_ASSERT_EQUAL(PIXFORMAT_JPEG, fb->format);
    TEST_ASSERT_TRUE(fb->len > 2000);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fb->buf[0]);   // JPEG SOI
    TEST_ASSERT_EQUAL_HEX8(0xD8, fb->buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fb->buf[fb->len - 2]);   // JPEG EOI
    TEST_ASSERT_EQUAL_HEX8(0xD9, fb->buf[fb->len - 1]);
    Serial.printf("[cam] frame %ux%u, %u bytes\n", fb->width, fb->height, (unsigned)fb->len);
    cam.release(fb);
}

void test_repeated_captures_stable() {
    int ok = 0;
    for(int i = 0; i < 5; i++) {
        camera_fb_t *fb = cam.capture();
        if(fb && fb->len > 2000)
            ok++;
        cam.release(fb);
        delay(50);
    }
    TEST_ASSERT_EQUAL_MESSAGE(5, ok, "captures failed back to back (frame buffer leak?)");
}

void test_optional_upload() {
    if(!cfg.hasWifiCredentials()) {
        TEST_IGNORE_MESSAGE("no WIFI_SSID in env/node_b/.env: upload not tested");
    }
    NetClient net;
    net.begin(NetConfig{cfg.wifiSsid, cfg.wifiPass, cfg.serverBaseUrl, cfg.hmacSecret, "cam-test"});
    if(!net.waitOnline(20000)) {
        TEST_IGNORE_MESSAGE("Wi-Fi did not connect: upload not tested");
    }
    camera_fb_t *fb = cam.capture();
    TEST_ASSERT_NOT_NULL(fb);
    String resp;
    int code = net.post("/api/v1/buses/0/frames", "image/jpeg", fb->buf, fb->len, &resp);
    cam.release(fb);
    Serial.printf("[cam] upload -> HTTP %d\n", code);
    TEST_ASSERT_TRUE_MESSAGE(code > 0, "upload failed at transport level");
    TEST_ASSERT_LESS_THAN(500, code);
}

void setup() {
    Serial.begin(115200);
    delay(800);
    cfg.load();

    UNITY_BEGIN();
    RUN_TEST(test_camera_inits);
    if(cam.ready()) {
        RUN_TEST(test_capture_is_jpeg);
        RUN_TEST(test_repeated_captures_stable);
        RUN_TEST(test_optional_upload);
    }
    UNITY_END();
}

void loop() {}
