#pragma once
#include <stddef.h>
#include <stdint.h>

/// Byte-exact BLE advertising payloads shared with the Android app team.
///
/// Advertising packet (31 B max):  flags | manufacturer data = iBeacon
///   4C 00 | 02 15 | system UUID (16, big-endian) | major = route (BE) | minor = bus (BE) | txPower@1m
///
/// Scan response (31 B max): manufacturer data, 24 B payload + 2 B AD header
///   FF FF | ver=02 | flags (bit0: clock synced) | slot (u32 BE) | token (16 B)
///
/// `token` = first 16 bytes of HMAC-SHA256(device secret, "<bus_id>.<slot>") with
/// slot = unix_time / slot_seconds. The app forwards (bus, slot, token) to the server, which
/// recomputes it: a phone can only produce a valid token if it really heard this bus recently.
namespace beaconframe {

constexpr uint16_t kAppleCompanyId = 0x004C;
constexpr uint16_t kTestCompanyId = 0xFFFF;     // Bluetooth SIG "reserved for testing"
constexpr uint8_t kPresenceVersion = 0x02;
constexpr uint8_t kFlagClockSynced = 0x01;

constexpr size_t kIBeaconLen = 25;
constexpr size_t kTokenLen = 16;
constexpr size_t kPresenceLen = 8 + kTokenLen;

/// Accepts "d22b117c-2f6e-4d74-9e9f-6f09213c3dd0" (dashes optional, any case).
bool parseUuid(const char *s, uint8_t out[16]);

void buildIBeacon(const uint8_t uuid[16], uint16_t major, uint16_t minor, int8_t txPowerAt1m,
                  uint8_t out[kIBeaconLen]);

void buildPresence(uint32_t slot, const uint8_t token[kTokenLen], bool clockSynced, uint8_t out[kPresenceLen]);

bool parsePresence(const uint8_t *d, size_t n, uint32_t &slot, uint8_t token[kTokenLen], bool &clockSynced);

inline uint32_t tokenSlot(uint32_t unixS, uint16_t slotSeconds) {
    return unixS / (slotSeconds ? slotSeconds : 1);
}

} // namespace beaconframe
