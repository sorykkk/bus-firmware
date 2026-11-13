#include "NetClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "Crypto.h"

void NetClient::begin(const NetConfig &cfg) {
    cfg_ = cfg;
    if(!mtx_)
        mtx_ = xSemaphoreCreateMutex();
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg_.ssid.c_str(), cfg_.pass.c_str());
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    lastOkMs_ = millis();
}

void NetClient::tick() {
    if(online()) {
        lastOkMs_ = millis();
        return;
    }
    // stuck for 15 s: hard reconnect
    if(millis() - lastOkMs_ > 15000) {
        Serial.println("[net] Wi-Fi down for 15 s, reconnecting");
        WiFi.disconnect();
        WiFi.begin(cfg_.ssid.c_str(), cfg_.pass.c_str());
        lastOkMs_ = millis();
    }
}

bool NetClient::online() const {
    return WiFi.status() == WL_CONNECTED;
}

bool NetClient::waitOnline(uint32_t timeoutMs) const {
    uint32_t t0 = millis();
    while(!online() && millis() - t0 < timeoutMs)
        delay(100);
    return online();
}

int NetClient::rssi() const {
    return online() ? WiFi.RSSI() : 0;
}

uint32_t NetClient::unixNow() const {
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

int NetClient::request(const char *method, const String &path, const char *ctype, const uint8_t *body, size_t len,
                       String *resp, const HttpHeader *extra, size_t extraCount) {
    if(!online() || !mtx_)
        return -1;

    String url = cfg_.baseUrl + path;
    String ts = String(unixNow());
    String sig = crypto::hmacSha256Hex(cfg_.hmacSecret.c_str(),
                                       ts + "." + cfg_.deviceId + "." + crypto::sha256Hex(body, len));

    xSemaphoreTake(mtx_, portMAX_DELAY);
    HTTPClient http;
    WiFiClient plain;
    WiFiClientSecure tls;
    bool ok;
    if(url.startsWith("https://")) {
        tls.setInsecure();                       // hackathon: no certificate pinning
        ok = http.begin(tls, url);
    } else {
        ok = http.begin(plain, url);
    }
    if(!ok) {
        xSemaphoreGive(mtx_);
        return -1;
    }

    http.setTimeout(4000);
    if(ctype)
        http.addHeader("Content-Type", ctype);
    http.addHeader("X-Device-Id", cfg_.deviceId);
    http.addHeader("X-Timestamp", ts);
    http.addHeader("X-Signature", sig);
    for(size_t i = 0; i < extraCount; i++)
        http.addHeader(extra[i].name, extra[i].value);

    int code = http.sendRequest(method, (uint8_t *)body, len);
    if(resp && code > 0)
        *resp = http.getString();
    http.end();
    xSemaphoreGive(mtx_);
    return code > 0 ? code : -1;
}

int NetClient::post(const String &path, const char *ctype, const uint8_t *body, size_t len, String *resp,
                    const HttpHeader *extra, size_t extraCount) {
    return request("POST", path, ctype, body, len, resp, extra, extraCount);
}

int NetClient::postJson(const String &path, const String &json, String *resp) {
    return post(path, "application/json", (const uint8_t *)json.c_str(), json.length(), resp);
}

int NetClient::get(const String &path, String *resp) {
    return request("GET", path, nullptr, nullptr, 0, resp, nullptr, 0);
}
