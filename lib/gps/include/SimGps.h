#pragma once
#ifdef USE_SIM_GPS
#include <string>
#include "GpsProvider.h"
#include "SimEngine.h"

/// Simulated GPS: route from /route.json, scenarios from /scenarios.json (LittleFS).
/// Falls back to a small built-in route/scenario when the files are missing or invalid,
/// so a freshly flashed board still moves.
class SimGps : public GpsProvider {
public:
    explicit SimGps(const char *initialScenario = "") : initial_(initialScenario ? initialScenario : "") {}

    bool begin() override;
    void update(uint32_t nowMs) override { engine_.update(nowMs); }
    GpsFix fix() const override;
    bool simulated() const override { return true; }

    /// Re-reads the JSON files (e.g. after a new route was downloaded).
    bool reload();

    SimEngine &engine() { return engine_; }
    const SimEngine &engine() const { return engine_; }
    bool usingFallback() const { return fallback_; }

private:
    SimEngine engine_;
    std::string initial_;
    bool fallback_ = false;
};

#endif // USE_SIM_GPS
