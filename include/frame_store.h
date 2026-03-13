#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>

// Thread-safe store for the latest JPEG frame.
// Main loop writes, HTTP handlers read.

class FrameStore {
public:
    bool begin() {
        _mutex = xSemaphoreCreateMutex();
        _buf = (uint8_t*)ps_malloc(64 * 1024);  // 64KB max JPEG
        return _mutex && _buf;
    }

    void update(const uint8_t* jpg, size_t len) {
        if (len > 64 * 1024) return;
        xSemaphoreTake(_mutex, portMAX_DELAY);
        memcpy(_buf, jpg, len);
        _len = len;
        _seq++;
        xSemaphoreGive(_mutex);
    }

    // Copy latest JPEG into caller's buffer. Returns size, 0 if none.
    size_t get(uint8_t* dst, size_t maxLen, uint32_t* seq = nullptr) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        size_t len = 0;
        if (_len > 0 && _len <= maxLen) {
            memcpy(dst, _buf, _len);
            len = _len;
        }
        if (seq) *seq = _seq;
        xSemaphoreGive(_mutex);
        return len;
    }

    uint32_t seq() {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        uint32_t s = _seq;
        xSemaphoreGive(_mutex);
        return s;
    }

private:
    SemaphoreHandle_t _mutex = nullptr;
    uint8_t* _buf = nullptr;
    size_t _len = 0;
    uint32_t _seq = 0;  // increments each update
};
