// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Phase 1: Camera stream verification
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "camera.h"
#include "pins.h"

// --- WiFi credentials (change these) ---
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASS";

// Forward declaration from camera web server
void startCameraServer();

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== gol-cam starting ===");

    // Init LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Init camera — JPEG for web streaming
    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_JPEG)) {
        Serial.println("FATAL: Camera init failed!");
        while (true) delay(1000);
    }

    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWiFi failed — continuing without streaming");
    }

    // Start camera web server
    startCameraServer();
    Serial.printf("Camera stream ready at: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("=== gol-cam ready ===");
}

void loop() {
    delay(10000);
}
