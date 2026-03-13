// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Phase 3: Yellow dice (dadinho) detection via color + clustering
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "camera.h"
#include "pins.h"
#include "goal_detector.h"
#include "frame_store.h"

// --- WiFi credentials ---
const char* WIFI_SSID     = "cross.team-orl";
const char* WIFI_PASSWORD = "euamoovasco";

// Forward declarations
void startCameraServer();

// Globals
GoalDetector detector;
FrameStore frameStore;
volatile bool goalJustScored = false;

// Detection parameters
#define DETECT_W 320
#define DETECT_H 240

// Yellow color thresholds in RGB565 (loose — clustering filters the rest)
#define YELLOW_R_MIN 16   // out of 31
#define YELLOW_G_MIN 28   // out of 63
#define YELLOW_B_MAX 16   // out of 31

// Cluster checks: the dadinho is tiny, so yellow must be concentrated
#define YELLOW_PIXEL_MIN 10        // minimum yellow pixels
#define CLUSTER_MAX_SIZE 40        // bounding box can't exceed 40px in either dimension
                                   // (dadinho is ~5mm ≈ 10-25px depending on distance)
#define CLUSTER_DENSITY_MIN 0.15f  // at least 15% of bounding box must be yellow

#define COOLDOWN_MS 3000
#define STABLE_FRAMES_NEEDED 3

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== gol-cam starting ===");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    if (!frameStore.begin()) {
        Serial.println("FATAL: FrameStore init failed!");
        while (true) delay(1000);
    }

    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_RGB565)) {
        Serial.println("FATAL: Camera init failed!");
        while (true) delay(1000);
    }

    // Boost saturation for better yellow detection
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_saturation(s, 2);
        s->set_brightness(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
    }

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

    startCameraServer();
    Serial.printf("Camera stream: http://%s\n", WiFi.localIP().toString().c_str());

    detector.cooldownMs = COOLDOWN_MS;
    Serial.println("=== gol-cam ready — watching for dadinho! ===");
}

void loop() {
    static bool diceWasPresent = false;
    static uint32_t noDiceFrames = 0;
    static uint32_t lastGoalTime = 0;
    static uint32_t frameNum = 0;
    static uint32_t fpsCount = 0;
    static uint32_t lastFpsTime = millis();
    static uint32_t lastPrint = 0;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { delay(10); return; }

    frameNum++;
    fpsCount++;

    uint32_t now = millis();
    if (now - lastFpsTime >= 1000) {
        detector.fps = fpsCount;
        fpsCount = 0;
        lastFpsTime = now;
    }

    size_t expectedLen = DETECT_W * DETECT_H * 2;
    if (fb->format != PIXFORMAT_RGB565 || fb->len < expectedLen) {
        esp_camera_fb_return(fb);
        delay(100);
        return;
    }

    // Convert to JPEG for web stream
    uint8_t* jpg_buf = NULL;
    size_t jpg_len = 0;
    if (frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
        frameStore.update(jpg_buf, jpg_len);
        free(jpg_buf);
    }

    // Scan for yellow pixels + track bounding box
    uint16_t* pixels = (uint16_t*)fb->buf;
    int rx = DETECT_W / 10;
    int ry = DETECT_H / 10;
    int rw = DETECT_W * 8 / 10;
    int rh = DETECT_H * 8 / 10;

    uint32_t yellowCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);

            uint8_t r = (px >> 11) & 0x1F;
            uint8_t g = (px >> 5) & 0x3F;
            uint8_t b = px & 0x1F;

            if (r >= YELLOW_R_MIN && g >= YELLOW_G_MIN && b <= YELLOW_B_MAX
                && r > (b + 2)
                && (g >> 1) > b) {
                yellowCount++;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    esp_camera_fb_return(fb);

    // Cluster analysis: is the yellow concentrated like a tiny dice?
    int bboxW = (yellowCount > 0) ? (maxX - minX + 1) : 0;
    int bboxH = (yellowCount > 0) ? (maxY - minY + 1) : 0;
    float density = (bboxW > 0 && bboxH > 0) ? (float)yellowCount / (bboxW * bboxH) : 0;

    // Dice = enough yellow pixels, small bounding box, dense cluster
    bool diceDetected = (yellowCount >= YELLOW_PIXEL_MIN)
        && (bboxW <= CLUSTER_MAX_SIZE)
        && (bboxH <= CLUSTER_MAX_SIZE)
        && (density >= CLUSTER_DENSITY_MIN);

    detector.lastChangeRatio = (float)yellowCount / (rw * rh);
    detector.frameCount = frameNum;

    bool inCooldown = (now - lastGoalTime) < COOLDOWN_MS;

    // Log detection with cluster info
    static uint32_t lastYellowLog = 0;
    if (yellowCount > 0 && (now - lastYellowLog >= 500)) {
        Serial.printf("[yellow] %d px bbox=%dx%d dens=%.0f%%%s%s\n",
            yellowCount, bboxW, bboxH, density * 100,
            diceDetected ? " DICE!" : "",
            inCooldown ? " (cooldown)" : "");
        lastYellowLog = now;
    }

    // Goal: dice appears after being absent
    if (diceDetected && !diceWasPresent && !inCooldown && noDiceFrames > STABLE_FRAMES_NEEDED) {
        lastGoalTime = now;
        detector.goalCount++;
        goalJustScored = true;

        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);

        Serial.printf("GOAL #%d! (%d px, %dx%d)\n",
            detector.goalCount, yellowCount, bboxW, bboxH);
    }

    if (diceDetected) {
        diceWasPresent = true;
        noDiceFrames = 0;
    } else {
        noDiceFrames++;
        if (noDiceFrames > STABLE_FRAMES_NEEDED) {
            diceWasPresent = false;
        }
    }

    if (now - lastPrint >= 5000) {
        Serial.printf("[stats] fps:%d frames:%d goals:%d yellow:%d bbox=%dx%d armed:%s\n",
            detector.fps, frameNum, detector.goalCount,
            yellowCount, bboxW, bboxH, !diceWasPresent ? "yes" : "no");
        lastPrint = now;
    }

    delay(10);
}
