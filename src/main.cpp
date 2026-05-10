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
#include <math.h>

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
enum GameState { STATE_IDLE, STATE_CALIBRATING, STATE_PLAYING, STATE_PAUSED, STATE_AUTOTUNE };
volatile GameState gameState = STATE_IDLE;

// Auto-tune progress (read by /status)
volatile int autotuneStage = 0;       // current sweep stage 0=idle, 1..N=running
volatile int autotuneStep = 0;
volatile int autotuneTotalSteps = 0;
volatile int autotuneDone = 0;        // 1 once the full sweep finishes
volatile int autotuneBestScore = 0;
volatile int autotuneBestGain  = 8;
volatile int autotuneBestGceil = 1;
volatile int autotuneBestAec   = 150;
volatile int autotuneBestGma   = 0;
volatile int autotuneBestLenc  = 0;
volatile int autotuneBestCon   = 1;
volatile int autotuneBestBri   = -1;
volatile int autotuneBestSharp = 1;
volatile int autotuneBestThresh = 30;

// Live "currently being tested" values — updated every applyCamSettings() so the
// dashboard can show sliders moving in real time during the sweep.
volatile int curCamGain  = 8;
volatile int curCamGceil = 1;
volatile int curCamAec   = 150;
volatile int curCamGma   = 0;
volatile int curCamLenc  = 0;
volatile int curCamCon   = 1;
volatile int curCamBri   = -1;
volatile int curCamSharp = 1;

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

// Last-detected dadinho bbox (in DETECT_W/H pixel coords). Set during STATE_PLAYING
// when the dice passes the size/density filters; consumed by the dashboard's SVG
// overlay. -1 = no current detection.
volatile int diceBboxX = -1, diceBboxY = -1, diceBboxW = 0, diceBboxH = 0;

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

// Speaker volume: 0-100 (percentage)
volatile int speakerVolume = 70;

// Pre-generated whistle waveform (allocated in initSpeaker)
static int16_t* whistleBuf = nullptr;
static int whistleSamples = 0;


static void buildWhistle() {
    const int blastMs = 180;
    const int gapMs = 90;
    const int blastSamples = (GOAL_SAMPLE_RATE * blastMs) / 1000;
    const int gapSamples = (GOAL_SAMPLE_RATE * gapMs) / 1000;
    const int total = 3 * blastSamples + 2 * gapSamples;
    whistleBuf = (int16_t*)ps_malloc(total * sizeof(int16_t));
    if (!whistleBuf) {
        Serial.println("[audio] ERROR: whistle buffer alloc failed");
        return;
    }
    whistleSamples = total;

    const float baseFreq = 2700.0f;
    const float vibratoFreq = 5.0f;
    const float vibratoDepth = 30.0f;
    const float twoPi = 6.28318530718f;
    const float dt = 1.0f / (float)GOAL_SAMPLE_RATE;
    const int rampSamples = GOAL_SAMPLE_RATE * 8 / 1000;

    int idx = 0;
    for (int blast = 0; blast < 3; blast++) {
        float phase = 0.0f, vphase = 0.0f;
        for (int n = 0; n < blastSamples; n++) {
            float env = 1.0f;
            if (n < rampSamples) env = (float)n / rampSamples;
            else if (blastSamples - n < rampSamples)
                env = (float)(blastSamples - n) / rampSamples;
            float freq = baseFreq + sinf(vphase) * vibratoDepth;
            float s = sinf(phase) * 0.85f + sinf(phase * 2.0f) * 0.15f;
            int sample = (int)(s * env * 18000.0f);
            if (sample > 32767) sample = 32767;
            if (sample < -32768) sample = -32768;
            whistleBuf[idx++] = (int16_t)sample;
            phase += twoPi * freq * dt;
            if (phase > twoPi) phase -= twoPi;
            vphase += twoPi * vibratoFreq * dt;
            if (vphase > twoPi) vphase -= twoPi;
        }
        if (blast < 2) {
            for (int g = 0; g < gapSamples; g++) whistleBuf[idx++] = 0;
        }
    }
    Serial.printf("[audio] Whistle pre-generated: %d samples (%d ms)\n",
        total, total * 1000 / GOAL_SAMPLE_RATE);
}

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

        // Play the pre-generated whistle buffer, scaling by current volume
        size_t written;
        if (whistleBuf && whistleSamples > 0) {
            const int chunk = 256;
            int16_t scaled[256];
            int remaining = whistleSamples;
            const int16_t* src = whistleBuf;
            while (remaining > 0) {
                int n = remaining < chunk ? remaining : chunk;
                int vol = speakerVolume;
                for (int i = 0; i < n; i++)
                    scaled[i] = (int16_t)(((int32_t)src[i] * vol) / 100);
                i2s_write(I2S_NUM, scaled, n * sizeof(int16_t), &written, portMAX_DELAY);
                src += n;
                remaining -= n;
            }
        }

        // Flush silence then uninstall — amp goes quiet
        int16_t flushSilence[256] = {};
        i2s_write(I2S_NUM, flushSilence, sizeof(flushSilence), &written, portMAX_DELAY);
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

// Which side of the scoreboard this camera advances. Resolved once at startup
// from .env: SCOREBOARD_SIDE override > BOARD_ROLE rule > default 'a'.
//   home camera → opponent (away) scores → side B
//   away camera → home scores            → side A
//   single camera (no role)              → side A
static char scoreboardSide = 'a';

static void resolveScoreboardSide() {
#ifdef SCOREBOARD_SIDE
    const char* s = SCOREBOARD_SIDE;
    if (s && (s[0] == 'a' || s[0] == 'A')) { scoreboardSide = 'a'; return; }
    if (s && (s[0] == 'b' || s[0] == 'B')) { scoreboardSide = 'b'; return; }
#endif
#ifdef BOARD_ROLE
    const char* r = BOARD_ROLE;
    if (r && r[0] == 'h') { scoreboardSide = 'b'; return; }  // home cam → side B
    if (r && r[0] == 'a') { scoreboardSide = 'a'; return; }  // away cam → side A
#endif
    scoreboardSide = 'a';
}

#ifdef SCOREBOARD_IP
// Background HTTP push so the goal-detection loop never blocks on the network.
// The task self-deletes after firing one request.
static void scoreboardPushTask(void* param) {
    char side = (char)(intptr_t)param;
    HTTPClient http;
    String url = String("http://") + SCOREBOARD_IP + "/goal?side=" + side;
    if (http.begin(url)) {
        http.setTimeout(2000);
        int code = http.GET();
        Serial.printf("[score] push side=%c → HTTP %d\n", side, code);
        http.end();
    } else {
        Serial.println("[score] http.begin failed");
    }
    vTaskDelete(NULL);
}

void pushGoalToScoreboard() {
    xTaskCreatePinnedToCore(scoreboardPushTask, "scorePush", 4096,
                            (void*)(intptr_t)scoreboardSide, 1, NULL, 0);
}
#else
void pushGoalToScoreboard() {}  // SCOREBOARD_IP not configured → no-op
#endif

void initSpeaker() {
    // Hold I2S pins low to keep amp silent until audio plays
    pinMode(SPK_BCLK_PIN, OUTPUT); digitalWrite(SPK_BCLK_PIN, LOW);
    pinMode(SPK_LRC_PIN, OUTPUT);  digitalWrite(SPK_LRC_PIN, LOW);
    pinMode(SPK_DOUT_PIN, OUTPUT); digitalWrite(SPK_DOUT_PIN, LOW);
    buildWhistle();
    xTaskCreatePinnedToCore(goalSoundTask, "goalSound", 4096, NULL, 1, &goalSoundTaskHandle, 1);
    Serial.println("[audio] Speaker task ready");
}

// Called from HTTP handler to trigger calibration
void requestCalibration() {
    gameState = STATE_CALIBRATING;
    Serial.println("[cal] Calibration requested");
}

void requestAutotune() {
    if (gameState != STATE_IDLE) return;
    autotuneStage = 0;
    autotuneStep = 0;
    autotuneTotalSteps = 0;
    autotuneDone = 0;
    autotuneBestScore = 0;
    gameState = STATE_AUTOTUNE;
    Serial.println("[tune] Auto-tune requested");
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

    // Decide which scoreboard side this camera advances on a goal
    resolveScoreboardSide();
#ifdef SCOREBOARD_IP
    Serial.printf("[score] target=%s side=%c\n", SCOREBOARD_IP, scoreboardSide);
#else
    Serial.println("[score] no SCOREBOARD_IP configured — scoreboard pushes disabled");
#endif

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

// Hard B&W binarization: pixels < threshold → 0, >= threshold → 255.
// threshold=0 means pass-through (no binarization).
void applyThreshold(uint8_t* pixels, int w, int h, int threshold) {
    if (threshold <= 0) return;
    int totalPx = w * h;
    for (int i = 0; i < totalPx; i++) {
        pixels[i] = (pixels[i] < threshold) ? 0 : 255;
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

// (Overlay rendering moved to client-side SVG in the dashboards.)

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

// Otsu's between-class variance over ROI. Returns peak score (long, scaled) and
// writes argmax threshold into *bestT. Higher peak = cleaner bimodal separation.
long otsuScore(uint8_t* pixels, int w, int rx, int ry, int rw, int rh, int* bestT) {
    int hist[256] = {0};
    int total = 0;
    for (int y = ry; y < ry + rh; y++) {
        uint8_t* row = pixels + y * w + rx;
        for (int i = 0; i < rw; i++) hist[row[i]]++;
        total += rw;
    }
    if (total == 0) { *bestT = 128; return 0; }

    long sum = 0;
    for (int i = 0; i < 256; i++) sum += (long)i * hist[i];

    long sumB = 0;
    long wB = 0;
    long peak = 0;
    int peakT = 128;
    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0) continue;
        long wF = total - wB;
        if (wF == 0) break;
        sumB += (long)t * hist[t];
        // Use squared-mean-diff scaled by class weights — keep numbers manageable.
        // (mB - mF)² in 0..65025; wB*wF / 1024 keeps the product within long range.
        long mBn = sumB;                 // sum * count units
        long mFn = sum - sumB;
        long diff = (mBn * wF - mFn * wB);
        long score = diff * diff / ((wB * wF) + 1) / 1024;
        if (score > peak) { peak = score; peakT = t; }
    }
    *bestT = peakT;
    return peak;
}

// Stddev of pixel intensity in ROI — used as the camera-sweep metric.
// Higher stddev = more dynamic range = better contrast for binarization.
long roiStdev(uint8_t* pixels, int w, int rx, int ry, int rw, int rh) {
    long sum = 0, sum2 = 0;
    long n = (long)rw * rh;
    for (int y = ry; y < ry + rh; y++) {
        uint8_t* row = pixels + y * w + rx;
        for (int i = 0; i < rw; i++) {
            int v = row[i];
            sum += v;
            sum2 += v * v;
        }
    }
    if (n == 0) return 0;
    long mean = sum / n;
    long var = sum2 / n - mean * mean;
    return var;  // skip sqrt — argmax is monotonic in variance
}

// All sensor knobs the autotune cycles through.
struct CamSettings {
    int gain;
    int gceil;
    int aec;
    int gma;
    int lenc;
    int con;
    int bri;
    int sharp;
};

static void applyCamSettings(const CamSettings& c) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    s->set_agc_gain(s, c.gain);
    s->set_gainceiling(s, (gainceiling_t)c.gceil);
    s->set_aec_value(s, c.aec);
    s->set_raw_gma(s, c.gma);
    s->set_lenc(s, c.lenc);
    s->set_contrast(s, c.con);
    s->set_brightness(s, c.bri);
    s->set_sharpness(s, c.sharp);
    curCamGain = c.gain; curCamGceil = c.gceil; curCamAec = c.aec;
    curCamGma = c.gma; curCamLenc = c.lenc;
    curCamCon = c.con; curCamBri = c.bri; curCamSharp = c.sharp;
}

// Apply sensor params, wait for camera to settle, then capture and score.
// Returns ROI variance (camera-sweep metric); writes Otsu's optimal threshold into *bestT.
// Set pushStreamFrame=true to also publish this frame to the MJPEG stream so
// the dashboard doesn't go stale during long sweeps (called once per stage).
static long captureAndScore(const CamSettings& c, int* bestT, bool pushStreamFrame) {
    applyCamSettings(c);
    // Discard 2 frames to let exposure/gain settle. esp_camera_fb_get blocks
    // until a new frame is available, so no extra delay is needed.
    for (int i = 0; i < 2; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_GRAYSCALE) {
        if (fb) esp_camera_fb_return(fb);
        *bestT = 128;
        return 0;
    }
    int rx, ry, rw, rh;
    computeROI(rx, ry, rw, rh);
    long score = roiStdev(fb->buf, DETECT_W, rx, ry, rw, rh);
    otsuScore(fb->buf, DETECT_W, rx, ry, rw, rh, bestT);
    if (pushStreamFrame) {
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 60, &jpg_buf, &jpg_len)) {
            frameStore.update(jpg_buf, jpg_len);
            free(jpg_buf);
        }
    }
    esp_camera_fb_return(fb);
    return score;
}

// Sweep one parameter through `values`, keeping all other settings at `*best`.
// Updates `*best` and `*bestScore`/`*bestThresh` if any tested value beats current best.
// `field` is a pointer-to-member-style int* that lives inside `*best`.
static void sweepParam(CamSettings* best, long* bestScore, int* bestThresh,
                       int* field, const int* values, int nVals,
                       const char* label, int stageNum) {
    autotuneStage = stageNum;
    autotuneTotalSteps = nVals;
    int origVal = *field;
    int bestVal = origVal;
    Serial.printf("[tune] Stage %d: %s sweep\n", stageNum, label);
    for (int i = 0; i < nVals; i++) {
        autotuneStep = i + 1;
        *field = values[i];
        int t;
        // Push the LAST capture of each sweep to the stream so the browser
        // sees a fresh frame every ~300-500 ms during the autotune.
        bool publish = (i == nVals - 1);
        long score = captureAndScore(*best, &t, publish);
        Serial.printf("[tune]  %s=%d  score=%ld  thresh=%d\n", label, values[i], score, t);
        snprintf(calFeedback, sizeof(calFeedback),
            "Auto-tune: %s %d/%d  best=%ld", label, i + 1, nVals, *bestScore);
        if (score > *bestScore) {
            *bestScore = score; bestVal = values[i]; *bestThresh = t;
        }
    }
    *field = bestVal;            // lock in winning value
    applyCamSettings(*best);     // re-apply so sensor + cur* reflect the running best
}

void runAutotune() {
    // Single-pass greedy sweep over the parameters that move the needle most
    // for separating a bright dadinho from a dark background. Other knobs
    // (sharp/gma/lenc/con) had marginal impact in testing — drop them for speed.
    static const int gainVals[]  = {0, 10, 20, 30};
    static const int gceilVals[] = {0, 3, 6};
    static const int aecVals[]   = {120, 280, 480, 800, 1200};
    static const int briVals[]   = {-1, 0, 1};

    CamSettings best = { .gain=8, .gceil=2, .aec=300, .gma=0, .lenc=0, .con=1, .bri=0, .sharp=1 };
    long bestScore = 0;
    int bestThresh = 128;

    sweepParam(&best, &bestScore, &bestThresh, &best.gain,  gainVals,  4, "gain",  1);
    sweepParam(&best, &bestScore, &bestThresh, &best.gceil, gceilVals, 3, "gceil", 2);
    sweepParam(&best, &bestScore, &bestThresh, &best.aec,   aecVals,   5, "aec",   3);
    sweepParam(&best, &bestScore, &bestThresh, &best.bri,   briVals,   3, "bri",   4);

    // Clamp the auto-Otsu threshold to a usable range. Below ~30 the binarised
    // frame becomes mostly white (dadinho disappears); above ~220 it's mostly
    // black. Both extremes hide the dadinho during play.
    if (bestThresh < 30)  bestThresh = 30;
    if (bestThresh > 220) bestThresh = 220;

    applyCamSettings(best);
    contrastThreshold = bestThresh;
    autotuneBestScore  = (int)(bestScore > 2147483647L ? 2147483647L : bestScore);
    autotuneBestGain   = best.gain;
    autotuneBestGceil  = best.gceil;
    autotuneBestAec    = best.aec;
    autotuneBestGma    = best.gma;
    autotuneBestLenc   = best.lenc;
    autotuneBestCon    = best.con;
    autotuneBestBri    = best.bri;
    autotuneBestSharp  = best.sharp;
    autotuneBestThresh = bestThresh;
    autotuneDone = 1;

    if (bestScore < 100) {
        snprintf(calFeedback, sizeof(calFeedback),
            "Auto-tune done — low contrast (score %ld). Place dadinho in ROI.", bestScore);
    } else {
        snprintf(calFeedback, sizeof(calFeedback),
            "Auto-tune done! gain=%d gceil=%d aec=%d bri=%d B&W=%d  score=%ld",
            best.gain, best.gceil, best.aec, best.bri, bestThresh, bestScore);
    }
    Serial.printf("[tune] DONE  gain=%d gceil=%d aec=%d bri=%d thresh=%d score=%ld\n",
        best.gain, best.gceil, best.aec, best.bri, bestThresh, bestScore);

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

    // Handle auto-tune (returns this frame, then runs its own captures)
    if (gameState == STATE_AUTOTUNE) {
        esp_camera_fb_return(fb);
        runAutotune();
        return;
    }

    // Compute ROI
    int rx, ry, rw, rh;
    computeROI(rx, ry, rw, rh);

    // Not playing: stream the binarized grayscale frame (no overlays — those
    // are drawn client-side as SVG over the <img>).
    if ((gameState != STATE_PLAYING) || calContrastMin <= 0) {
        applyThreshold(pixels, DETECT_W, DETECT_H, contrastThreshold);
        diceBboxX = -1;
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 70, &jpg_buf, &jpg_len)) {
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

    // Apply post-threshold to the grayscale buffer (used for the visualization)
    applyThreshold(pixels, DETECT_W, DETECT_H, contrastThreshold);

    // Check goal
    detector.lastChangeRatio = (float)matchCount / (rw * rh);
    detector.frameCount = frameNum;
    bool inCooldown = (now - lastGoalTimeMs) < COOLDOWN_MS;
    bool isGoal = diceDetected && !diceWasPresent && !inCooldown && noDiceFrames >= STABLE_FRAMES_NEEDED;

    // Publish dadinho bbox so the dashboard SVG overlay can draw it in green
    if (matchCount > 0 && diceDetected) {
        diceBboxX = minX; diceBboxY = minY;
        diceBboxW = maxX - minX + 1; diceBboxH = maxY - minY + 1;
    } else {
        diceBboxX = -1;
    }

    // Encode the (binarized) grayscale frame — overlays drawn client-side
    {
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 70, &jpg_buf, &jpg_len)) {
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

        // Push to the electronic scoreboard (non-blocking task, ~2s timeout)
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
