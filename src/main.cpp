// =============================================================
// gol-cam — Button Soccer Goal Detection Camera
// Contrast-based detection with I2S goal celebration audio
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "driver/i2s.h"
#include "camera.h"
#include "pins.h"
#include "goal_detector.h"
#include "frame_store.h"
#include "gol_sound.h"
#include "gol_sound_flamengo.h"
#include "gol_sound_vasco.h"
#include "gol_sound_brasil.h"

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

// Edge-based calibration (Sobel gradient)
volatile int calContrastMin = 0;   // Sobel gradient threshold (0 = not calibrated)
volatile int calPixelCount = 0;    // Edge pixel count of dadinho
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

// Software contrast threshold (0=off, 1-255=progressively B&W)
volatile int contrastThreshold = 30;

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

// Audio selection: 0=brasil (default/legacy), 1=flamengo, 2=vasco
volatile int goalAudioSelection = 0;
// Speaker volume: 0-100 (percentage)
volatile int speakerVolume = 70;
// Preview mode: 0=full playback, 1=5 second preview
volatile int audioPreview = 0;

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
        // Write silence to flush all DMA buffers, then pause to let amp settle
        {
            int16_t silence[256] = {};
            size_t w;
            for (int i = 0; i < 16; i++)
                i2s_write(I2S_NUM, silence, sizeof(silence), &w, portMAX_DELAY);
            delay(100);
        }

        // Select audio based on user choice
        const int16_t* soundData;
        int soundSamples;
        switch (goalAudioSelection) {
            case 1:  soundData = GOL_SOUND_FLAMENGO; soundSamples = GOL_SOUND_FLAMENGO_SAMPLES; break;
            case 2:  soundData = GOL_SOUND_VASCO;    soundSamples = GOL_SOUND_VASCO_SAMPLES;    break;
            default: soundData = GOL_SOUND_BRASIL;   soundSamples = GOL_SOUND_BRASIL_SAMPLES;   break;
        }

        // Play the selected goal sound (full or 5s preview)
        int maxSamples = audioPreview ? min(soundSamples, GOAL_SAMPLE_RATE * 5) : soundSamples;
        audioPreview = 0;
        const int16_t* src = soundData;
        int remaining = maxSamples;
        int16_t scaledBuf[256];
        while (remaining > 0) {
            int chunk = min(256, remaining);
            int vol = speakerVolume;
            for (int i = 0; i < chunk; i++)
                scaledBuf[i] = (int16_t)(((int32_t)src[i] * vol) / 100);
            size_t written;
            i2s_write(I2S_NUM, scaledBuf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
            src += chunk;
            remaining -= chunk;
        }

        // Flush silence then uninstall — amp goes quiet
        int16_t silence[256] = {};
        size_t written;
        i2s_write(I2S_NUM, silence, sizeof(silence), &written, portMAX_DELAY);
        delay(50);
        i2s_driver_uninstall(I2S_NUM);
        // Pull I2S pins low to silence the amp when idle
        pinMode(SPK_BCLK_PIN, OUTPUT); digitalWrite(SPK_BCLK_PIN, LOW);
        pinMode(SPK_LRC_PIN, OUTPUT);  digitalWrite(SPK_LRC_PIN, LOW);
        pinMode(SPK_DOUT_PIN, OUTPUT); digitalWrite(SPK_DOUT_PIN, LOW);
    }
}

void playGoalSound() {
    if (goalSoundTaskHandle) {
        xTaskNotifyGive(goalSoundTaskHandle);
    }
}

void initSpeaker() {
    // Hold I2S pins low to keep amp silent until audio plays
    pinMode(SPK_BCLK_PIN, OUTPUT); digitalWrite(SPK_BCLK_PIN, LOW);
    pinMode(SPK_LRC_PIN, OUTPUT);  digitalWrite(SPK_LRC_PIN, LOW);
    pinMode(SPK_DOUT_PIN, OUTPUT); digitalWrite(SPK_DOUT_PIN, LOW);
    xTaskCreatePinnedToCore(goalSoundTask, "goalSound", 4096, NULL, 1, &goalSoundTaskHandle, 1);
    Serial.println("[audio] Speaker task ready");
}

// --- Goal push: notify the scoreboard board over WiFi (best effort) ---
// Runs on its own task so the detection loop never blocks on HTTP.
// SCOREBOARD_IP is injected from .env via load_env.py; absent → no push.
TaskHandle_t goalPushTaskHandle = NULL;

void goalPushTask(void* param) {
#ifdef SCOREBOARD_IP
    const char* scoreboardIp = SCOREBOARD_IP;
    const char* mySide =
    #ifdef BOARD_ROLE
        (strstr(BOARD_ROLE, "_b") || strstr(BOARD_ROLE, "_B")) ? "B" : "A";
    #else
        "A";
    #endif
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (WiFi.status() != WL_CONNECTED || !scoreboardIp[0]) continue;
        String url = String("http://") + scoreboardIp + "/goal";
        String body = String("{\"side\":\"") + mySide + "\"}";
        HTTPClient http;
        http.setTimeout(1000);
        if (http.begin(url)) {
            http.addHeader("Content-Type", "application/json");
            int code = http.POST(body);
            Serial.printf("[scoreboard] POST %s side=%s → %d\n", url.c_str(), mySide, code);
            http.end();
        }
    }
#else
    (void)param;
    vTaskDelete(NULL);
#endif
}

void pushGoalToScoreboard() {
    if (goalPushTaskHandle) xTaskNotifyGive(goalPushTaskHandle);
}

void initScoreboardPush() {
#ifdef SCOREBOARD_IP
    if (SCOREBOARD_IP[0] != '\0') {
        xTaskCreatePinnedToCore(goalPushTask, "goalPush", 4096, NULL, 1, &goalPushTaskHandle, 1);
        Serial.printf("[scoreboard] push target = http://%s/goal\n", SCOREBOARD_IP);
    } else {
        Serial.println("[scoreboard] SCOREBOARD_IP empty; goal push disabled");
    }
#else
    Serial.println("[scoreboard] SCOREBOARD_IP not set; goal push disabled");
#endif
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

    // IR LED on GPIO47 — simple digital output (no PWM to avoid amp noise)
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

    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_GRAYSCALE)) {
        Serial.println("FATAL: Camera init failed!");
        while (true) delay(1000);
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_contrast(s, 1);
        s->set_brightness(s, -1);
        s->set_sharpness(s, 1);
        s->set_special_effect(s, 2); // grayscale
        s->set_exposure_ctrl(s, 0);  // manual exposure
        s->set_aec_value(s, 150);
        s->set_aec2(s, 0);
        s->set_gain_ctrl(s, 0);      // manual gain
        s->set_agc_gain(s, 8);
        s->set_gainceiling(s, (gainceiling_t)1);
        s->set_raw_gma(s, 0);
        s->set_lenc(s, 0);
        Serial.println("[cam] Sensor configured (grayscale + high contrast edge mode)");
    }

    // Initialize I2S speaker
    initSpeaker();

    // Initialize scoreboard push (best-effort goal notification to placar board)
    initScoreboardPush();

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

// Apply software threshold to push pixels toward B&W (grayscale version)
void applyThreshold(uint8_t* pixels, int w, int h, int threshold) {
    if (threshold <= 0) return;
    int totalPx = w * h;
    for (int i = 0; i < totalPx; i++) {
        int v = pixels[i];
        bool bright = v >= 128;
        if (threshold >= 255) {
            pixels[i] = bright ? 255 : 0;
        } else {
            int target = bright ? 255 : 0;
            pixels[i] = (v * (255 - threshold) + target * threshold) / 255;
        }
    }
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

// 3x3 Sobel edge detection on grayscale ROI
// Returns edge magnitude for pixel (x,y). Caller must ensure 1px border.
inline int sobelMag(uint8_t* pixels, int x, int y, int w) {
    int tl = pixels[(y-1)*w + (x-1)];
    int tc = pixels[(y-1)*w + x];
    int tr = pixels[(y-1)*w + (x+1)];
    int ml = pixels[y*w + (x-1)];
    int mr = pixels[y*w + (x+1)];
    int bl = pixels[(y+1)*w + (x-1)];
    int bc = pixels[(y+1)*w + x];
    int br = pixels[(y+1)*w + (x+1)];
    int gx = -tl + tr - 2*ml + 2*mr - bl + br;
    int gy = -tl - 2*tc - tr + bl + 2*bc + br;
    // Approximate magnitude (avoids sqrt)
    return abs(gx) + abs(gy);
}

// Draw ROI rectangle (white border on grayscale)
void drawROI(uint8_t* pixels, int rx, int ry, int rw, int rh) {
    uint8_t roiColor = 255;
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

// Save calibration snapshot JPEG with bbox drawn (grayscale)
void saveCalSnapshot(camera_fb_t* fb, uint8_t* pixels, int x1, int y1, int x2, int y2, uint8_t color) {
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

// Calibration: Sobel edge detection to measure dadinho edges
void doCalibration(camera_fb_t* fb, uint8_t* pixels) {
    snprintf(calFeedback, sizeof(calFeedback), "Analyzing edges...");

    int rx, ry, rw, rh;
    computeROI(rx, ry, rw, rh);

    // Step 1: Compute peak Sobel magnitude in ROI interior
    int peakMag = 0;
    int marginX = max(2, rw * 15 / 100);
    int marginY = max(2, rh * 15 / 100);

    // Need 1px border for Sobel kernel
    int sx = max(1, rx + marginX);
    int sy = max(1, ry + marginY);
    int ex = min(DETECT_W - 2, rx + rw - marginX);
    int ey = min(DETECT_H - 2, ry + rh - marginY);

    for (int y = sy; y <= ey; y++) {
        for (int x = sx; x <= ex; x++) {
            int mag = sobelMag(pixels, x, y, DETECT_W);
            if (mag > peakMag) peakMag = mag;
        }
    }

    Serial.printf("[cal] Peak Sobel magnitude: %d\n", peakMag);

    if (peakMag < 30) {
        snprintf(calFeedback, sizeof(calFeedback),
            "FAILED: No edges found (%d). Place dadinho with good contrast.", peakMag);
        saveCalSnapshot(fb, pixels, -1, 0, 0, 0, 0);
        Serial.println("[cal] FAILED: no edges found!");
        gameState = STATE_IDLE;
        return;
    }

    // Step 2: Set edge threshold at 40% of peak magnitude
    int threshold = peakMag * 40 / 100;
    if (threshold < 15) threshold = 15;

    // Step 3: Count edge pixels and measure bounding box
    uint32_t edgeCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = sy; y <= ey; y++) {
        for (int x = sx; x <= ex; x++) {
            int mag = sobelMag(pixels, x, y, DETECT_W);
            if (mag >= threshold) {
                edgeCount++;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (edgeCount < 5) {
        snprintf(calFeedback, sizeof(calFeedback),
            "FAILED: Too few edges (%d px). Move dadinho closer.", (int)edgeCount);
        saveCalSnapshot(fb, pixels, -1, 0, 0, 0, 0);
        Serial.println("[cal] FAILED: too few edge pixels!");
        gameState = STATE_IDLE;
        return;
    }

    // Store calibration
    calContrastMin = threshold;
    calPixelCount = edgeCount;
    calBboxW = maxX - minX + 1;
    calBboxH = maxY - minY + 1;

    int limitMin = max(5, (int)(calPixelCount / 3));
    int limitMax = calPixelCount * 4;
    int limitBbox = max(calBboxW, calBboxH) * 2;

    snprintf(calFeedback, sizeof(calFeedback),
        "OK! Edge threshold: %d (peak: %d). Edges: %d px, %dx%d. "
        "Limits: %d-%d px, bbox <= %d.",
        threshold, peakMag, (int)edgeCount, calBboxW, calBboxH,
        limitMin, limitMax, limitBbox);

    // Save snapshot with white bbox
    int bx1 = max(0, minX - 2);
    int by1 = max(0, minY - 2);
    int bx2 = min(DETECT_W - 1, maxX + 2);
    int by2 = min(DETECT_H - 1, maxY + 2);
    saveCalSnapshot(fb, pixels, bx1, by1, bx2, by2, 255);

    Serial.printf("[cal] SUCCESS! Edge threshold=%d, %d edges, bbox=%dx%d\n",
        calContrastMin, calPixelCount, calBboxW, calBboxH);

    // Flash LED to confirm
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

    size_t expectedLen = DETECT_W * DETECT_H;
    if (fb->format != PIXFORMAT_GRAYSCALE || fb->len < expectedLen) {
        esp_camera_fb_return(fb);
        delay(100);
        return;
    }

    uint8_t* pixels = fb->buf;

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
        applyThreshold(pixels, DETECT_W, DETECT_H, contrastThreshold);
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

    // --- GAME MODE: Sobel edge-based detection ---

    // Sobel on ROI only (need 1px border for kernel)
    int sx = max(1, rx);
    int sy = max(1, ry);
    int ex = min(DETECT_W - 2, rx + rw - 1);
    int ey = min(DETECT_H - 2, ry + rh - 1);

    uint32_t matchCount = 0;
    int minX = DETECT_W, maxX = 0, minY = DETECT_H, maxY = 0;

    for (int y = sy; y <= ey; y++) {
        for (int x = sx; x <= ex; x++) {
            int mag = sobelMag(pixels, x, y, DETECT_W);
            if (mag >= calContrastMin) {
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

    // Apply threshold filter then draw ROI
    applyThreshold(pixels, DETECT_W, DETECT_H, contrastThreshold);
    drawROI(pixels, rx, ry, rw, rh);

    // Draw bounding box (white=detected, gray=rejected)
    if (matchCount > 0) {
        uint8_t color = diceDetected ? 255 : 128;
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
        Serial.printf("[detect] %d edges bbox=%dx%d dens=%.0f%% (lim:%d-%d bbox<=%d)%s%s%s\n",
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

        // Push goal to physical scoreboard (best effort; placar polling is fallback)
        pushGoalToScoreboard();

        // Flash LED
        digitalWrite(LED_PIN, HIGH);
        delay(300);
        digitalWrite(LED_PIN, LOW);

        Serial.printf("GOAL #%d! (%d edges, threshold>=%d)\n",
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
