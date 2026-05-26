#pragma once

#include <Arduino.h>

// =============================================================
// PlacarButtons — debounced edge-trigger reader for 4 push buttons
// Calls registered callbacks on falling edge (INPUT_PULLUP).
// =============================================================

class PlacarButtons {
public:
    using Callback = void (*)();

    void begin(Callback onUpA, Callback onDownA, Callback onUpB, Callback onDownB);
    void poll();   // call from loop()

private:
    static const uint32_t DEBOUNCE_MS = 120;
    struct Btn {
        uint8_t pin;
        bool    lastState;
        uint32_t lastEdgeMs;
        Callback cb;
    };
    Btn _btns[4];
};
