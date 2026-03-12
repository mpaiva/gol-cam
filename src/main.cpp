// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Phase 3: Pixel-level frame differencing (grayscale native)
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

// Forward declarations
void startCameraServer();

// Globals
GoalDetector detector;
volatile bool goalJustScored = false;

// Detection parameters
#define DETECT_W 320
#define DETECT_H 240
#define PIXEL_THRESHOLD 15      // per-pixel brightness change
#define CHANGE_THRESHOLD 0.03f  // 3% of ROI pixels must change
#define COOLDOWN_MS 3000
#define STABLE_FRAMES_NEEDED 3

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== gol-cam starting ===");

    // Init LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Init camera in GRAYSCALE for detection
    // We'll encode to JPEG for the web stream in the handler
    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_GRAYSCALE)) {
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

    // Configure detector
    detector.changeThreshold = CHANGE_THRESHOLD;
    detector.cooldownMs = COOLDOWN_MS;

    Serial.printf("Detection: pixThresh=%d, changeThresh=%.1f%%, cooldown=%dms\n",
        PIXEL_THRESHOLD, CHANGE_THRESHOLD * 100, COOLDOWN_MS);
    Serial.println("=== gol-cam ready — watching for goals! ===");
}

void loop() {
    static uint8_t* prevFrame = nullptr;
    static bool hasPrev = false;
    static uint32_t stableFrames = 0;
    static uint32_t lastGoalTime = 0;
    static uint32_t frameNum = 0;
    static uint32_t fpsCount = 0;
    static uint32_t lastFpsTime = millis();
    static uint32_t lastPrint = 0;

    // Allocate prev frame buffer once
    if (!prevFrame) {
        prevFrame = (uint8_t*)ps_malloc(DETECT_W * DETECT_H);
        if (!prevFrame) {
            Serial.println("FATAL: Failed to allocate prev frame");
            delay(5000);
            return;
        }
        memset(prevFrame, 0, DETECT_W * DETECT_H);
    }

    // Grab a frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        delay(10);
        return;
    }

    frameNum++;
    fpsCount++;

    uint32_t now = millis();
    if (now - lastFpsTime >= 1000) {
        detector.fps = fpsCount;
        fpsCount = 0;
        lastFpsTime = now;
    }

    // Verify we got a grayscale frame
    if (fb->format != PIXFORMAT_GRAYSCALE || fb->len < DETECT_W * DETECT_H) {
        Serial.printf("Unexpected frame: format=%d len=%d (expected %d)\n",
            fb->format, fb->len, DETECT_W * DETECT_H);
        esp_camera_fb_return(fb);
        delay(100);
        return;
    }

    if (!hasPrev) {
        memcpy(prevFrame, fb->buf, DETECT_W * DETECT_H);
        hasPrev = true;
        esp_camera_fb_return(fb);
        return;
    }

    // Frame differencing in ROI (center 80%)
    int rx = DETECT_W / 10;
    int ry = DETECT_H / 10;
    int rw = DETECT_W * 8 / 10;
    int rh = DETECT_H * 8 / 10;

    uint32_t changedPixels = 0;
    uint32_t totalPixels = 0;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            int idx = y * DETECT_W + x;
            int diff = abs((int)fb->buf[idx] - (int)prevFrame[idx]);
            if (diff > PIXEL_THRESHOLD) {
                changedPixels++;
            }
            totalPixels++;
        }
    }

    // Copy BEFORE returning the frame buffer
    memcpy(prevFrame, fb->buf, DETECT_W * DETECT_H);
    esp_camera_fb_return(fb);

    float changeRatio = (float)changedPixels / totalPixels;
    detector.lastChangeRatio = changeRatio;
    detector.frameCount = frameNum;

    bool inCooldown = (now - lastGoalTime) < COOLDOWN_MS;

    // Log any significant motion
    if (changeRatio > 0.02f) {
        Serial.printf("[detect] change: %.1f%% (%d/%d px)%s\n",
            changeRatio * 100, changedPixels, totalPixels,
            inCooldown ? " (cooldown)" : "");
    }

    // Goal detection
    if (changeRatio >= CHANGE_THRESHOLD && !inCooldown && stableFrames > STABLE_FRAMES_NEEDED) {
        lastGoalTime = now;
        detector.goalCount++;
        goalJustScored = true;
        stableFrames = 0;

        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);

        Serial.printf("GOAL #%d! (change: %.1f%%)\n",
            detector.goalCount, changeRatio * 100);
    } else if (changeRatio < 0.02f) {
        stableFrames++;
    } else {
        stableFrames = 0;
    }

    // Print stats every 5 seconds
    if (now - lastPrint >= 5000) {
        Serial.printf("[stats] fps:%d frames:%d goals:%d change:%.1f%% stable:%d\n",
            detector.fps, frameNum, detector.goalCount,
            changeRatio * 100, stableFrames);
        lastPrint = now;
    }

    delay(30);
}
