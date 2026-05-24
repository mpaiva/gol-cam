#pragma once

// =============================================================
// Placar Eletronico — Pin Definitions (ESP32 DevKit V1)
// Source: legacy/Placar_Eletronico_Wi-Fi.ino
// =============================================================

// --- Display A (2 modules MAX7219, chained) ---
#define PLACAR_DIN_A     23
#define PLACAR_CLK_A     18
#define PLACAR_CS_A       5

// --- Display B (2 modules MAX7219, chained) ---
#define PLACAR_DIN_B     19
#define PLACAR_CLK_B     21
#define PLACAR_CS_B      22

// --- Buttons (INPUT_PULLUP, active LOW) ---
#define PLACAR_BTN_UP_A  32
#define PLACAR_BTN_DW_A  33
#define PLACAR_BTN_UP_B  25
#define PLACAR_BTN_DW_B  26

// --- Display config ---
#define PLACAR_HW_TYPE       MD_MAX72XX::FC16_HW
#define PLACAR_MAX_DEVICES   2     // 2 modules per side (tens + units)
#define PLACAR_BRIGHTNESS    3     // 0..15
