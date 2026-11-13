#pragma once
#include <Arduino.h>
#include <WiFi.h>

struct NetConfig {
    String ssid;
    String pass;
    String baseUrl;
    String hmacSecret;
    String deviceId;
};

struct HttpHeader {
    const char *name;
    String value;
};

/// Wi-Fi + NTP + signed HTTP.
/// Signature:  X-Signature = hex( HMAC_SHA256(secret, "<ts>.<deviceId>.<sha256hex(body)>") )
/// Headers:    X-Device-Id, X-Timestamp (unix s, 0 if NTP not synced yet), X-Signature
/// GET requests sign an empty body.
class NetClient {
public:
    void begin(const NetConfig &cfg);
    void tick();                                 // call ~1 Hz: reconnects a stuck link
    bool online() const;
    bool waitOnline(uint32_t timeoutMs) const;
    int rssi() const;
    uint32_t unixNow() const;                    // 0 until NTP synced
    bool timeSynced() const { return unixNow() != 0; }

    // Return HTTP status, or -1 when offline / transport error.
    int post(const String &path, const char *contentType, const uint8_t *body, size_t len, String *resp = nullptr,
             const HttpHeader *extra = nullptr, size_t extraCount = 0);
    int postJson(const String &path, const String &json, String *resp = nullptr);
    int get(const String &path, String *resp = nullptr);

private:
    int request(const char *method, const String &path, const char *contentType, const uint8_t *body, size_t len,
                String *resp, const HttpHeader *extra, size_t extraCount);

    NetConfig cfg_;
    uint32_t lastOkMs_ = 0;
    SemaphoreHandle_t mtx_ = nullptr;            // HTTPClient is not shared between tasks
};
