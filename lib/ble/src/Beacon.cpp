#include "Beacon.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string>
#include "BeaconFrame.h"

namespace beacon {

static uint8_t s_token[beaconframe::kTokenLen] = {0};
static uint32_t s_slot = 0;
static bool s_synced = false;

static void applyScanResponse(NimBLEAdvertising *adv) {
    NimBLEAdvertisementData sr;
    if(s_synced) {
        uint8_t p[beaconframe::kPresenceLen];
        beaconframe::buildPresence(s_slot, s_token, true, p);
        sr.setManufacturerData(std::string((const char *)p, sizeof p));
    }
    adv->setScanResponse(s_synced);
    adv->setScanResponseData(sr);
}

static void refreshScanResponse() {
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    bool was = adv->isAdvertising();
    if(was)
        adv->stop();
    applyScanResponse(adv);
    if(was)
        adv->start();
}

bool begin(const Config &cfg) {
    uint8_t uuid[16];
    if(!beaconframe::parseUuid(cfg.systemUuid, uuid)) {
        Serial.println("[ble] bad system UUID");
        return false;
    }

    uint8_t d[beaconframe::kIBeaconLen];
    beaconframe::buildIBeacon(uuid, cfg.major, cfg.minor, cfg.txPowerAt1m, d);

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData ad;
    ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    ad.setManufacturerData(std::string((const char *)d, sizeof d));
    adv->setAdvertisementData(ad);
    applyScanResponse(adv);

    adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    uint16_t units = (uint16_t)(cfg.intervalMs / 0.625f);   // BLE unit = 0.625 ms
    adv->setMinInterval(units);
    adv->setMaxInterval(units + 80);                        // +50 ms spread to avoid collisions
    bool ok = adv->start();
    Serial.printf("[ble] iBeacon %s: major(route)=%u minor(bus)=%u non-connectable\n",
                  ok ? "on" : "FAILED", cfg.major, cfg.minor);
    return ok;
}

void updateToken(uint32_t slot, const uint8_t token[16], bool clockSynced) {
    if(!clockSynced) {
        clearToken();
        return;
    }
    s_slot = slot;
    memcpy(s_token, token, beaconframe::kTokenLen);
    s_synced = true;
    refreshScanResponse();
}

void clearToken() {
    memset(s_token, 0, sizeof s_token);
    s_slot = 0;
    if(!s_synced)
        return;
    s_synced = false;
    refreshScanResponse();
}

bool advertising() {
    return NimBLEDevice::getAdvertising()->isAdvertising();
}

} // namespace beacon
