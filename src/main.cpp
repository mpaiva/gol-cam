// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Contrast-based detection with I2S goal celebration audio
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "driver/i2s.h"
#include "camera.h"
#include "pins.h"
#include "goal_detector.h"
#include "frame_store.h"
#include "gol_sound.h"

// --- WiFi credentials (from .env file via build defines) ---
#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Create a .env file with WIFI_SSID=your_ssid"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined. Create a .env file with WIFI_PASSWORD=your_password"
#endif

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

// Contrast-based calibration (lighting-independent)
volatile int calContrastMin = 0;   // min distance from background to count as object (0 = not calibrated)
volatile int calPixelCount = 0;
volatile int calBboxW = 0, calBboxH = 0;

// Detection params
#define COOLDOWN_MS 10000
#define STABLE_FRAMES_NEEDED 1

// ROI offset and size for digital pan/resize (adjusted via /roi endpoint)
volatile int roiOffsetX = 0, roiOffsetY = 0;
volatile int roiW = DETECT_W * 8 / 10, roiH = DETECT_H * 8 / 10;

// Shared detection state for web console
volatile int lastMatchCount = 0, lastBboxW = 0, lastBboxH = 0;
volatile int lastMinPx = 0, lastMaxPx = 0, lastMaxBbox = 0;
volatile float lastDensity = 0;
volatile const char* lastRejectReason = "";

// Calibration snapshot
uint8_t* calSnapshotBuf = nullptr;
size_t calSnapshotLen = 0;
SemaphoreHandle_t calSnapshotMutex = nullptr;
char calFeedback[256] = "";

// Goal snapshot
uint8_t* goalSnapshotBuf = nullptr;
size_t goalSnapshotLen = 0;
SemaphoreHandle_t goalSnapshotMutex = nullptr;
volatile uint32_t goalSnapshotSeq = 0;
volatile uint32_t lastGoalTimeMs = 0;

// --- I2S Speaker for goal celebration ---
// Driver is installed only when playing, then uninstalled to keep speaker silent.
#define I2S_NUM I2S_NUM_0
#define GOAL_SAMPLE_RATE 16000
TaskHandle_t goalSoundTaskHandle = NULL;

void goalSoundTask(void* param) {
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = GOAL_SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = 0;
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 256;
    i2s_config.use_apll = false;

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = SPK_BCLK_PIN;
    pin_config.ws_io_num = SPK_LRC_PIN;
    pin_config.data_out_num = SPK_DOUT_PIN;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Install I2S driver, play sound 3x, then uninstall to keep amp silent
        if (i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL) != ESP_OK) continue;
        i2s_set_pin(I2S_NUM, &pin_config);
        i2s_zero_dma_buffer(I2S_NUM);

        // Play the embedded goal sound 3 times
        for (int rep = 0; rep < 3; rep++) {
            const int16_t* src = GOL_SOUND;
            int remaining = GOL_SOUND_SAMPLES;
            while (remaining > 0) {
                int chunk = min(256, remaining);
                size_t written;
                i2s_write(I2S_NUM, src, chunk * sizeof(int16_t), &written, portMAX_DELAY);
                src += chunk;
                remaining -= chunk;
            }
        }

        // Flush silence then uninstall — amp goes quiet
        int16_t silence[256] = {};
        size_t written;
        i2s_write(I2S_NUM, silence, sizeof(silence), &written, portMAX_DELAY);
        delay(50);
        i2s_driver_uninstall(I2S_NUM);
    }
}

void playGoalSound() {
    if (goalSoundTaskHandle) {
        xTaskNotifyGive(goalSoundTaskHandle);
    }
}

void initSpeaker() {
    xTaskCreatePinnedToCore(goalSoundTask, "goalSound", 4096, NULL, 1, &goalSoundTaskHandle, 1);
    Serial.println("[audio] Speaker task ready");
}

// Called from HTTP handler to trigger calibration
void requestCalibration() {
    gameState = STATE_CALIBRATING;
    Serial.println("[cal] Calibration requested");
}

void requestStart() {
    if (calContrastMin > 0) {
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

void requestDeduct() {
    if (detector.goalCount > 0) {
        detector.goalCount--;
        Serial.printf("[VAR] Gol annulled! Score now %d\n", detector.goalCount);
    }
}

void requestReset() {
    detector.goalCount = 0;
    goalJustScored = false;
    goalSnapshotSeq = 0;
    goalSnapshotLen = 0;
    gameState = STATE_IDLE;
    Serial.println("[game] Reset — back to calibration");
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
        s->set_contrast(s, 2);
        s->set_brightness(s, 1);
        s->set_sharpness(s, 2);
        s->set_whitebal(s, 0);
        s->set_awb_gain(s, 0);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_gainceiling(s, (gainceiling_t)4);
        s->set_lenc(s, 1);
        s->set_raw_gma(s, 1);
        Serial.println("[cam] Sensor configured");
    }

    // Initialize I2S speaker
    initSpeaker();

    // Static IP configuration (optional — set in .env)
#ifdef WIFI_STATIC_IP
    IPAddress staticIP, gateway, subnet;
    staticIP.fromString(WIFI_STATIC_IP);
    gateway.fromString(WIFI_GATEWAY);
    subnet.fromString(WIFI_SUBNET);
    WiFi.config(staticIP, gateway, subnet);
    Serial.printf("Static IP: %s\n", WIFI_STATIC_IP);
#endif

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

// Helper: compute ROI bounds
void computeROI(int &rx, int &ry, int &rw, int &rh) {
    rw = roiW; rh = roiH;
    rx = (DETECT_W - rw) / 2 + roiOffsetX;
    ry = (DETECT_H - rh) / 2 + roiOffsetY;
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx + rw > DETECT_W) rx = DETECT_W - rw;
    if (ry + rh > DETECT_H) ry = DETECT_H - rh;
}

// Helper: compute background average from ALL ROI pixels.
// Since the background dominates the ROI area (~95%+), the average
// naturally represents the background even when the dadinho is present.
// This is more robust than edge-band sampling when the ROI contains
// varying brightness (goal frame edges, reflections).
void computeBackground(uint16_t* pixels, int rx, int ry, int rw, int rh,
                        int16_t &bgR, int16_t &bgG, int16_t &bgB) {
    uint32_t sR = 0, sG = 0, sB = 0;
    uint32_t cnt = rw * rh;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            sR += (px >> 11) & 0x1F;
            sG += (px >> 5) & 0x3F;
            sB += px & 0x1F;
        }
    }
    if (cnt == 0) cnt = 1;
    bgR = sR / cnt;
    bgG = sG / cnt;
    bgB = sB / cnt;
}

// Draw ROI rectangle (cyan)
void drawROI(uint16_t* pixels, int rx, int ry, int rw, int rh) {
    uint16_t roiColor = 0xFF07; // cyan byte-swapped
    int rx2 = rx + rw - 1, ry2 = ry + rh - 1;
    for (int x = rx; x <= rx2; x++) {
        pixels[ry * DETECT_W + x] = roiColor;
        pixels[ry2 * DETECT_W + x] = roiColor;
    }
    for (int y = ry; y <= ry2; y++) {
        pixels[y * DETECT_W + rx] = roiColor;
        pixels[y * DETECT_W + rx2] = roiColor;
    }
}

// Save calibration snapshot JPEG with bbox drawn
void saveCalSnapshot(camera_fb_t* fb, uint16_t* pixels, int x1, int y1, int x2, int y2, uint16_t color) {
    if (!calSnapshotBuf || !calSnapshotMutex) return;

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

    uint8_t* jpg_buf = NULL;
    size_t jpg_len = 0;
    if (frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
        xSemaphoreTake(calSnapshotMutex, portMAX_DELAY);
        if (jpg_len <= 64 * 1024) {
            memcpy(calSnapshotBuf, jpg_buf, jpg_len);
            calSnapshotLen = jpg_len;
        }
        xSemaphoreGive(calSnapshotMutex);
        frameStore.update(jpg_buf, jpg_len);
        free(jpg_buf);
    }
}

// Calibration: measure contrast ratio between object and background
void doCalibration(camera_fb_t* fb, uint16_t* pixels) {
    snprintf(calFeedback, sizeof(calFeedback), "Sampling background...");

    int rx, ry, rw, rh;
    computeROI(rx, ry, rw, rh);

    // Step 1: Background from ROI edge band
    int16_t bgR, bgG, bgB;
    computeBackground(pixels, rx, ry, rw, rh, bgR, bgG, bgB);
    Serial.printf("[cal] Background: R=%d G=%d B=%d\n", bgR, bgG, bgB);

    // Step 2: Find peak contrast pixel in ROI interior
    int bestDist = 0;
    int marginX = rw * 15 / 100, marginY = rh * 15 / 100;
    if (marginX < 2) marginX = 2;
    if (marginY < 2) marginY = 2;

    for (int y = ry + marginY; y < ry + rh - marginY; y++) {
        for (int x = rx + marginX; x < rx + rw - marginX; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            int16_t r = (px >> 11) & 0x1F;
            int16_t g = (px >> 5) & 0x3F;
            int16_t b = px & 0x1F;
            int dist = abs(r - bgR) + abs(g - bgG) + abs(b - bgB);
            if (dist > bestDist) bestDist = dist;
        }
    }

    Serial.printf("[cal] Peak contrast distance: %d\n", bestDist);

    if (bestDist < 5) {
        snprintf(calFeedback, sizeof(calFeedback),
            "FAILED: No distinct object found. Place the dadinho in the ROI and try again.");
        saveCalSnapshot(fb, pixels, -1, 0, 0, 0, 0);
        Serial.println("[cal] FAILED: no distinct object found!");
        gameState = STATE_IDLE;
        return;
    }

    // Step 3: Set contrast threshold at 65% of peak distance
    int threshold = bestDist * 65 / 100;
    if (threshold < 3) threshold = 3;

    // Step 4: Collect pixels exceeding threshold to measure object size
    uint32_t objCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = ry + marginY; y < ry + rh - marginY; y++) {
        for (int x = rx + marginX; x < rx + rw - marginX; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            int16_t r = (px >> 11) & 0x1F;
            int16_t g = (px >> 5) & 0x3F;
            int16_t b = px & 0x1F;
            int dist = abs(r - bgR) + abs(g - bgG) + abs(b - bgB);
            if (dist >= threshold) {
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
            "FAILED: Object too small (%d px). Move dadinho closer.", (int)objCount);
        saveCalSnapshot(fb, pixels, -1, 0, 0, 0, 0);
        Serial.println("[cal] FAILED: object too small!");
        gameState = STATE_IDLE;
        return;
    }

    // Store calibration
    calContrastMin = threshold;
    calPixelCount = objCount;
    calBboxW = maxX - minX + 1;
    calBboxH = maxY - minY + 1;

    int limitMin = max(5, (int)(calPixelCount / 3));
    int limitMax = calPixelCount * 4;
    int limitBbox = max(calBboxW, calBboxH) * 2;

    snprintf(calFeedback, sizeof(calFeedback),
        "OK! Contrast threshold: %d (peak: %d). Object: %d px, %dx%d. "
        "Limits: %d-%d px, bbox <= %d.",
        threshold, bestDist, (int)objCount, calBboxW, calBboxH,
        limitMin, limitMax, limitBbox);

    // Save snapshot with green bbox
    int bx1 = max(0, minX - 2);
    int by1 = max(0, minY - 2);
    int bx2 = min(DETECT_W - 1, maxX + 2);
    int by2 = min(DETECT_H - 1, maxY + 2);
    saveCalSnapshot(fb, pixels, bx1, by1, bx2, by2, 0xE007);

    Serial.printf("[cal] SUCCESS! Contrast threshold=%d, %d px, bbox=%dx%d\n",
        calContrastMin, calPixelCount, calBboxW, calBboxH);

    // Flash LED + play sound to confirm
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    gameState = STATE_IDLE;
}

void loop() {
    static bool diceWasPresent = false;
    static uint32_t noDiceFrames = 0;
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
        esp_camera_fb_return(fb);
        return;
    }

    // Compute ROI
    int rx, ry, rw, rh;
    computeROI(rx, ry, rw, rh);

    // Not playing: draw ROI and stream
    if ((gameState != STATE_PLAYING) || calContrastMin <= 0) {
        drawROI(pixels, rx, ry, rw, rh);
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
                detector.fps, frameNum, calContrastMin > 0 ? "yes" : "no");
            lastPrint = now;
        }
        return;
    }

    // --- GAME MODE: contrast-based detection ---

    // Step 1: Compute live background from ROI edge band
    int16_t bgR, bgG, bgB;
    computeBackground(pixels, rx, ry, rw, rh, bgR, bgG, bgB);

    // Step 2: Find pixels exceeding contrast threshold
    uint32_t matchCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = ry; y < ry + rh; y++) {
        for (int x = rx; x < rx + rw; x++) {
            uint16_t raw = pixels[y * DETECT_W + x];
            uint16_t px = (raw >> 8) | (raw << 8);
            int16_t r = (px >> 11) & 0x1F;
            int16_t g = (px >> 5) & 0x3F;
            int16_t b = px & 0x1F;

            int dist = abs(r - bgR) + abs(g - bgG) + abs(b - bgB);
            if (dist >= calContrastMin) {
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

    // Size filters
    int minPixels = max(5, (int)(calPixelCount / 3));
    int maxPixels = calPixelCount * 4;
    int maxBbox = max(calBboxW, calBboxH) * 2;
    bool diceDetected = ((int)matchCount >= minPixels)
        && ((int)matchCount <= maxPixels)
        && (bboxW <= maxBbox) && (bboxH <= maxBbox)
        && (density >= 0.12f);

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

    // Draw ROI rectangle
    drawROI(pixels, rx, ry, rw, rh);

    // Draw bounding box
    if (matchCount > 0) {
        uint16_t color = diceDetected ? 0xE007 : 0x00F8;
        int x1 = max(0, minX - 2);
        int y1 = max(0, minY - 2);
        int x2 = min(DETECT_W - 1, maxX + 2);
        int y2 = min(DETECT_H - 1, maxY + 2);
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

    // Check goal
    detector.lastChangeRatio = (float)matchCount / (rw * rh);
    detector.frameCount = frameNum;
    bool inCooldown = (now - lastGoalTimeMs) < COOLDOWN_MS;
    bool isGoal = diceDetected && !diceWasPresent && !inCooldown && noDiceFrames >= STABLE_FRAMES_NEEDED;

    // Convert to JPEG
    {
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
            frameStore.update(jpg_buf, jpg_len);
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

    // Goal scored!
    if (isGoal) {
        lastGoalTimeMs = now;
        detector.goalCount++;
        goalJustScored = true;

        // Play celebration sound (non-blocking, runs on separate core)
        playGoalSound();

        // Flash LED
        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);

        Serial.printf("GOAL #%d! (%d px, contrast>=%d)\n",
            detector.goalCount, matchCount, (int)calContrastMin);
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
}
