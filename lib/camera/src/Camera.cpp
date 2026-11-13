#include "Camera.h"

framesize_t CameraSettings::parseFrameSize(const String &name) {
    String n = name;
    n.toUpperCase();
    if(n == "QVGA") return FRAMESIZE_QVGA;
    if(n == "VGA")  return FRAMESIZE_VGA;
    if(n == "SVGA") return FRAMESIZE_SVGA;
    if(n == "XGA")  return FRAMESIZE_XGA;
    if(n == "HD")   return FRAMESIZE_HD;
    if(n == "SXGA") return FRAMESIZE_SXGA;
    if(n == "UXGA") return FRAMESIZE_UXGA;
    return FRAMESIZE_SVGA;
}

bool Camera::begin(const CameraSettings &s) {
    s_ = s;
    psram_ = psramFound();

    camera_config_t c = {};
    c.ledc_channel = LEDC_CHANNEL_0;
    c.ledc_timer = LEDC_TIMER_0;

    c.pin_d0 = 5;
    c.pin_d1 = 18;
    c.pin_d2 = 19;
    c.pin_d3 = 21;
    c.pin_d4 = 36;
    c.pin_d5 = 39;
    c.pin_d6 = 34;
    c.pin_d7 = 35;

    c.pin_xclk = 0;
    c.pin_pclk = 22;
    c.pin_vsync = 25;
    c.pin_href = 23;
    c.pin_sccb_sda = 26;
    c.pin_sccb_scl = 27;

    c.pin_pwdn = 32;
    c.pin_reset = -1;
    c.xclk_freq_hz = 20000000;

    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size = s_.frameSize;
    c.jpeg_quality = s_.jpegQuality;

    c.fb_count = psram_ ? 2 : 1;
    c.fb_location = psram_ ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;            // always the freshest frame, never a stale buffered one

    if(!psram_ && c.frame_size > FRAMESIZE_QVGA) {
        Serial.println("[cam] no PSRAM: falling back to QVGA");
        c.frame_size = FRAMESIZE_QVGA;
    }

    esp_err_t err = esp_camera_init(&c);
    if(err != ESP_OK) {
        Serial.printf("[cam] init failed 0x%x\n", err);
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if(sensor) {
        sensor->set_vflip(sensor, s_.vflip);
        sensor->set_hmirror(sensor, s_.hmirror);
    }

    if(s_.flash) {
        pinMode(kFlashPin, OUTPUT);
        digitalWrite(kFlashPin, LOW);
    }
    ready_ = true;
    return true;
}

void Camera::end() {
    if(ready_)
        esp_camera_deinit();
    ready_ = false;
}

void Camera::warmup(int frames) {
    for(int i = 0; i < frames; i++) {
        release(esp_camera_fb_get());
        delay(100);
    }
}

camera_fb_t *Camera::capture() {
    if(!ready_)
        return nullptr;
    if(s_.flash) {
        digitalWrite(kFlashPin, HIGH);
        delay(80);
        release(esp_camera_fb_get());            // the buffered frame was exposed without the LED
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if(s_.flash)
        digitalWrite(kFlashPin, LOW);
    return fb;
}

void Camera::release(camera_fb_t *fb) {
    if(fb)
        esp_camera_fb_return(fb);
}
