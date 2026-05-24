#include "placar_display.h"
#include "pins_placar.h"

static const uint8_t DIGIT_FONT[10][8] = {
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
    {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00}, // 1
    {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}  // 9
};

PlacarDisplay::PlacarDisplay()
    : _dspA(PLACAR_HW_TYPE, PLACAR_DIN_A, PLACAR_CLK_A, PLACAR_CS_A, PLACAR_MAX_DEVICES),
      _dspB(PLACAR_HW_TYPE, PLACAR_DIN_B, PLACAR_CLK_B, PLACAR_CS_B, PLACAR_MAX_DEVICES) {}

void PlacarDisplay::begin() {
    _dspA.begin();
    _dspB.begin();
    setBrightness(PLACAR_BRIGHTNESS);
    clear();
}

void PlacarDisplay::setBrightness(uint8_t value) {
    if (value > 15) value = 15;
    _dspA.control(MD_MAX72XX::INTENSITY, value);
    _dspB.control(MD_MAX72XX::INTENSITY, value);
}

void PlacarDisplay::clear() {
    _dspA.clear();
    _dspB.clear();
}

void PlacarDisplay::clearModule(MD_MAX72XX& dsp, uint8_t module) {
    for (uint8_t row = 0; row < 8; row++) {
        dsp.setRow(module, row, 0x00);
    }
}

// Renders a digit rotated 90 degrees (module orientation differs from MD_MAX72XX default).
void PlacarDisplay::drawDigit90(MD_MAX72XX& dsp, uint8_t module, int digit) {
    if (digit < 0 || digit > 9) return;
    for (uint8_t row = 0; row < 8; row++) {
        uint8_t out = 0;
        for (uint8_t col = 0; col < 8; col++) {
            if (DIGIT_FONT[digit][7 - col] & (1 << (7 - row))) {
                out |= (1 << (7 - col));
            }
        }
        dsp.setRow(module, row, out);
    }
}

// Module 0 = tens (left), module 1 = units (right). Tens hidden when value < 10.
void PlacarDisplay::renderNumber(MD_MAX72XX& dsp, int value) {
    if (value < 0)  value = 0;
    if (value > 99) value = 99;
    int tens  = value / 10;
    int units = value % 10;
    if (value < 10) clearModule(dsp, 0);
    else            drawDigit90(dsp, 0, tens);
    drawDigit90(dsp, 1, units);
}

void PlacarDisplay::showA(int value) { renderNumber(_dspA, value); }
void PlacarDisplay::showB(int value) { renderNumber(_dspB, value); }
