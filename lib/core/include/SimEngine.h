#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "SimScenario.h"

struct SimFix {
    double lat = 0, lon = 0;
    float speedKmh = 0;
    float headingDeg = 0;
    bool fixOk = false;
    int16_t stopIndex = -1;      // stop the bus is standing at, else -1
    int16_t lastStopIndex = -1;  // last stop visited (or standing at)
};

/// Drives a virtual bus along a Route following the active Scenario:
/// accelerates to cruise speed, respects slow zones, brakes into each stop, dwells, repeats.
/// Time is injected through update(nowMs), so it is fully deterministic and unit-testable.
class SimEngine {
public:
    enum class State : uint8_t { Idle, Dwelling, Driving, Paused, Finished };

    /// Returns false if the route has fewer than 2 points or the scenario set is empty.
    bool load(const Route &route, const ScenarioSet &scenarios);

    bool setScenario(size_t index);
    bool setScenario(const std::string &name);
    void nextScenario();
    size_t scenarioIndex() const { return scn_; }
    size_t scenarioCount() const { return set_.scenarios.size(); }
    const char *scenarioName() const { return cur_.name.c_str(); }
    const char *scenarioNameAt(size_t i) const;

    void start();
    void pause();
    void toggle();
    void reset();
    void update(uint32_t nowMs);

    SimFix fix() const;
    State state() const { return state_; }
    const char *stateName() const;
    float progressM() const { return pos_; }
    float totalM() const { return total_; }
    size_t stopCount() const { return stops_.size(); }
    const char *stopName(int index) const;
    const Route &route() const { return route_; }

    void setCruiseKmh(float kmh);
    float cruiseKmh() const { return cur_.cruiseKmh; }
    void setTimeScale(float scale);
    float timeScale() const { return cur_.timeScale; }

private:
    struct Stop {
        float atM;
        std::string name;
    };

    void step(float dt);
    void arrive(int stop, bool wrapped);
    bool nextStop(float from, int &stop, float &dist) const;
    bool skipped(size_t stop) const;
    float speedLimitMps(float pos) const;
    bool inDropout(float pos) const;
    void positionAt(float pos, double &lat, double &lon, float &heading) const;
    float nextRandom();   // [-1, 1]

    Route route_;
    ScenarioSet set_;
    size_t scn_ = 0;
    Scenario cur_;
    std::vector<float> cum_;
    std::vector<Stop> stops_;
    bool closed_ = false;
    float total_ = 0;

    State state_ = State::Idle, resume_ = State::Idle;
    float pos_ = 0, v_ = 0, dwellLeft_ = 0;
    int16_t atStop_ = -1, lastStop_ = -1;
    uint32_t lastMs_ = 0;
    bool haveLast_ = false;
    uint32_t rng_ = 0x2545F491;
    float jitN_ = 0, jitE_ = 0, jitterClock_ = 0;
    double goodLat_ = 0, goodLon_ = 0;
};
