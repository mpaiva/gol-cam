// =============================================================
// gol-cam — Scoreboard firmware (ESP32 DevKit V1)
//
// Drives 4x MAX7219 8x8 LED matrices (2 per side) showing the
// match score, listens for goals from the camera board over WiFi,
// and exposes 4 physical buttons for manual increment/reset.
//
// STUB — full implementation in Phase 5 of the consolidation plan.
// =============================================================

#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("placar: boot (stub — Phase 5 will implement)");
}

void loop() {
    delay(1000);
}
