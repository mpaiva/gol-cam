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

// Frame-differencing trigger — catches fast-moving balls the edge detector
// misses by counting pixels in the ROI whose raw luma changed > motionPixelDelta
// vs. the previous frame. Calibrated against the quiet-scene noise floor; both
// the per-pixel delta and the absolute threshold are runtime-tunable via REST.
uint8_t* prevFrame = nullptr;            // PSRAM copy of last raw luma frame
uint8_t* lumaBuf  = nullptr;             // per-frame Y plane derived from RGB565 fb
volatile bool prevFrameValid = false;

// Colour-match trigger — fires when enough ROI pixels match the ball's
// learned chroma (U, V). Hue-invariant to luma so the IR hot-spot, room
// lighting, and shadows don't confuse it the way the motion trigger
// could. Target U/V are sampled at /calibrate.
volatile int targetU = 0;                // calibrated ball chroma U (-128..127)
volatile int targetV = 0;                // calibrated ball chroma V (-128..127)
volatile int colorTol = 28;              // max |Δu|+|Δv| for a pixel to "match"
volatile int colorThreshold = 0;         // 0 = disabled; pixels-matching needed to fire
volatile int calColorFloor = 0;          // peak match count seen in quiet calibration scene
volatile int lastColorMatch = 0;         // live match count
volatile int lastColorBboxPct = 0;       // live match-bbox area as % of ROI
volatile int motionPixelDelta = 30;      // |Δluma| above this counts a pixel as "moving" (/motion-delta)
volatile int motionThreshold = 0;        // 0 = disabled; pixels-moving needed to fire (/motion-threshold)
volatile int motionBboxMaxPct = 30;      // reject trigger when motion bbox covers > N% of ROI (/motion-bbox-max)
volatile int calMotionFloor = 0;         // peak noise motion count observed during cal
volatile int lastMotion = 0;             // live motion count for diagnostics
volatile int lastMotionBboxPct = 0;      // live motion bbox area as % of ROI (diagnostic)

// Detection params
#define COOLDOWN_MS 10000
#define STABLE_FRAMES_NEEDED 1
// During PLAYING, only encode 1 in playStreamSkip frames to leave CPU for the
// detection loop (JPEG dominates the per-frame cost). A goal-frame always encodes.
// Runtime-tunable via /stream-skip; 1 disables throttling.
volatile int playStreamSkip = 3;

// ROI offset and size for digital pan/resize (adjusted via /roi endpoint).
// Default + auto-tune lock: 304×160 centered (matches a typical button-soccer
// goal frame's wide-and-short aspect — fills the goal mouth without the side
// posts). Auto-tune resets the ROI to this baseline so calibration is
// reproducible across boards.
#define AUTOTUNE_ROI_W 304
#define AUTOTUNE_ROI_H 160
volatile int roiOffsetX = 0, roiOffsetY = 0;
volatile int roiW = AUTOTUNE_ROI_W, roiH = AUTOTUNE_ROI_H;

// Shared detection state for web console
volatile int lastMatchCount = 0, lastBboxW = 0, lastBboxH = 0;
volatile int lastMinPx = 0, lastMaxPx = 0, lastMaxBbox = 0;
volatile float lastDensity = 0;
volatile const char* lastRejectReason = "";

// Last-detected dadinho bbox (in DETECT_W/H pixel coords). Set during STATE_PLAYING
// when the dice passes the size/density filters; consumed by the dashboard's SVG
// overlay. -1 = no current detection.
volatile int diceBboxX = -1, diceBboxY = -1, diceBboxW = 0, diceBboxH = 0;

// Calibration snapshot
uint8_t* calSnapshotBuf = nullptr;
size_t calSnapshotLen = 0;
SemaphoreHandle_t calSnapshotMutex = nullptr;
char calFeedback[256] = "";

// Goal snapshot — pair of JPEGs captured atomically when isGoal fires:
//   goalSnapshotBuf      = current raw frame (the moment we decided "goal")
//   goalSnapshotPrevBuf  = previous raw frame (one frame earlier, often the
//                          actual ball-in-flight evidence for motion triggers)
// Both share goalSnapshotMutex and bump goalSnapshotSeq together.
uint8_t* goalSnapshotBuf = nullptr;
size_t goalSnapshotLen = 0;
uint8_t* goalSnapshotPrevBuf = nullptr;
size_t goalSnapshotPrevLen = 0;
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
char scoreboardSide = 'a';

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

// Set by /test-fire; drained by loop() at the top of the next iteration so the
// VAR snapshot captures a fresh fb. Bypasses cooldown/calibration — for end-to-end
// dashboard validation without a physical ball.
volatile bool pendingTestFire = false;
void requestTestFire() {
    pendingTestFire = true;
    Serial.println("[test] /test-fire requested");
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
    goalSnapshotPrevLen = 0;
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
    goalSnapshotPrevBuf = (uint8_t*)ps_malloc(64 * 1024);
    goalSnapshotMutex = xSemaphoreCreateMutex();
    if (!goalSnapshotBuf || !goalSnapshotPrevBuf || !goalSnapshotMutex) {
        Serial.println("WARN: goal snapshot alloc failed");
    }

    // Previous-frame buffer for the motion-difference trigger (Y plane only).
    // QVGA = 76 800 B; sits in PSRAM. If alloc fails the motion path stays disabled.
    prevFrame = (uint8_t*)ps_malloc(320 * 240);
    if (!prevFrame) Serial.println("WARN: prevFrame alloc failed — motion trigger disabled");

    // Luma plane derived from the RGB565 framebuffer each iteration so the
    // existing Sobel + motion code keeps working on 8-bit Y (no rewrite).
    lumaBuf = (uint8_t*)ps_malloc(320 * 240);
    if (!lumaBuf) Serial.println("WARN: lumaBuf alloc failed — detection disabled");

    if (!initCamera(FRAMESIZE_QVGA, PIXFORMAT_RGB565)) {
        Serial.println("FATAL: Camera init failed!");
        while (true) delay(1000);
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_contrast(s, 1);
        s->set_brightness(s, -1);
        s->set_sharpness(s, 1);
        // RGB mode — no grayscale special effect.
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

// RGB565 → 8-bit luma plane in lumaBuf. ESP camera stores RGB565 big-endian
// (hi byte first), so swap when unpacking. Y = (2R + 5G + B) / 8 approximates
// ITU-R 601 luma with integer math.
static inline void rgb565ToLuma(const uint8_t* src, uint8_t* dst, int n) {
    for (int i = 0; i < n; i++) {
        uint16_t px = ((uint16_t)src[i*2] << 8) | src[i*2 + 1];
        int r = ((px >> 11) & 0x1F) * 255 / 31;
        int g = ((px >> 5)  & 0x3F) * 255 / 63;
        int b =  (px        & 0x1F) * 255 / 31;
        dst[i] = (uint8_t)((r * 2 + g * 5 + b) >> 3);
    }
}

// Decode one RGB565 pixel into 8-bit R/G/B for the colour-match path.
static inline void rgb565Decode(uint16_t px, int* r, int* g, int* b) {
    *r = ((px >> 11) & 0x1F) * 255 / 31;
    *g = ((px >> 5)  & 0x3F) * 255 / 63;
    *b =  (px        & 0x1F) * 255 / 31;
}

// Compute chroma (U, V) for a single pixel. Hue-invariant pair — same colour
// at different brightnesses gives nearly the same (U, V).
//   Y = (2R + 5G + B) / 8
//   U = R − Y   (luma-shifted red)
//   V = B − Y   (luma-shifted blue)
static inline void rgbToUV(int r, int g, int b, int* u, int* v) {
    int y = (r * 2 + g * 5 + b) >> 3;
    *u = r - y;
    *v = b - y;
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
        // Wipe stale calibration so /status.calibrated honestly reports false
        // and the dashboards drop back to "Idle" until a successful recal.
        calContrastMin = 0; calPixelCount = 0;
        calBboxW = 0; calBboxH = 0;
        motionThreshold = 0; calMotionFloor = 0;
        colorThreshold = 0; targetU = 0; targetV = 0;
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
        // Wipe stale calibration (see comment above).
        calContrastMin = 0; calPixelCount = 0;
        calBboxW = 0; calBboxH = 0;
        motionThreshold = 0; calMotionFloor = 0;
        colorThreshold = 0; targetU = 0; targetV = 0;
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

    // Sample the ball's chroma signature inside the edge-bbox so the
    // colour-match trigger has a target (U, V). UV is luma-invariant, so the
    // IR hot-spot (low saturation, high luma) won't fool it and lighting
    // shifts that mostly change Y barely move the match.
    if (fb->format == PIXFORMAT_RGB565 && calBboxW > 0 && calBboxH > 0) {
        long sumU = 0, sumV = 0;
        int nSamp = 0;
        const uint8_t* src = (const uint8_t*)fb->buf;
        for (int y = minY; y <= maxY; y++) {
            for (int x = minX; x <= maxX; x++) {
                int idx = (y * DETECT_W + x) * 2;
                uint16_t px = ((uint16_t)src[idx] << 8) | src[idx + 1];
                int r, g, b, u, v;
                rgb565Decode(px, &r, &g, &b);
                rgbToUV(r, g, b, &u, &v);
                sumU += u; sumV += v; nSamp++;
            }
        }
        if (nSamp > 0) {
            targetU = (int)(sumU / nSamp);
            targetV = (int)(sumV / nSamp);
            // Set the colour-match trigger threshold proportional to bbox area
            // (the ball should produce ≥ 30% of the bbox area in matching
            // pixels). Operator can override via /color-threshold.
            int bboxArea = calBboxW * calBboxH;
            colorThreshold = max(20, bboxArea * 30 / 100);
            Serial.printf("[cal] colour target U=%d V=%d  threshold=%d (bbox %d px)\n",
                (int)targetU, (int)targetV, (int)colorThreshold, bboxArea);
            size_t fbLen = strlen(calFeedback);
            snprintf(calFeedback + fbLen, sizeof(calFeedback) - fbLen,
                " Colour: U=%d V=%d, trigger>=%d.",
                (int)targetU, (int)targetV, (int)colorThreshold);
        }
    }

    // Sample the quiet-scene motion floor inside the ROI so the
    // frame-differencing trigger has a sensible threshold. For each frame we
    // first compute the mean |Δluma| across the ROI (the DC component — what
    // a uniform lighting shift looks like) and then count pixels whose
    // |Δluma| − mean > motionPixelDelta. The subtraction makes the trigger
    // immune to global brightness swings (clouds, room lights, auto-WB
    // hiccups) while keeping a localised object (the ball) clearly above
    // threshold. Peak across 6 samples × 4 + slack gives the absolute
    // motion-pixel count required to fire.
    if (prevFrame && lumaBuf && fb->format == PIXFORMAT_RGB565) {
        // pixels here is lumaBuf (already populated by the loop pre-amble).
        memcpy(prevFrame, pixels, (size_t)DETECT_W * DETECT_H);
        prevFrameValid = true;
        int noisePeak = 0;
        for (int i = 0; i < 6; i++) {
            delay(50);
            camera_fb_t* f = esp_camera_fb_get();
            if (!f) continue;
            if (f->format != PIXFORMAT_RGB565 || f->len < (size_t)(DETECT_W * DETECT_H * 2)) {
                esp_camera_fb_return(f);
                continue;
            }
            rgb565ToLuma(f->buf, lumaBuf, DETECT_W * DETECT_H);
            int npix = rw * rh;
            long sumD = 0;
            for (int y = ry; y < ry + rh; y++) {
                const uint8_t* a = lumaBuf + y * DETECT_W + rx;
                const uint8_t* b = (const uint8_t*)prevFrame + y * DETECT_W + rx;
                for (int x = 0; x < rw; x++) {
                    int d = (int)a[x] - (int)b[x];
                    if (d < 0) d = -d;
                    sumD += d;
                }
            }
            int meanD = npix > 0 ? (int)(sumD / npix) : 0;
            int mc = 0;
            for (int y = ry; y < ry + rh; y++) {
                const uint8_t* a = lumaBuf + y * DETECT_W + rx;
                const uint8_t* b = (const uint8_t*)prevFrame + y * DETECT_W + rx;
                for (int x = 0; x < rw; x++) {
                    int d = (int)a[x] - (int)b[x];
                    if (d < 0) d = -d;
                    if (d - meanD > motionPixelDelta) mc++;
                }
            }
            if (mc > noisePeak) noisePeak = mc;
            memcpy(prevFrame, lumaBuf, (size_t)DETECT_W * DETECT_H);
            esp_camera_fb_return(f);
        }
        calMotionFloor = noisePeak;
        int roiPx = rw * rh;
        // 4x peak noise + small per-ROI slack so a quiet scene + jitter still
        // sits well below trigger. Caps at roiPx/2 (anything more is a hand
        // sweep, not a ball).
        int slack = roiPx / 200; if (slack < 50) slack = 50;
        int t = noisePeak * 4 + slack;
        int cap = roiPx / 2;
        if (t > cap) t = cap;
        motionThreshold = t;
        Serial.printf("[cal] motion floor=%d → threshold=%d (roi %d px)\n",
            calMotionFloor, (int)motionThreshold, roiPx);
        size_t fbLen = strlen(calFeedback);
        snprintf(calFeedback + fbLen, sizeof(calFeedback) - fbLen,
            " Motion: floor=%d, trigger>=%d.", calMotionFloor, (int)motionThreshold);
    }

    // Flash LED to confirm
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);

    gameState = STATE_IDLE;
}

// Autotune objective: Sobel gradient energy with a multiplicative
// saturation penalty. The IR LED can't be turned off so the ROI always
// has a bright hot-spot — gradient-alone would keep pushing exposure up
// because the hot-spot's rim still has crisp edges. The multiplier
// punishes that by counting pixels that hit either clip rail:
//
//   sat = #pixels with luma ≥ 250 or ≤ 5  (out of N total)
//   mult = max(0, (N − 3·sat) / N)
//
// 0% sat → multiplier 1 (full gradient score)
// 10% sat → multiplier 0.7
// 33%+ sat → multiplier 0 (entire frame disqualified)
//
// A small dadinho at 240-ish doesn't trigger the penalty; a glowing IR
// hot-spot covering even 15% of the ROI cuts the score in half. Result:
// the optimiser settles where the dadinho is crisp AND the surface
// stays below clip — the auto-contrast behaviour the operator wanted.
long roiGradEnergy(uint8_t* pixels, int w, int rx, int ry, int rw, int rh) {
    int sx = max(1, rx);
    int sy = max(1, ry);
    int ex = min(w - 2, rx + rw - 1);
    int ey = min(DETECT_H - 2, ry + rh - 1);
    long gradSum = 0;
    int sat = 0;
    int total = 0;
    for (int y = sy; y <= ey; y++) {
        for (int x = sx; x <= ex; x++) {
            uint8_t v = pixels[y * w + x];
            if (v >= 250 || v <= 5) sat++;
            total++;
            gradSum += sobelMag(pixels, x, y, w);
        }
    }
    if (total == 0) return 0;
    long mult_num = (long)total - (long)sat * 3;
    if (mult_num < 0) mult_num = 0;
    return gradSum * mult_num / total;
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
// Returns ROI Sobel-gradient energy — the autotune objective. Higher = more
// edge content in the goal mouth, which is exactly what both the edge and
// the motion detectors consume. Crucially, saturated regions (the IR hot
// spot) are flat and contribute nothing, so the optimiser stops chasing
// extreme exposures that wash out the dadinho.
// Set pushStreamFrame=true to also publish this frame to the MJPEG stream so
// the dashboard doesn't go stale during long sweeps (called once per stage).
static long captureAndScore(const CamSettings& c, bool pushStreamFrame) {
    applyCamSettings(c);
    // Discard 2 frames to let exposure/gain settle. esp_camera_fb_get blocks
    // until a new frame is available, so no extra delay is needed.
    for (int i = 0; i < 2; i++) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb || fb->format != PIXFORMAT_RGB565 || !lumaBuf) {
        if (fb) esp_camera_fb_return(fb);
        return 0;
    }
    rgb565ToLuma(fb->buf, lumaBuf, DETECT_W * DETECT_H);
    int rx, ry, rw, rh;
    computeROI(rx, ry, rw, rh);
    long score = roiGradEnergy(lumaBuf, DETECT_W, rx, ry, rw, rh);
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
// Updates `*best` and `*bestScore` if any tested value beats current best.
// `field` is a pointer-to-member-style int* that lives inside `*best`.
static void sweepParam(CamSettings* best, long* bestScore,
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
        // Push the LAST capture of each sweep to the stream so the browser
        // sees a fresh frame every ~300-500 ms during the autotune.
        bool publish = (i == nVals - 1);
        long score = captureAndScore(*best, publish);
        Serial.printf("[tune]  %s=%d  score=%ld\n", label, values[i], score);
        snprintf(calFeedback, sizeof(calFeedback),
            "Auto-tune: %s %d/%d  best=%ld", label, i + 1, nVals, *bestScore);
        if (score > *bestScore) {
            *bestScore = score; bestVal = values[i];
        }
    }
    *field = bestVal;            // lock in winning value
    applyCamSettings(*best);     // re-apply so sensor + cur* reflect the running best
}

void runAutotune() {
    // Lock the ROI to the canonical 304×160 @ centre. This makes calibration
    // reproducible across boards and frames — the operator can still nudge
    // the ROI manually after auto-tune via the dpad/W·H buttons if needed.
    roiW = AUTOTUNE_ROI_W;
    roiH = AUTOTUNE_ROI_H;
    roiOffsetX = 0;
    roiOffsetY = 0;
    Serial.printf("[tune] ROI reset to %d×%d @ 0,0\n", AUTOTUNE_ROI_W, AUTOTUNE_ROI_H);

    // Single-pass greedy sweep over the parameters that move the needle most
    // for separating a bright dadinho from a dark background. Other knobs
    // (sharp/gma/lenc/con) had marginal impact in testing — drop them for speed.
    static const int gainVals[]  = {0, 10, 20, 30};
    static const int gceilVals[] = {0, 3, 6};
    static const int aecVals[]   = {120, 280, 480, 800, 1200};
    static const int briVals[]   = {-1, 0, 1};

    CamSettings best = { .gain=8, .gceil=2, .aec=300, .gma=0, .lenc=0, .con=1, .bri=0, .sharp=1 };
    long bestScore = 0;

    sweepParam(&best, &bestScore, &best.gain,  gainVals,  4, "gain",  1);
    sweepParam(&best, &bestScore, &best.gceil, gceilVals, 3, "gceil", 2);
    sweepParam(&best, &bestScore, &best.aec,   aecVals,   5, "aec",   3);
    sweepParam(&best, &bestScore, &best.bri,   briVals,   3, "bri",   4);

    applyCamSettings(best);
    autotuneBestScore  = (int)(bestScore > 2147483647L ? 2147483647L : bestScore);
    autotuneBestGain   = best.gain;
    autotuneBestGceil  = best.gceil;
    autotuneBestAec    = best.aec;
    autotuneBestGma    = best.gma;
    autotuneBestLenc   = best.lenc;
    autotuneBestCon    = best.con;
    autotuneBestBri    = best.bri;
    autotuneBestSharp  = best.sharp;
    autotuneDone = 1;

    // Gradient-energy scale: a quiet flat scene runs ~5-20k; a sharp dadinho
    // in good light easily clears 200k. Below ~50k there's barely any edge
    // structure to lock onto.
    if (bestScore < 50000) {
        snprintf(calFeedback, sizeof(calFeedback),
            "Auto-tune done — low edge content (score %ld). Place dadinho in ROI.", bestScore);
    } else {
        snprintf(calFeedback, sizeof(calFeedback),
            "Auto-tune done! gain=%d gceil=%d aec=%d bri=%d  score=%ld",
            best.gain, best.gceil, best.aec, best.bri, bestScore);
    }
    Serial.printf("[tune] DONE  gain=%d gceil=%d aec=%d bri=%d score=%ld\n",
        best.gain, best.gceil, best.aec, best.bri, bestScore);

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

    // Camera is RGB565 (2 bytes/pixel). Derive a Y plane in lumaBuf each frame
    // so the existing Sobel + motion code keeps reading 8-bit luma at &pixels[idx].
    size_t expectedLen = (size_t)DETECT_W * DETECT_H * 2;
    if (fb->format != PIXFORMAT_RGB565 || fb->len < expectedLen || !lumaBuf) {
        esp_camera_fb_return(fb);
        delay(100);
        return;
    }
    rgb565ToLuma(fb->buf, lumaBuf, DETECT_W * DETECT_H);
    uint8_t* pixels = lumaBuf;

    // Test-fire: simulates a real goal without the detection pipeline so the
    // dashboard can be validated end-to-end (VAR lightbox + scoreboard push +
    // audio celebration) without a physical ball. Bypasses cooldown and
    // calibration. Captures current fb + prevFrame as the two VAR snapshots.
    if (pendingTestFire) {
        pendingTestFire = false;

        if (goalSnapshotBuf && goalSnapshotPrevBuf && goalSnapshotMutex) {
            uint8_t* var_jpg = NULL;  size_t var_len = 0;
            bool curOk = frame2jpg(fb, 80, &var_jpg, &var_len);

            uint8_t* prev_jpg = NULL; size_t prev_len = 0;
            bool prevOk = false;
            if (prevFrame && prevFrameValid) {
                prevOk = fmt2jpg(prevFrame, (size_t)DETECT_W * DETECT_H,
                                 DETECT_W, DETECT_H, PIXFORMAT_GRAYSCALE, 80,
                                 &prev_jpg, &prev_len);
            }
            xSemaphoreTake(goalSnapshotMutex, portMAX_DELAY);
            if (curOk && var_len <= 64 * 1024) {
                memcpy(goalSnapshotBuf, var_jpg, var_len);
                goalSnapshotLen = var_len;
            }
            if (prevOk && prev_len <= 64 * 1024) {
                memcpy(goalSnapshotPrevBuf, prev_jpg, prev_len);
                goalSnapshotPrevLen = prev_len;
            } else {
                goalSnapshotPrevLen = 0;
            }
            if (curOk) goalSnapshotSeq++;
            xSemaphoreGive(goalSnapshotMutex);
            if (var_jpg)  free(var_jpg);
            if (prev_jpg) free(prev_jpg);
        }

        lastGoalTimeMs = now;
        detector.goalCount++;
        goalJustScored = true;
        playGoalSound();
        pushGoalToScoreboard();
        digitalWrite(LED_PIN, HIGH); delay(300); digitalWrite(LED_PIN, LOW);
        Serial.printf("[test] GOAL #%d via /test-fire (side=%c)\n",
                      detector.goalCount, scoreboardSide);
        esp_camera_fb_return(fb);
        return;
    }

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
    // are drawn client-side as SVG over the <img>). Update prevFrame from raw
    // luma BEFORE binarisation so motion detection has a fresh reference when
    // play starts.
    if ((gameState != STATE_PLAYING) || calContrastMin <= 0) {
        if (prevFrame) {
            memcpy(prevFrame, fb->buf, (size_t)DETECT_W * DETECT_H);
            prevFrameValid = true;
        }
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

    // Frame-differencing trigger — counts pixels in the ROI whose raw luma
    // moved noticeably vs. the previous frame. To stay robust against global
    // lighting shifts (clouds, room lights, auto-WB hiccups), we first
    // compute the mean |Δluma| across the ROI and subtract it: a uniform
    // brightness change has every pixel near the mean, so deviation ≈ 0 and
    // no pixel exceeds the per-pixel delta. A localised object still spikes
    // far above the mean and gets counted. Catches balls that flash through
    // the ROI in 1-2 frames (too brief for the edge detector to lock on).
    int motionCount = 0;
    int motionMean = 0;
    int motionBboxArea = 0;
    int motionBboxPct = 0;
    if (prevFrame && prevFrameValid && motionThreshold > 0) {
        int npix = rw * rh;
        long sumD = 0;
        for (int y = ry; y < ry + rh; y++) {
            const uint8_t* a = pixels + y * DETECT_W + rx;
            const uint8_t* b = prevFrame + y * DETECT_W + rx;
            for (int x = 0; x < rw; x++) {
                int d = (int)a[x] - (int)b[x];
                if (d < 0) d = -d;
                sumD += d;
            }
        }
        motionMean = npix > 0 ? (int)(sumD / npix) : 0;
        // Second pass: count motion pixels and track the bbox enclosing them.
        // A localised ball produces a tight bbox; a non-uniform lighting
        // change (cloud shadow, room-light gradient, hand sweep) spreads
        // across most of the ROI even after mean subtraction.
        int minLX = rw, maxLX = -1, minLY = rh, maxLY = -1;
        for (int y = 0; y < rh; y++) {
            const uint8_t* a = pixels + (ry + y) * DETECT_W + rx;
            const uint8_t* b = prevFrame + (ry + y) * DETECT_W + rx;
            for (int x = 0; x < rw; x++) {
                int d = (int)a[x] - (int)b[x];
                if (d < 0) d = -d;
                if (d - motionMean > motionPixelDelta) {
                    motionCount++;
                    if (x < minLX) minLX = x;
                    if (x > maxLX) maxLX = x;
                    if (y < minLY) minLY = y;
                    if (y > maxLY) maxLY = y;
                }
            }
        }
        if (motionCount > 0) {
            motionBboxArea = (maxLX - minLX + 1) * (maxLY - minLY + 1);
            motionBboxPct = (npix > 0) ? (motionBboxArea * 100 / npix) : 0;
        }
    }
    lastMotion = motionCount;
    lastMotionBboxPct = motionBboxPct;

    // Colour-match trigger — counts ROI pixels whose chroma (U, V) matches
    // the ball's calibrated colour, then gates on bbox compactness like
    // motion does. UV is hue-invariant so this trigger is immune to the IR
    // hot-spot (low saturation) and to lighting shifts (mostly luma).
    int colorCount = 0;
    int colorBboxPct = 0;
    if (colorThreshold > 0 && fb->format == PIXFORMAT_RGB565) {
        const uint8_t* src = (const uint8_t*)fb->buf;
        int tU = targetU, tV = targetV, tol = colorTol;
        int minLX = rw, maxLX = -1, minLY = rh, maxLY = -1;
        for (int y = 0; y < rh; y++) {
            int rowBase = ((ry + y) * DETECT_W + rx) * 2;
            for (int x = 0; x < rw; x++) {
                int idx = rowBase + x * 2;
                uint16_t px = ((uint16_t)src[idx] << 8) | src[idx + 1];
                int r, g, b, u, v;
                rgb565Decode(px, &r, &g, &b);
                rgbToUV(r, g, b, &u, &v);
                int du = u - tU; if (du < 0) du = -du;
                int dv = v - tV; if (dv < 0) dv = -dv;
                if (du + dv <= tol) {
                    colorCount++;
                    if (x < minLX) minLX = x;
                    if (x > maxLX) maxLX = x;
                    if (y < minLY) minLY = y;
                    if (y > maxLY) maxLY = y;
                }
            }
        }
        if (colorCount > 0) {
            int bArea = (maxLX - minLX + 1) * (maxLY - minLY + 1);
            int npix = rw * rh;
            colorBboxPct = (npix > 0) ? (bArea * 100 / npix) : 0;
        }
    }
    lastColorMatch = colorCount;
    lastColorBboxPct = colorBboxPct;

    // Compute the goal decision before encoding so isGoal can drive the VAR
    // snapshot capture below.
    detector.lastChangeRatio = (float)matchCount / (rw * rh);
    detector.frameCount = frameNum;
    bool inCooldown = (now - lastGoalTimeMs) < COOLDOWN_MS;
    bool edgeTrigger = diceDetected && !diceWasPresent && noDiceFrames >= STABLE_FRAMES_NEEDED;
    // Motion trigger requires BOTH enough motion pixels AND a compact bbox —
    // diffuse motion (lighting) gets rejected even when the pixel count is high.
    bool motionTrigger = (motionThreshold > 0)
                      && (motionCount > motionThreshold)
                      && (motionBboxPct <= motionBboxMaxPct);
    // Colour trigger uses the same bbox compactness gate as motion.
    bool colorTrigger = (colorThreshold > 0)
                     && (colorCount > colorThreshold)
                     && (colorBboxPct <= motionBboxMaxPct);
    bool isGoal = !inCooldown && (edgeTrigger || motionTrigger || colorTrigger);

    // VAR snapshots — encode TWO raw grayscale JPEGs at quality 80 when isGoal
    // fires:
    //   (a) current fb = "trigger frame" (what we saw when we decided to score)
    //   (b) prevFrame  = "before" frame (one earlier; for motion triggers this
    //                    is often where the ball was actually visible).
    // Both must be encoded BEFORE the memcpy that overwrites prevFrame. Mutex
    // held for the whole pair so the dashboard never reads a half-updated set.
    if (isGoal && goalSnapshotBuf && goalSnapshotPrevBuf && goalSnapshotMutex) {
        uint8_t* var_jpg = NULL;
        size_t var_len = 0;
        bool curOk = frame2jpg(fb, 80, &var_jpg, &var_len);

        uint8_t* prev_jpg = NULL;
        size_t prev_len = 0;
        bool prevOk = false;
        if (prevFrame && prevFrameValid) {
            prevOk = fmt2jpg(prevFrame, (size_t)DETECT_W * DETECT_H,
                             DETECT_W, DETECT_H, PIXFORMAT_GRAYSCALE, 80,
                             &prev_jpg, &prev_len);
        }

        xSemaphoreTake(goalSnapshotMutex, portMAX_DELAY);
        if (curOk && var_len <= 64 * 1024) {
            memcpy(goalSnapshotBuf, var_jpg, var_len);
            goalSnapshotLen = var_len;
        }
        if (prevOk && prev_len <= 64 * 1024) {
            memcpy(goalSnapshotPrevBuf, prev_jpg, prev_len);
            goalSnapshotPrevLen = prev_len;
        } else {
            // No prev available (first goal after calibrate/reset). Mark
            // empty so the dashboard knows not to show it.
            goalSnapshotPrevLen = 0;
        }
        if (curOk) goalSnapshotSeq++;
        xSemaphoreGive(goalSnapshotMutex);

        if (var_jpg)  free(var_jpg);
        if (prev_jpg) free(prev_jpg);
    }

    // Stash this frame as the motion-diff reference for the next iteration.
    if (prevFrame) {
        memcpy(prevFrame, pixels, (size_t)DETECT_W * DETECT_H);
        prevFrameValid = true;
    }

    // Publish dadinho bbox so the dashboard SVG overlay can draw it in green
    if (matchCount > 0 && diceDetected) {
        diceBboxX = minX; diceBboxY = minY;
        diceBboxW = maxX - minX + 1; diceBboxH = maxY - minY + 1;
    } else {
        diceBboxX = -1;
    }

    // Encode the (binarized) grayscale frame for the live stream — overlays
    // drawn client-side. JPEG encoding dominates per-frame CPU so we encode
    // 1-in-playStreamSkip frames during play (always on a goal so the dashboard
    // sees the goal moment even if it falls between regular stream frames).
    bool shouldEncode = (playStreamSkip <= 1) || (frameNum % playStreamSkip == 0) || isGoal;
    if (shouldEncode) {
        uint8_t* jpg_buf = NULL;
        size_t jpg_len = 0;
        if (frame2jpg(fb, 70, &jpg_buf, &jpg_len)) {
            frameStore.update(jpg_buf, jpg_len);
            free(jpg_buf);
        }
    }
    esp_camera_fb_return(fb);

    static uint32_t lastDetectLog = 0;
    if ((matchCount > 0 || motionCount > motionThreshold / 2) && (now - lastDetectLog >= 500)) {
        const char* reason = "";
        if (!diceDetected) {
            if ((int)matchCount > maxPixels) reason = " TOO-BIG";
            else if (bboxW > maxBbox || bboxH > maxBbox) reason = " BBOX-BIG";
            else if ((int)matchCount < minPixels) reason = " TOO-SMALL";
            else if (density < 0.12f) reason = " SPARSE";
        }
        bool motionBboxFail = (motionThreshold > 0) && (motionCount > motionThreshold)
                              && (motionBboxPct > motionBboxMaxPct);
        bool colorBboxFail  = (colorThreshold > 0)  && (colorCount  > colorThreshold)
                              && (colorBboxPct  > motionBboxMaxPct);
        Serial.printf("[detect] e=%d bbox=%dx%d dens=%.0f%% m=%d/%d mBb=%d%% c=%d/%d cBb=%d%%/%d%%%s%s%s%s%s%s\n",
            matchCount, bboxW, bboxH, density * 100,
            motionCount, (int)motionThreshold, motionBboxPct,
            colorCount, (int)colorThreshold, colorBboxPct, (int)motionBboxMaxPct,
            diceDetected ? " DICE!" : reason,
            motionTrigger ? " MOTION!" : "",
            colorTrigger ? " COLOUR!" : "",
            motionBboxFail ? " M-DIFFUSE" : "",
            colorBboxFail ? " C-DIFFUSE" : "",
            inCooldown ? " (cd)" : "");
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

        // Source = first letter of each firing trigger: e (edge), m (motion), c (colour)
        char trigSrc[8] = {0};
        int ti = 0;
        if (edgeTrigger)   trigSrc[ti++] = 'e';
        if (motionTrigger) trigSrc[ti++] = 'm';
        if (colorTrigger)  trigSrc[ti++] = 'c';
        Serial.printf("GOAL #%d! src=%s (edges=%d motion=%d colour=%d  th edge>=%d motion>=%d colour>=%d)\n",
            detector.goalCount, trigSrc,
            matchCount, motionCount, colorCount,
            (int)calContrastMin, (int)motionThreshold, (int)colorThreshold);
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
