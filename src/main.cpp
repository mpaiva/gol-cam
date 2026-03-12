// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Phase 3: Frame differencing ball detection
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "camera.h"
#include "pins.h"
#include "goal_detector.h"

// --- WiFi credentials ---
const char* WIFI_SSID     = "cross.team-orl";
const char* WIFI_PASSWORD = "euamoovasco";

// --- Detection config ---
#define DETECT_WIDTH   320  // QVGA
#define DETECT_HEIGHT  240

// Forward declarations
void startCameraServer();

// Globals
GoalDetector detector;
volatile bool goalJustScored = false;

// Detection runs on core 0, web server runs on core 1
void detectionTask(void* param) {
    // Init a second camera session for grayscale detection frames
    // We'll grab frames directly — the camera is shared but fb_count=2 helps
    Serial.println("Detection task started on core " + String(xPortGetCoreID()));

    while (true) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            delay(10);
            continue;
        }

        // If JPEG (for web streaming), we need to decode to grayscale
        // Instead, let's work with the raw JPEG size as a simple motion proxy
        // OR we can switch pixel format — but that breaks the JPEG stream
        //
        // Strategy: Use JPEG frame size changes as a motion indicator.
        // When the ball enters, the scene changes → JPEG size changes significantly.
        // This avoids needing a separate grayscale capture.

        // For now, use a simpler approach: convert JPEG to grayscale in RAM
        // The ESP32-S3 with 8MB PSRAM can handle this

        // Actually, let's use a dual approach:
        // The web stream uses JPEG, but we periodically grab a frame,
        // decode it, and run detection on the grayscale version.

        // fmt2rgb888 then convert to gray would work but is expensive.
        // Simpler: just use JPEG file size delta as motion proxy.

        static size_t prevSize = 0;
        static uint32_t stableCount = 0;
        static uint32_t lastGoalTime = 0;
        static uint32_t frameNum = 0;
        static uint32_t fpsCount = 0;
        static uint32_t lastFpsTime = millis();

        frameNum++;
        fpsCount++;

        uint32_t now = millis();
        if (now - lastFpsTime >= 1000) {
            detector.fps = fpsCount;
            fpsCount = 0;
            lastFpsTime = now;
        }

        if (prevSize > 0) {
            float sizeChange = abs((int)fb->len - (int)prevSize) / (float)prevSize;
            detector.lastChangeRatio = sizeChange;

            bool inCooldown = (now - lastGoalTime) < detector.cooldownMs;

            // JPEG size changes significantly when scene content changes
            // A ball entering the goal causes a noticeable size shift
            if (sizeChange > detector.changeThreshold && !inCooldown && stableCount > 5) {
                lastGoalTime = now;
                detector.goalCount++;
                goalJustScored = true;
                stableCount = 0;

                // Flash IR LED
                digitalWrite(LED_PIN, HIGH);
                delay(200);
                digitalWrite(LED_PIN, LOW);

                Serial.printf("⚽ GOAL #%d! (JPEG size change: %.1f%%)\n",
                    detector.goalCount, sizeChange * 100);
            } else if (sizeChange < 0.02f) {
                // Scene is stable
                stableCount++;
            } else {
                stableCount = 0;
            }
        }

        prevSize = fb->len;
        detector.frameCount = frameNum;
        esp_camera_fb_return(fb);

        delay(50); // ~20 fps for detection
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== gol-cam starting ===");

    // Init LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Init camera — JPEG for web streaming + detection
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
    Serial.printf("Camera stream: http://%s\n", WiFi.localIP().toString().c_str());

    // Start detection task on core 0
    xTaskCreatePinnedToCore(detectionTask, "detect", 4096, NULL, 1, NULL, 0);

    Serial.println("=== gol-cam ready — watching for goals! ===");
}

void loop() {
    // Print stats every 5 seconds
    static uint32_t lastPrint = 0;
    uint32_t now = millis();
    if (now - lastPrint >= 5000) {
        Serial.printf("[stats] fps:%d frames:%d goals:%d change:%.1f%%\n",
            detector.fps, detector.frameCount, detector.goalCount,
            detector.lastChangeRatio * 100);
        lastPrint = now;
    }
    delay(100);
}
