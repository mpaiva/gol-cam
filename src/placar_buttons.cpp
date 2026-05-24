#include "placar_buttons.h"
#include "pins_placar.h"

void PlacarButtons::begin(Callback onUpA, Callback onDownA, Callback onUpB, Callback onDownB) {
    _btns[0] = {PLACAR_BTN_UP_A, HIGH, 0, onUpA};
    _btns[1] = {PLACAR_BTN_DW_A, HIGH, 0, onDownA};
    _btns[2] = {PLACAR_BTN_UP_B, HIGH, 0, onUpB};
    _btns[3] = {PLACAR_BTN_DW_B, HIGH, 0, onDownB};
    for (auto& b : _btns) {
        pinMode(b.pin, INPUT_PULLUP);
    }
}

void PlacarButtons::poll() {
    uint32_t now = millis();
    for (auto& b : _btns) {
        bool state = digitalRead(b.pin);
        if (b.lastState == HIGH && state == LOW && (now - b.lastEdgeMs) > DEBOUNCE_MS) {
            b.lastEdgeMs = now;
            if (b.cb) b.cb();
        }
        b.lastState = state;
    }
}
