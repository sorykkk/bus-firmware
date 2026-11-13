#include "SimEngine.h"
#include <algorithm>
#include <cmath>
#include "Geo.h"

static constexpr float kClosedRouteToleranceM = 15.0f;
static constexpr float kStepS = 0.2f;           // integration step (simulated seconds)
static constexpr float kMaxCatchUpS = 120.0f;   // ignore huge gaps (e.g. after a debugger pause)
static constexpr float kCreepMps = 0.5f;        // last metres before a stop
static constexpr float kArriveM = 0.05f;        // a stop closer than this counts as reached, not ahead
static constexpr float kNoLimit = 1e9f;

bool SimEngine::load(const Route &route, const ScenarioSet &scenarios) {
    if(route.points.size() < 2 || scenarios.scenarios.empty())
        return false;
    route_ = route;
    set_ = scenarios;

    const auto &p = route_.points;
    cum_.assign(p.size(), 0.0f);
    for(size_t i = 1; i < p.size(); i++)
        cum_[i] = cum_[i - 1] + (float)geo::distanceM(p[i - 1].lat, p[i - 1].lon, p[i].lat, p[i].lon);
    total_ = cum_.back();
    closed_ = p.size() > 2 &&
              geo::distanceM(p.front().lat, p.front().lon, p.back().lat, p.back().lon) < kClosedRouteToleranceM;

    stops_.clear();
    for(size_t i = 0; i < p.size(); i++) {
        if(p[i].name.empty())
            continue;
        if(closed_ && i == p.size() - 1 && !stops_.empty() && stops_.front().atM < 1.0f)
            continue;           // closing point repeats the first stop
        stops_.push_back(Stop{cum_[i], p[i].name});
    }

    int idx = set_.indexOf(set_.defaultName);
    return setScenario(idx >= 0 ? (size_t)idx : 0);
}

bool SimEngine::setScenario(size_t index) {
    if(index >= set_.scenarios.size())
        return false;
    scn_ = index;
    cur_ = set_.scenarios[index];
    reset();
    return true;
}

bool SimEngine::setScenario(const std::string &name) {
    int i = set_.indexOf(name);
    return i >= 0 && setScenario((size_t)i);
}

void SimEngine::nextScenario() {
    if(!set_.scenarios.empty())
        setScenario((scn_ + 1) % set_.scenarios.size());
}

const char *SimEngine::scenarioNameAt(size_t i) const {
    return i < set_.scenarios.size() ? set_.scenarios[i].name.c_str() : "";
}

void SimEngine::reset() {
    v_ = 0;
    dwellLeft_ = 0;
    pos_ = 0;
    atStop_ = lastStop_ = -1;
    haveLast_ = false;
    jitN_ = jitE_ = jitterClock_ = 0;

    if(!stops_.empty()) {
        int s = std::max(0, std::min(cur_.startStop, (int)stops_.size() - 1));
        pos_ = stops_[s].atM;
        atStop_ = lastStop_ = (int16_t)s;
        dwellLeft_ = cur_.dwellS;
    }
    state_ = !cur_.autostart ? State::Idle : (atStop_ >= 0 ? State::Dwelling : State::Driving);

    float hdg;
    positionAt(pos_, goodLat_, goodLon_, hdg);
}

void SimEngine::start() {
    if(state_ == State::Finished) {
        reset();
        state_ = atStop_ >= 0 ? State::Dwelling : State::Driving;
    } else if(state_ == State::Idle) {
        state_ = atStop_ >= 0 ? State::Dwelling : State::Driving;
    } else if(state_ == State::Paused) {
        state_ = resume_;
    }
    haveLast_ = false;
}

void SimEngine::pause() {
    if(state_ == State::Dwelling || state_ == State::Driving) {
        resume_ = state_;
        state_ = State::Paused;
    }
}

void SimEngine::toggle() {
    if(state_ == State::Dwelling || state_ == State::Driving)
        pause();
    else
        start();
}

void SimEngine::setCruiseKmh(float kmh) {
    if(kmh > 0 && kmh <= 120)
        cur_.cruiseKmh = kmh;
}

void SimEngine::setTimeScale(float scale) {
    if(scale > 0 && scale <= 100)
        cur_.timeScale = scale;
}

const char *SimEngine::stateName() const {
    switch(state_) {
        case State::Idle:     return "idle";
        case State::Dwelling: return "at_stop";
        case State::Driving:  return "driving";
        case State::Paused:   return "paused";
        case State::Finished: return "finished";
    }
    return "?";
}

const char *SimEngine::stopName(int index) const {
    return (index >= 0 && (size_t)index < stops_.size()) ? stops_[index].name.c_str() : "-";
}

void SimEngine::update(uint32_t nowMs) {
    if(!haveLast_) {
        lastMs_ = nowMs;
        haveLast_ = true;
        return;
    }
    float dt = (uint32_t)(nowMs - lastMs_) / 1000.0f * cur_.timeScale;
    lastMs_ = nowMs;
    dt = std::min(dt, kMaxCatchUpS);

    jitterClock_ += dt;
    if(cur_.gpsJitterM > 0 && jitterClock_ >= 1.0f) {
        jitterClock_ = 0;
        jitN_ = nextRandom() * cur_.gpsJitterM;
        jitE_ = nextRandom() * cur_.gpsJitterM;
    }

    while(dt > 0) {
        float h = std::min(dt, kStepS);
        step(h);
        dt -= h;
    }

    if(!inDropout(pos_)) {
        float hdg;
        positionAt(pos_, goodLat_, goodLon_, hdg);
    }
}

void SimEngine::step(float dt) {
    if(state_ == State::Dwelling) {
        v_ = 0;
        dwellLeft_ -= dt;
        if(dwellLeft_ <= 0) {
            state_ = State::Driving;
            atStop_ = -1;
        }
        return;
    }
    if(state_ != State::Driving)
        return;

    int stop = -1;
    float remaining;
    if(!nextStop(pos_, stop, remaining))
        remaining = closed_ ? kNoLimit : total_ - pos_;   // open route: drive to the terminus

    float limit = std::min(cur_.cruiseKmh / 3.6f, speedLimitMps(pos_));
    if(v_ < limit)
        v_ = std::min(limit, v_ + cur_.accelMps2 * dt);
    else
        v_ = std::max(limit, v_ - cur_.decelMps2 * dt);
    v_ = std::min(v_, std::sqrt(2.0f * cur_.decelMps2 * std::max(0.0f, remaining)));

    float adv = std::max(v_, remaining < 2.0f ? kCreepMps : 0.0f) * dt;
    if(adv >= remaining - kArriveM) {
        pos_ += remaining;
        bool wrapped = false;
        if(closed_ && pos_ >= total_ - 0.01f) {
            pos_ = std::max(0.0f, pos_ - total_);
            wrapped = true;
        }
        arrive(stop, wrapped);
        return;
    }

    pos_ += adv;
    if(closed_ && pos_ >= total_) {
        pos_ -= total_;
        if(!cur_.loop) {
            pos_ = 0;
            v_ = 0;
            state_ = State::Finished;
        }
    }
}

void SimEngine::arrive(int stop, bool wrapped) {
    v_ = 0;
    if(stop < 0) {                              // terminus of an open route
        if(!cur_.loop) {
            state_ = State::Finished;
            return;
        }
        pos_ = 0;
        atStop_ = -1;
        state_ = State::Driving;
        if(!stops_.empty() && stops_[0].atM < 1.0f) {
            atStop_ = lastStop_ = 0;
            dwellLeft_ = cur_.dwellS;
            state_ = State::Dwelling;
        }
        return;
    }
    atStop_ = lastStop_ = (int16_t)stop;
    if(wrapped && !cur_.loop) {
        state_ = State::Finished;
        return;
    }
    dwellLeft_ = cur_.dwellS;
    state_ = State::Dwelling;
}

bool SimEngine::nextStop(float from, int &stop, float &dist) const {
    for(size_t i = 0; i < stops_.size(); i++) {
        if(!skipped(i) && stops_[i].atM > from + kArriveM) {
            stop = (int)i;
            dist = stops_[i].atM - from;
            return true;
        }
    }
    if(closed_) {
        for(size_t i = 0; i < stops_.size(); i++) {
            if(!skipped(i)) {
                stop = (int)i;
                dist = total_ - from + stops_[i].atM;
                return true;
            }
        }
    }
    return false;
}

bool SimEngine::skipped(size_t stop) const {
    const auto &skip = cur_.skipStops;
    return std::find(skip.begin(), skip.end(), stops_[stop].name) != skip.end();
}

float SimEngine::speedLimitMps(float pos) const {
    float lim = kNoLimit;
    for(const auto &z : cur_.slowZones)
        if(pos >= z.fromM && pos <= z.toM)
            lim = std::min(lim, z.kmh / 3.6f);
    return lim;
}

bool SimEngine::inDropout(float pos) const {
    for(const auto &r : cur_.gpsDropouts)
        if(pos >= r.fromM && pos <= r.toM)
            return true;
    return false;
}

void SimEngine::positionAt(float pos, double &lat, double &lon, float &heading) const {
    const auto &p = route_.points;
    if(p.size() < 2) {
        lat = lon = 0;
        heading = 0;
        return;
    }
    size_t i = 0;
    while(i + 2 < p.size() && pos > cum_[i + 1])
        i++;
    float seg = cum_[i + 1] - cum_[i];
    double t = seg > 0 ? std::max(0.0f, std::min(1.0f, (pos - cum_[i]) / seg)) : 0.0;
    lat = p[i].lat + (p[i + 1].lat - p[i].lat) * t;
    lon = p[i].lon + (p[i + 1].lon - p[i].lon) * t;
    heading = (float)geo::bearingDeg(p[i].lat, p[i].lon, p[i + 1].lat, p[i + 1].lon);
}

SimFix SimEngine::fix() const {
    SimFix f;
    double lat, lon;
    positionAt(pos_, lat, lon, f.headingDeg);
    f.fixOk = route_.points.size() >= 2 && !inDropout(pos_);
    if(f.fixOk) {
        if(cur_.gpsJitterM > 0)
            geo::offsetM(lat, lon, jitN_, jitE_);
        f.lat = lat;
        f.lon = lon;
    } else {
        f.lat = goodLat_;
        f.lon = goodLon_;
    }
    f.speedKmh = state_ == State::Driving ? v_ * 3.6f : 0.0f;
    f.stopIndex = atStop_;
    f.lastStopIndex = lastStop_;
    return f;
}

float SimEngine::nextRandom() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (float)(rng_ & 0xFFFF) / 32767.5f - 1.0f;
}
