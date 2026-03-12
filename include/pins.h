#pragma once

// =============================================================
// DFR1154 ESP32-S3 AI Camera — Pin Definitions
// Source: https://github.com/DFRobot/DFR1154_Examples
// =============================================================

// --- Camera (OV3660) ---
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     5
#define CAM_PIN_SIOD     8   // SCCB I2C SDA
#define CAM_PIN_SIOC     9   // SCCB I2C SCL
#define CAM_PIN_D7       4
#define CAM_PIN_D6       6
#define CAM_PIN_D5       7
#define CAM_PIN_D4      14
#define CAM_PIN_D3      17
#define CAM_PIN_D2      21
#define CAM_PIN_D1      18
#define CAM_PIN_D0      16
#define CAM_PIN_VSYNC    1
#define CAM_PIN_HREF     2
#define CAM_PIN_PCLK    15

// --- Audio: I2S PDM Microphone ---
#define MIC_CLOCK_PIN   38
#define MIC_DATA_PIN    39

// --- Audio: MAX98357 Speaker Amplifier (I2S) ---
#define SPK_BCLK_PIN    45
#define SPK_LRC_PIN     46
#define SPK_DOUT_PIN    42

// --- IR LED / Flash ---
#define LED_PIN         47

// --- Status LED ---
#define STATUS_LED_PIN   3

// --- SD Card (SPI) ---
#define SD_CS_PIN       10
#define SD_MOSI_PIN     11
#define SD_SCK_PIN      12
#define SD_MISO_PIN     13
