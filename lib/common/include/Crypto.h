#pragma once
#include <Arduino.h>

namespace crypto {

String toHex(const uint8_t *d, size_t n);
String sha256Hex(const uint8_t *d, size_t n);
void hmacSha256(const char *key, const uint8_t *msg, size_t n, uint8_t out[32]);
String hmacSha256Hex(const char *key, const String &msg);

/// BLE proof-of-presence token: first 16 bytes of HMAC-SHA256(secret, "<busId>.<slot>").
/// Server side (Python): hmac.new(secret, f"{bus}.{slot}".encode(), sha256).digest()[:16]
void presenceToken(const char *secret, uint16_t busId, uint32_t slot, uint8_t out[16]);

} // namespace crypto
