#pragma once
#include <Arduino.h>
#include <deque>

/// Tiny thread-safe FIFO of outgoing JSON messages. When Wi-Fi drops, entries pile up
/// (oldest dropped past `cap`) and are replayed in order once the link is back.
/// The server de-duplicates telemetry on (device_id, boot_id, seq). boot_id changes every boot.
class StoreForward {
public:
    struct Msg {
        String path;
        String body;
    };

    explicit StoreForward(size_t cap = 60);
    void push(const String &path, const String &body);
    bool peek(Msg &out);
    void pop();
    size_t size();

private:
    std::deque<Msg> q_;
    size_t cap_;
    SemaphoreHandle_t mtx_;
};
