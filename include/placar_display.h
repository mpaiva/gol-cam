#pragma once

#include <Arduino.h>
#include <MD_MAX72xx.h>

// =============================================================
// PlacarDisplay — drives two pairs of MAX7219 8x8 modules (sides A and B)
// Each side shows a 2-digit decimal number (0..99) rotated 90 degrees.
// =============================================================

class PlacarDisplay {
public:
    PlacarDisplay();

    void begin();
    void setBrightness(uint8_t value);   // 0..15
    void showA(int value);               // clamps to 0..99
    void showB(int value);               // clamps to 0..99
    void clear();

private:
    MD_MAX72XX _dspA;
    MD_MAX72XX _dspB;

    void drawDigit90(MD_MAX72XX& dsp, uint8_t module, int digit);
    void clearModule(MD_MAX72XX& dsp, uint8_t module);
    void renderNumber(MD_MAX72XX& dsp, int value);
};
