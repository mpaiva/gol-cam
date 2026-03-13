// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Phase 3: Yellow dice (dadinho) detection via color filtering
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

// Yellow color thresholds in RGB565
// RGB565: RRRRRGGG GGGBBBBB (R=5bit, G=6bit, B=5bit)
#define YELLOW_R_MIN 18   // out of 31 — high red
#define YELLOW_G_MIN 30   // out of 63 — high green
#define YELLOW_B_MAX 14   // out of 31 — low blue

// How many yellow pixels to trigger a goal
#define YELLOW_PIXEL_MIN 15       // minimum yellow pixels to count as "dice present"
#define COOLDOWN_MS 3000
#define STABLE_FRAMES_NEEDED 3    // frames without dice before re-arming

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== gol-cam starting ===");

    // Init LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Init frame store
    if (!frameStore.begin()) {
        Serial.println("FATAL: FrameStore init failed!");
        while (true) delay(1000);
    }

    // Init camera in RGB565 for color detection
    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_RGB565)) {
        Serial.println("FATAL: Camera init failed!");
        while (true) delay(1000);
    }

    // Boost saturation for better yellow detection
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_saturation(s, 2);   // max saturation for vivid yellows
        s->set_brightness(s, 0);
        s->set_whitebal(s, 1);     // auto white balance
        s->set_awb_gain(s, 1);
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
    detector.cooldownMs = COOLDOWN_MS;

    Serial.printf("Detection: yellow R>=%d G>=%d B<=%d, minPx=%d, cooldown=%dms\n",
        YELLOW_R_MIN, YELLOW_G_MIN, YELLOW_B_MAX, YELLOW_PIXEL_MIN, COOLDOWN_MS);
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

    // Grab a frame — ONLY the main loop calls esp_camera_fb_get()
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

    // Verify RGB565 frame
    size_t expectedLen = DETECT_W * DETECT_H * 2;  // 2 bytes per pixel
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

    // Scan for yellow pixels in ROI (center 80%)
    uint16_t* pixels = (uint16_t*)fb->buf;
    int rx = DETECT_W / 10;
    int ry = DETECT_H / 10;
    int rw = DETECT_W * 8 / 10;
    int rh = DETECT_H * 8 / 10;

    uint32_t yellowCount = 0;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            // RGB565 is big-endian from camera, swap bytes
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);  // byte swap

            uint8_t r = (px >> 11) & 0x1F;  // 0-31
            uint8_t g = (px >> 5) & 0x3F;   // 0-63
            uint8_t b = px & 0x1F;           // 0-31

            if (r >= YELLOW_R_MIN && g >= YELLOW_G_MIN && b <= YELLOW_B_MAX) {
                yellowCount++;
            }
        }
    }

    esp_camera_fb_return(fb);

    detector.lastChangeRatio = (float)yellowCount / (rw * rh);
    detector.frameCount = frameNum;

    bool dicePresent = yellowCount >= YELLOW_PIXEL_MIN;
    bool inCooldown = (now - lastGoalTime) < COOLDOWN_MS;

    // Log when yellow is detected
    if (yellowCount > 0) {
        Serial.printf("[yellow] %d px%s%s\n", yellowCount,
            dicePresent ? " DICE!" : "",
            inCooldown ? " (cooldown)" : "");
    }

    // Goal: dice appears after being absent
    if (dicePresent && !diceWasPresent && !inCooldown && noDiceFrames > STABLE_FRAMES_NEEDED) {
        lastGoalTime = now;
        detector.goalCount++;
        goalJustScored = true;

        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);

        Serial.printf("GOAL #%d! (%d yellow px)\n",
            detector.goalCount, yellowCount);
    }

    if (dicePresent) {
        diceWasPresent = true;
        noDiceFrames = 0;
    } else {
        noDiceFrames++;
        if (noDiceFrames > STABLE_FRAMES_NEEDED) {
            diceWasPresent = false;
        }
    }

    // Print stats every 5 seconds
    if (now - lastPrint >= 5000) {
        Serial.printf("[stats] fps:%d frames:%d goals:%d yellow:%d armed:%s\n",
            detector.fps, frameNum, detector.goalCount,
            yellowCount, !diceWasPresent ? "yes" : "no");
        lastPrint = now;
    }

    delay(10);
}
