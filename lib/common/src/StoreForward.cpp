#include "StoreForward.h"

StoreForward::StoreForward(size_t cap) : cap_(cap) {
    mtx_ = xSemaphoreCreateMutex();
}

void StoreForward::push(const String &path, const String &body) {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    if(q_.size() >= cap_)
        q_.pop_front();
    q_.push_back(Msg{path, body});
    xSemaphoreGive(mtx_);
}

bool StoreForward::peek(Msg &out) {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    bool ok = !q_.empty();
    if(ok)
        out = q_.front();
    xSemaphoreGive(mtx_);
    return ok;
}

void StoreForward::pop() {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    if(!q_.empty())
        q_.pop_front();
    xSemaphoreGive(mtx_);
}

size_t StoreForward::size() {
    xSemaphoreTake(mtx_, portMAX_DELAY);
    size_t n = q_.size();
    xSemaphoreGive(mtx_);
    return n;
}
