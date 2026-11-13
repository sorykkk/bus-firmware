#pragma once
#include <Arduino.h>
#include "esp_camera.h"

/// AI-Thinker ESP32-CAM (as sold with the ESP32-CAM-MB USB board), OV2640 sensor.
struct CameraSettings {
    framesize_t frameSize = FRAMESIZE_SVGA;  // 800x600: enough for person counting, ~20-40 KB
    int jpegQuality = 12;                    // 10-63, lower = better quality / bigger file
    bool vflip = false;
    bool hmirror = false;
    bool flash = false;                      // GPIO 4 white LED during capture

    /// "QVGA", "VGA", "SVGA", "XGA", "HD", "SXGA", "UXGA" -> framesize_t (SVGA if unknown).
    static framesize_t parseFrameSize(const String &name);
};

class Camera {
public:
    static constexpr int kFlashPin = 4;

    bool begin(const CameraSettings &s = CameraSettings());
    void end();

    /// Throws away a few frames so auto-exposure / white balance settle.
    void warmup(int frames = 3);

    /// Caller must release() the buffer as soon as possible (there are only 1-2).
    camera_fb_t *capture();
    void release(camera_fb_t *fb);

    bool ready() const { return ready_; }
    bool hasPsram() const { return psram_; }

private:
    CameraSettings s_;
    bool ready_ = false;
    bool psram_ = false;
};
