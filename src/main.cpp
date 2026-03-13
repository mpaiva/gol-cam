// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Calibration-based yellow dice (dadinho) detection
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

#define DETECT_W 320
#define DETECT_H 240

// Game states
enum GameState { STATE_IDLE, STATE_CALIBRATING, STATE_PLAYING, STATE_PAUSED };
volatile GameState gameState = STATE_IDLE;

// Calibrated color (set during calibration)
volatile int16_t calR = -1, calG = -1, calB = -1;  // -1 = not calibrated
volatile int16_t calTolR = 5, calTolG = 10, calTolB = 5;  // tolerance per channel
volatile int calPixelCount = 0;  // how many pixels the dice was during calibration
volatile int calBboxW = 0, calBboxH = 0;

// Detection params
#define COOLDOWN_MS 10000
#define STABLE_FRAMES_NEEDED 3

// Shared detection state for web console
volatile int lastMatchCount = 0, lastBboxW = 0, lastBboxH = 0;
volatile int lastMinPx = 0, lastMaxPx = 0, lastMaxBbox = 0;
volatile float lastDensity = 0;
volatile const char* lastRejectReason = "";

// Calibration snapshot: JPEG of the frame with detected object highlighted
uint8_t* calSnapshotBuf = nullptr;
size_t calSnapshotLen = 0;
SemaphoreHandle_t calSnapshotMutex = nullptr;
char calFeedback[256] = "";  // calibration feedback message

// Goal snapshot: JPEG of the frame when a goal was scored
uint8_t* goalSnapshotBuf = nullptr;
size_t goalSnapshotLen = 0;
SemaphoreHandle_t goalSnapshotMutex = nullptr;
volatile uint32_t goalSnapshotSeq = 0;  // increments each goal, so browser knows when new one is ready

// Called from HTTP handler to trigger calibration
void requestCalibration() {
    gameState = STATE_CALIBRATING;
    Serial.println("[cal] Calibration requested");
}

void requestStart() {
    if (calR >= 0) {
        gameState = STATE_PLAYING;
        detector.goalCount = 0;
        goalJustScored = false;
        Serial.println("[game] Game started!");
    }
}

void requestPause() {
    if (gameState == STATE_PLAYING) {
        gameState = STATE_PAUSED;
        Serial.println("[game] Game paused");
    }
}

void requestResume() {
    if (gameState == STATE_PAUSED) {
        gameState = STATE_PLAYING;
        Serial.println("[game] Game resumed");
    }
}

void requestStop() {
    gameState = STATE_IDLE;
    Serial.println("[game] Game stopped");
}

void requestReset() {
    detector.goalCount = 0;
    goalJustScored = false;
    goalSnapshotSeq = 0;
    goalSnapshotLen = 0;
    if (gameState == STATE_PLAYING || gameState == STATE_PAUSED) {
        gameState = STATE_PLAYING;
    }
    Serial.println("[game] Score reset to 0");
}

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

    calSnapshotBuf = (uint8_t*)ps_malloc(64 * 1024);
    calSnapshotMutex = xSemaphoreCreateMutex();
    if (!calSnapshotBuf || !calSnapshotMutex) {
        Serial.println("WARN: cal snapshot alloc failed");
    }

    goalSnapshotBuf = (uint8_t*)ps_malloc(64 * 1024);
    goalSnapshotMutex = xSemaphoreCreateMutex();
    if (!goalSnapshotBuf || !goalSnapshotMutex) {
        Serial.println("WARN: goal snapshot alloc failed");
    }

    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_RGB565)) {
        Serial.println("FATAL: Camera init failed!");
        while (true) delay(1000);
    }

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
    Serial.printf("Dashboard: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.println("=== gol-cam ready ===");
}

// Save calibration snapshot JPEG with bbox drawn
void saveCalSnapshot(camera_fb_t* fb, uint16_t* pixels, int x1, int y1, int x2, int y2, uint16_t color) {
    if (!calSnapshotBuf || !calSnapshotMutex) return;

    // Draw bounding box on frame
    if (x1 >= 0) {
        for (int x = x1; x <= x2; x++) {
            pixels[y1 * DETECT_W + x] = color;
            if (y1 + 1 < DETECT_H) pixels[(y1+1) * DETECT_W + x] = color;
            pixels[y2 * DETECT_W + x] = color;
            if (y2 - 1 >= 0) pixels[(y2-1) * DETECT_W + x] = color;
        }
        for (int y = y1; y <= y2; y++) {
            pixels[y * DETECT_W + x1] = color;
            if (x1 + 1 < DETECT_W) pixels[y * DETECT_W + x1 + 1] = color;
            pixels[y * DETECT_W + x2] = color;
            if (x2 - 1 >= 0) pixels[y * DETECT_W + x2 - 1] = color;
        }
    }

    // Convert to JPEG and store
    uint8_t* jpg_buf = NULL;
    size_t jpg_len = 0;
    if (frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
        xSemaphoreTake(calSnapshotMutex, portMAX_DELAY);
        if (jpg_len <= 64 * 1024) {
            memcpy(calSnapshotBuf, jpg_buf, jpg_len);
            calSnapshotLen = jpg_len;
        }
        xSemaphoreGive(calSnapshotMutex);
        // Also update the live stream
        frameStore.update(jpg_buf, jpg_len);
        free(jpg_buf);
    }
}

// Find the brightest/most-distinct object in the frame for calibration
void doCalibration(camera_fb_t* fb, uint16_t* pixels) {
    snprintf(calFeedback, sizeof(calFeedback), "Sampling background...");

    // Step 1: Find the pixel that is most different from the background
    uint32_t bgR = 0, bgG = 0, bgB = 0, bgCount = 0;
    for (int y = 0; y < DETECT_H; y++) {
        for (int x = 0; x < DETECT_W; x++) {
            if (x > DETECT_W/10 && x < DETECT_W*9/10 &&
                y > DETECT_H/10 && y < DETECT_H*9/10) continue;
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            bgR += (px >> 11) & 0x1F;
            bgG += (px >> 5) & 0x3F;
            bgB += px & 0x1F;
            bgCount++;
        }
    }
    int16_t avgBgR = bgR / bgCount;
    int16_t avgBgG = bgG / bgCount;
    int16_t avgBgB = bgB / bgCount;
    Serial.printf("[cal] Background: R=%d G=%d B=%d\n", avgBgR, avgBgG, avgBgB);

    // Step 2: Find the peak distinct pixel
    int16_t bestR = 0, bestG = 0, bestB = 0;
    int bestDist = 0;
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    uint32_t objCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = DETECT_H/10; y < DETECT_H*9/10; y++) {
        for (int x = DETECT_W/10; x < DETECT_W*9/10; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            int16_t r = (px >> 11) & 0x1F;
            int16_t g = (px >> 5) & 0x3F;
            int16_t b = px & 0x1F;

            int dist = abs(r - avgBgR) + abs(g - avgBgG) + abs(b - avgBgB);
            if (dist > bestDist) {
                bestDist = dist;
                bestR = r; bestG = g; bestB = b;
            }
        }
    }

    Serial.printf("[cal] Peak distinct pixel: R=%d G=%d B=%d (dist=%d)\n",
        bestR, bestG, bestB, bestDist);

    if (bestDist < 5) {
        snprintf(calFeedback, sizeof(calFeedback),
            "FAILED: No distinct object found. Place the dadinho in the center of the frame and try again.");
        // Save snapshot without bbox
        saveCalSnapshot(fb, pixels, -1, 0, 0, 0, 0);
        Serial.println("[cal] FAILED: no distinct object found!");
        gameState = STATE_IDLE;
        return;
    }

    // Second pass: collect similar pixels
    int16_t tolR = 4, tolG = 8, tolB = 4;
    for (int y = DETECT_H/10; y < DETECT_H*9/10; y++) {
        for (int x = DETECT_W/10; x < DETECT_W*9/10; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            int16_t r = (px >> 11) & 0x1F;
            int16_t g = (px >> 5) & 0x3F;
            int16_t b = px & 0x1F;

            if (abs(r - bestR) <= tolR && abs(g - bestG) <= tolG && abs(b - bestB) <= tolB) {
                sumR += r; sumG += g; sumB += b;
                objCount++;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (objCount < 5) {
        snprintf(calFeedback, sizeof(calFeedback),
            "FAILED: Object too small (%d pixels). Move the dadinho closer to the camera.", (int)objCount);
        saveCalSnapshot(fb, pixels, -1, 0, 0, 0, 0);
        Serial.println("[cal] FAILED: object too small!");
        gameState = STATE_IDLE;
        return;
    }

    // Store calibrated values
    calR = sumR / objCount;
    calG = sumG / objCount;
    calB = sumB / objCount;
    calTolR = max((int16_t)3, tolR);
    calTolG = max((int16_t)6, tolG);
    calTolB = max((int16_t)3, tolB);
    calPixelCount = objCount;
    calBboxW = maxX - minX + 1;
    calBboxH = maxY - minY + 1;

    int limitMin = max(5, (int)(calPixelCount / 3));
    int limitMax = calPixelCount * 4;
    int limitBbox = max(calBboxW, calBboxH) * 2;

    snprintf(calFeedback, sizeof(calFeedback),
        "OK! Found dice: %d pixels, %dx%d px. Color RGB565(%d,%d,%d). "
        "Detection limits: %d-%d pixels, bbox <= %d px.",
        (int)calPixelCount, calBboxW, calBboxH,
        (int)calR, (int)calG, (int)calB,
        limitMin, limitMax, limitBbox);

    // Save snapshot with green bbox around detected object
    int bx1 = max(0, minX - 2);
    int by1 = max(0, minY - 2);
    int bx2 = min(DETECT_W - 1, maxX + 2);
    int by2 = min(DETECT_H - 1, maxY + 2);
    saveCalSnapshot(fb, pixels, bx1, by1, bx2, by2, 0xE007);  // green

    Serial.printf("[cal] SUCCESS! Color: R=%d G=%d B=%d, %d pixels, bbox=%dx%d\n",
        calR, calG, calB, calPixelCount, calBboxW, calBboxH);

    // Flash LED to confirm
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    gameState = STATE_IDLE;
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

    uint16_t* pixels = (uint16_t*)fb->buf;

    // Handle calibration
    if (gameState == STATE_CALIBRATING) {
        doCalibration(fb, pixels);
        // saveCalSnapshot already updated frameStore and saved the snapshot
        esp_camera_fb_return(fb);
        delay(10);
        return;
    }

    // Detection only runs during active gameplay
    if ((gameState != STATE_PLAYING) || calR < 0) {
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
            frameStore.update(jpg_buf, jpg_len);
            free(jpg_buf);
        }
        esp_camera_fb_return(fb);
        detector.frameCount = frameNum;
        if (now - lastPrint >= 5000) {
            Serial.printf("[idle] fps:%d frames:%d cal:%s\n",
                detector.fps, frameNum, calR >= 0 ? "yes" : "no");
            lastPrint = now;
        }
        delay(10);
        return;
    }

    // --- GAME MODE: detect calibrated dice color ---
    int rx = DETECT_W / 10, ry = DETECT_H / 10;
    int rw = DETECT_W * 8 / 10, rh = DETECT_H * 8 / 10;

    uint32_t matchCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            int16_t r = (px >> 11) & 0x1F;
            int16_t g = (px >> 5) & 0x3F;
            int16_t b = px & 0x1F;

            if (abs(r - calR) <= calTolR &&
                abs(g - calG) <= calTolG &&
                abs(b - calB) <= calTolB) {
                matchCount++;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    int bboxW = (matchCount > 0) ? (maxX - minX + 1) : 0;
    int bboxH = (matchCount > 0) ? (maxY - minY + 1) : 0;
    float density = (bboxW > 0 && bboxH > 0) ? (float)matchCount / (bboxW * bboxH) : 0;

    // Strict size check: must look like the calibrated dice, not a hand
    int minPixels = max(5, (int)(calPixelCount / 3));       // at least 1/3 of calibrated
    int maxPixels = calPixelCount * 4;                       // at most 4x calibrated
    int maxBbox = max(calBboxW, calBboxH) * 2;              // bbox can't exceed 2x calibrated
    bool diceDetected = ((int)matchCount >= minPixels)
        && ((int)matchCount <= maxPixels)                    // TOO MANY = hand, not dice
        && (bboxW <= maxBbox) && (bboxH <= maxBbox)          // TOO BIG = hand, not dice
        && (density >= 0.12f);                               // must be dense cluster

    // Update shared state for web console
    lastMatchCount = matchCount;
    lastBboxW = bboxW;
    lastBboxH = bboxH;
    lastMinPx = minPixels;
    lastMaxPx = maxPixels;
    lastMaxBbox = maxBbox;
    lastDensity = density * 100;
    if (matchCount == 0) lastRejectReason = "";
    else if (diceDetected) lastRejectReason = "DICE";
    else if ((int)matchCount > maxPixels) lastRejectReason = "TOO-BIG";
    else if (bboxW > maxBbox || bboxH > maxBbox) lastRejectReason = "BBOX-BIG";
    else if ((int)matchCount < minPixels) lastRejectReason = "TOO-SMALL";
    else if (density < 0.12f) lastRejectReason = "SPARSE";
    else lastRejectReason = "REJECTED";

    // Draw bounding box on the frame before JPEG conversion
    if (matchCount > 0) {
        // Green = DICE, Red = rejected
        // RGB565 green: R=0 G=63 B=0 → 0x07E0, byte-swapped → 0xE007
        // RGB565 red:   R=31 G=0 B=0 → 0xF800, byte-swapped → 0x00F8
        uint16_t color = diceDetected ? 0xE007 : 0x00F8;

        // Expand bbox by 2px padding for visibility
        int x1 = max(0, minX - 2);
        int y1 = max(0, minY - 2);
        int x2 = min(DETECT_W - 1, maxX + 2);
        int y2 = min(DETECT_H - 1, maxY + 2);

        // Draw top and bottom edges
        for (int x = x1; x <= x2; x++) {
            pixels[y1 * DETECT_W + x] = color;
            if (y1 + 1 < DETECT_H) pixels[(y1+1) * DETECT_W + x] = color;
            pixels[y2 * DETECT_W + x] = color;
            if (y2 - 1 >= 0) pixels[(y2-1) * DETECT_W + x] = color;
        }
        // Draw left and right edges
        for (int y = y1; y <= y2; y++) {
            pixels[y * DETECT_W + x1] = color;
            if (x1 + 1 < DETECT_W) pixels[y * DETECT_W + x1 + 1] = color;
            pixels[y * DETECT_W + x2] = color;
            if (x2 - 1 >= 0) pixels[y * DETECT_W + x2 - 1] = color;
        }
    }

    // Check goal BEFORE converting to JPEG, so we can save the goal snapshot
    detector.lastChangeRatio = (float)matchCount / (rw * rh);
    detector.frameCount = frameNum;
    bool inCooldown = (now - lastGoalTime) < COOLDOWN_MS;
    bool isGoal = diceDetected && !diceWasPresent && !inCooldown && noDiceFrames > STABLE_FRAMES_NEEDED;

    // Convert to JPEG (with rectangle drawn)
    {
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
            frameStore.update(jpg_buf, jpg_len);
            // Save goal snapshot
            if (isGoal && goalSnapshotBuf && goalSnapshotMutex) {
                xSemaphoreTake(goalSnapshotMutex, portMAX_DELAY);
                if (jpg_len <= 64 * 1024) {
                    memcpy(goalSnapshotBuf, jpg_buf, jpg_len);
                    goalSnapshotLen = jpg_len;
                    goalSnapshotSeq++;
                }
                xSemaphoreGive(goalSnapshotMutex);
            }
            free(jpg_buf);
        }
    }
    esp_camera_fb_return(fb);

    static uint32_t lastDetectLog = 0;
    if (matchCount > 0 && (now - lastDetectLog >= 500)) {
        const char* reason = "";
        if (!diceDetected) {
            if ((int)matchCount > maxPixels) reason = " TOO-BIG";
            else if (bboxW > maxBbox || bboxH > maxBbox) reason = " BBOX-BIG";
            else if ((int)matchCount < minPixels) reason = " TOO-SMALL";
            else if (density < 0.12f) reason = " SPARSE";
        }
        Serial.printf("[detect] %d px bbox=%dx%d dens=%.0f%% (lim:%d-%d bbox<=%d)%s%s%s\n",
            matchCount, bboxW, bboxH, density * 100,
            minPixels, maxPixels, maxBbox,
            diceDetected ? " DICE!" : reason,
            inCooldown ? " (cd)" : "",
            !diceWasPresent ? "" : " (seen)");
        lastDetectLog = now;
    }

    // Goal: dice appears after being absent
    if (isGoal) {
        lastGoalTime = now;
        detector.goalCount++;
        goalJustScored = true;

        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);

        Serial.printf("GOAL #%d! (%d px)\n", detector.goalCount, matchCount);
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
        Serial.printf("[game] fps:%d goals:%d match:%d armed:%s\n",
            detector.fps, detector.goalCount, matchCount,
            !diceWasPresent ? "yes" : "no");
        lastPrint = now;
    }

    delay(10);
}
