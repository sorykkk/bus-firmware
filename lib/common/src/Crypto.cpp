#include "Crypto.h"
#include <mbedtls/md.h>

namespace crypto {

String toHex(const uint8_t *d, size_t n) {
    static const char *h = "0123456789abcdef";
    String s;
    s.reserve(n * 2);
    for(size_t i = 0; i < n; i++) {
        s += h[d[i] >> 4];
        s += h[d[i] & 0xf];
    }
    return s;
}

String sha256Hex(const uint8_t *d, size_t n) {
    uint8_t out[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), d, n, out);
    return toHex(out, 32);
}

void hmacSha256(const char *key, const uint8_t *msg, size_t n, uint8_t out[32]) {
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t *)key, strlen(key), msg, n, out);
}

String hmacSha256Hex(const char *key, const String &msg) {
    uint8_t out[32];
    hmacSha256(key, (const uint8_t *)msg.c_str(), msg.length(), out);
    return toHex(out, 32);
}

void presenceToken(const char *secret, uint16_t busId, uint32_t slot, uint8_t out[16]) {
    char msg[24];
    int n = snprintf(msg, sizeof msg, "%u.%lu", busId, (unsigned long)slot);
    uint8_t mac[32];
    hmacSha256(secret, (const uint8_t *)msg, n, mac);
    memcpy(out, mac, 16);
}

} // namespace crypto
