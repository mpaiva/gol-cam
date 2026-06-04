// =============================================================
// gol-cam HMI — CrowPanel ESP32-S3 5" 800x480 touchscreen placar
//
// Phase 3a: passive placar mirror + touch-driven operator surface.
// The CrowPanel polls the existing MAX7219 placar + both cameras over
// HTTP, renders a clear A × B score, and exposes four big buttons
// (Calibrate A / Start-Pause / Reset All / Calibrate B) that fan
// HTTP requests out to the cameras + placar — so a match can be run
// from the panel alone, no laptop required.
//
// Phase 3b will layer VAR review (goal thumbnails + Foi/Anula modal)
// on top of this surface.
//
// Hardware: CrowPanel DIS07050H
//   MCU         ESP32-S3-WROOM-1-N4R8 (240 MHz, 4 MB Flash, 8 MB PSRAM)
//   Display     800x480 IPS, parallel RGB (ILI6122 / ILI5960)
//   Touch       Capacitive GT911, I2C (SDA 19, SCL 20, polling mode)
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_http_server.h"
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#ifndef WIFI_SSID
#error "WIFI_SSID not defined — set it in .env (see CLAUDE.md)"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined — set it in .env"
#endif
#ifndef SCOREBOARD_IP
#error "SCOREBOARD_IP not defined — set it in .env"
#endif

// Camera IPs. Default to the static IPs from the standard scheme; can be
// overridden in .env via CAM_A_IP / CAM_B_IP if your network differs.
#ifndef CAM_A_IP
#define CAM_A_IP "192.168.40.90"
#endif
#ifndef CAM_B_IP
#define CAM_B_IP "192.168.40.91"
#endif

// CrowPanel DIS07050H pinout — bare B0/R0/.. collide with Arduino's bit
// macros so we prefix with the channel.
namespace pins {
    constexpr int LCD_B0=8,  LCD_B1=3,  LCD_B2=46, LCD_B3=9,  LCD_B4=1;
    constexpr int LCD_G0=5,  LCD_G1=6,  LCD_G2=7,  LCD_G3=15, LCD_G4=16, LCD_G5=4;
    constexpr int LCD_R0=45, LCD_R1=48, LCD_R2=47, LCD_R3=21, LCD_R4=14;
    constexpr int LCD_HSYNC=39, LCD_VSYNC=41, LCD_DE=40, LCD_PCLK=0;
    constexpr int LCD_BACKLIGHT = 2;
    constexpr int TOUCH_SDA=19, TOUCH_SCL=20;
    constexpr int TOUCH_INT=-1, TOUCH_RST=-1;   // CrowPanel: not wired separately
}

// Parallel RGB panel + auto-flushed canvas. Standard ILI6122 timing —
// the wider HBP=43 / VBP=12 vs the library's tighter 8/4/8 defaults is
// what makes the full 800×480 actually render.
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    pins::LCD_DE, pins::LCD_VSYNC, pins::LCD_HSYNC, pins::LCD_PCLK,
    pins::LCD_R0, pins::LCD_R1, pins::LCD_R2, pins::LCD_R3, pins::LCD_R4,
    pins::LCD_G0, pins::LCD_G1, pins::LCD_G2, pins::LCD_G3, pins::LCD_G4, pins::LCD_G5,
    pins::LCD_B0, pins::LCD_B1, pins::LCD_B2, pins::LCD_B3, pins::LCD_B4,
    1, 8, 4, 43,
    1, 8, 4, 12,
    1,
    16000000
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    800, 480, rgbpanel, 0, true);

// Minimal inline GT911 driver. The TAMC_GT911 lib chokes on the
// CrowPanel revision that doesn't expose INT/RST (it tries to pinMode
// -1 which throws "Invalid pin" errors and leaves the lib in a partial
// init state). We do polling reads of the chip's status + coord
// registers directly over Wire.
namespace gt911 {
    constexpr uint8_t ADDR              = 0x5D;
    constexpr uint16_t REG_COMMAND      = 0x8040;  // 0=normal, 2=soft reset
    constexpr uint16_t REG_PROD_ID      = 0x8140;  // 4 ASCII chars
    constexpr uint16_t REG_FW_VER       = 0x8144;  // 2 bytes LE
    constexpr uint16_t REG_STATUS       = 0x814E;  // bit7=ready, low 4=#points
    constexpr uint16_t REG_POINT1       = 0x8150;  // 7 bytes per point
    static int  lastX = -1, lastY = -1;
    static bool touched = false;
    static uint32_t pollCount = 0;
    static uint32_t touchCount = 0;
    // Diagnostic state — exposed via /debug/touch so we can probe the chip
    // from outside without a serial cable.
    static uint8_t  lastStatus = 0;       // raw 0x814E byte from most recent poll
    static uint8_t  bootCfgVer = 0;       // config version read at boot
    static uint8_t  postCfgVer = 0;       // config version after blob write (or boot if no write)
    static bool     wroteBlob = false;    // did we write the fallback config this boot?
    static bool     wroteOk   = false;    // did the chip accept the blob (post-write ver = 0x42)?
    static char     prodId[5] = {0,0,0,0,0};
    static uint16_t fwVer = 0;

    static bool writeReg(uint16_t reg, uint8_t v) {
        Wire.beginTransmission(ADDR);
        Wire.write((uint8_t)(reg >> 8));
        Wire.write((uint8_t)(reg & 0xFF));
        Wire.write(v);
        return Wire.endTransmission() == 0;
    }
    static bool readRegs(uint16_t reg, uint8_t* buf, int n) {
        Wire.beginTransmission(ADDR);
        Wire.write((uint8_t)(reg >> 8));
        Wire.write((uint8_t)(reg & 0xFF));
        if (Wire.endTransmission(false) != 0) return false;
        Wire.requestFrom((int)ADDR, n);
        int i = 0;
        while (Wire.available() && i < n) buf[i++] = Wire.read();
        return i == n;
    }

    // Chunked sequential write — Wire's default 128-byte buffer can't hold
    // the 185-byte config payload in one transaction, so we split it into
    // 16-byte runs and advance the register address each time.
    static bool writeRegs(uint16_t reg, const uint8_t* buf, int n) {
        constexpr int CHUNK = 16;
        for (int off = 0; off < n; off += CHUNK) {
            int len = (n - off > CHUNK) ? CHUNK : (n - off);
            Wire.beginTransmission(ADDR);
            Wire.write((uint8_t)((reg + off) >> 8));
            Wire.write((uint8_t)((reg + off) & 0xFF));
            for (int i = 0; i < len; i++) Wire.write(buf[off + i]);
            if (Wire.endTransmission() != 0) {
                Serial.printf("[gt911] writeRegs chunk @0x%04X (%d bytes) FAILED\n",
                              reg + off, len);
                return false;
            }
        }
        return true;
    }

    // Verified 800x480 GT911 config blob from bekencorp/armino production
    // firmware (middleware/driver/tp/tp_gt911.c, single-touch variant ver 0x42).
    // 184 cfg bytes (0x8047-0x80FE) + 1 checksum (0x80FF). After writing the
    // 185 bytes, set 0x8100 = 1 ("config_fresh") to apply.
    // Header: ver=0x42, x_max=0x0320 (800), y_max=0x01E0 (480), touch_pts=1.
    static constexpr uint8_t CFG_800x480[185] = {
        0x42, 0x20, 0x03, 0xE0, 0x01, 0x01, 0x3D, 0x00, 0x01, 0x08, 0x28, 0x05, 0x50, 0x32, 0x03, 0x05,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1A, 0x1F, 0x14, 0x8C, 0x24, 0x0A, 0x1B, 0x19,
        0xF4, 0x0A, 0x00, 0x00, 0x00, 0x20, 0x04, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x64, 0x32,
        0x00, 0x00, 0x00, 0x11, 0xB2, 0x94, 0xC5, 0x02, 0x07, 0x00, 0x00, 0x04, 0x8E, 0x16, 0x00, 0x5D,
        0x23, 0x00, 0x3D, 0x38, 0x00, 0x2A, 0x5A, 0x00, 0x22, 0x90, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x14, 0x12, 0x10, 0x0E, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1D, 0x1C,
        0x18, 0x16, 0x14, 0x13, 0x12, 0x10, 0x0F, 0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x4F
    };

    // One-shot: only fires when the chip reports no factory config loaded
    // (0xFF) or a version older than what's compiled in. Otherwise we
    // leave the chip's burned-in tuning alone.
    static bool writeFactoryConfig() {
        Serial.println("[gt911] writing 800x480 config blob (Beken/armino, ver 0x42)");
        if (!writeRegs(0x8047, CFG_800x480, sizeof(CFG_800x480))) {
            Serial.println("[gt911] config payload write FAILED — i2c bus issue?");
            return false;
        }
        // Read back the first 8 bytes BEFORE committing. If these are
        // 0x42,0x20,0x03,0xE0,0x01,... then the chip is accepting writes
        // to the config-RAM region. If they're still 0xFF, the chip is
        // silently rejecting writes (likely a config-lock / version-bump
        // protection issue) and the commit can never succeed.
        uint8_t rb[8] = {0};
        readRegs(0x8047, rb, 8);
        Serial.printf("[gt911] post-write pre-commit readback: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                      rb[0], rb[1], rb[2], rb[3], rb[4], rb[5], rb[6], rb[7]);
        // Goodix two-step commit:
        //   0x8100 = 1 → load config from RAM into working registers
        //   0x8101 = 1 → persist config to chip NVRAM (some units require
        //                this for the version-bump check to clear)
        if (!writeReg(0x8100, 0x01)) {
            Serial.println("[gt911] config commit (0x8100<-1) FAILED");
            return false;
        }
        delay(50);
        if (!writeReg(0x8101, 0x01)) {
            Serial.println("[gt911] config persist (0x8101<-1) FAILED");
            // not fatal — the chip may still scan from RAM
        }
        delay(500);  // generous — datasheet implies ~200ms but some units need more
        uint8_t cfgver = 0;
        readRegs(0x8047, &cfgver, 1);
        Serial.printf("[gt911] post-commit config version = 0x%02X (want 0x42)\n", cfgver);
        // Pre-commit readback proved bytes land; if cfgver reverted, the
        // chip is rejecting the commit (likely version-bump check or
        // panel-tuning self-test failure on factory-blank NVRAM).
        postCfgVer = cfgver;
        wroteOk = (cfgver == 0x42);
        return wroteOk;
    }

    static void begin() {
        uint8_t id[5] = {0};
        if (readRegs(REG_PROD_ID, id, 4)) {
            for (int i = 0; i < 4; i++) prodId[i] = id[i] ? id[i] : '?';
            prodId[4] = 0;
            Serial.printf("[gt911] product_id = \"%s\"\n", prodId);
        } else {
            Serial.println("[gt911] FAILED to read product ID");
        }
        uint8_t fw[2] = {0};
        if (readRegs(REG_FW_VER, fw, 2)) {
            fwVer = ((uint16_t)fw[1] << 8) | fw[0];
            Serial.printf("[gt911] firmware = 0x%04X\n", fwVer);
        }

        // Probe config version. 0xFF = no factory config loaded; the
        // chip will refuse to scan until we write a valid blob. ONLY
        // overwrite when 0xFF — anything else (including older versions)
        // means a factory-tuned config is present and we'd lose the
        // panel-specific drive/sense calibration if we replaced it.
        uint8_t cfgver = 0;
        readRegs(0x8047, &cfgver, 1);
        bootCfgVer = cfgver;
        postCfgVer = cfgver;
        Serial.printf("[gt911] boot config version = 0x%02X\n", cfgver);
        if (cfgver == 0xFF) {
            Serial.println("[gt911] no valid config — writing fallback blob");
            wroteBlob = true;
            writeFactoryConfig();
        } else {
            Serial.println("[gt911] factory config present — leaving as-is");
        }

        // Always end in normal-scan mode with a cleared status byte so
        // the first real touch produces a "buffer ready" event.
        writeReg(REG_COMMAND, 0x00);
        delay(10);
        writeReg(REG_STATUS, 0);
        Serial.println("[gt911] init complete, polling enabled");
    }


    static void poll() {
        pollCount++;
        uint8_t s;
        if (!readRegs(REG_STATUS, &s, 1)) { lastStatus = 0xEE; touched = false; return; }
        lastStatus = s;
        // Diagnostic: print the raw status byte once every 500 polls so we
        // can see what the chip is actually reporting without flooding the
        // serial line. Strip this once the panel is reliably working.
        if (pollCount % 500 == 0) {
            Serial.printf("[gt911] status=0x%02X (poll #%u)\n", s, (unsigned)pollCount);
        }
        if (!(s & 0x80)) { touched = false; return; }
        int n = s & 0x0F;
        if (n == 0) { touched = false; writeReg(REG_STATUS, 0); return; }
        uint8_t pt[7];
        if (!readRegs(REG_POINT1, pt, 7)) { writeReg(REG_STATUS, 0); return; }
        lastX = pt[0] | (pt[1] << 8);
        lastY = pt[2] | (pt[3] << 8);
        touched = true;
        touchCount++;
        writeReg(REG_STATUS, 0);
    }
}

// =============================================================
// Camera state — both cameras polled every CAM_POLL_MS so we can render
// status pills under HOME/AWAY without serially blocking the touch loop.
// =============================================================
constexpr uint32_t CAM_POLL_MS = 1000;

struct CamState {
    const char* ip;
    bool online = false;
    int  state = -1;          // 0=IDLE, 1=CAL, 2=PLAY, 3=PAUSE
    bool calibrated = false;
    int  goals = 0;
    uint32_t lastPollMs = 0;
    // Extra fields pulled from /status during calibration. None of these
    // are touched by the score path, so racing reads from the main loop
    // are harmless (worst case: one stale frame of overlay text).
    int  motionTh = 0;
    int  colorTh = 0;
    int  calContrast = 0;
    bool hasCalSnap = false;
    char calMsg[40] = {0};
};
static CamState camA;
static CamState camB;

// HMI maintains its own score state — this device IS the placar, so
// /status / /goal / /goal-undo / /api/reset hit local memory rather
// than a downstream MAX7219 board. Cameras push goals here exactly as
// they pushed to the LED placar; the LED placar can be retired or
// kept as a passive mirror.
static volatile int placarA = 0;
static volatile int placarB = 0;
static volatile bool placarOnline = true;  // we're the placar — always "online"
static bool firstFullDraw = true;

// HTTP server on port 80 — same REST shape as src_scoreboard/scoreboard.cpp
// so existing camera firmware can point SCOREBOARD_IP at this device with
// zero protocol changes. Uses esp_http_server (same stack the cameras use)
// instead of WebServer.h — the latter exhausted its 5-socket pool under
// integration-test burst load (port 80 starts refusing connections after
// ~70 rapid requests). esp_http_server allows 10 sockets + LRU purge.
static httpd_handle_t server = NULL;

// Dirty flags set by the background polling task (core 0). The main loop
// (core 1) checks them every iteration and re-renders only what changed,
// so HTTP latency never blocks touch reads or button feedback.
static volatile bool dirtyHeader  = false;
static volatile bool dirtyDigits  = false;
static volatile bool dirtyStatus  = false;
static volatile bool dirtyButtons = false;
static volatile bool dirtyOverlay = false;

// =============================================================
// Calibration overlay — full-screen modal that takes over the score +
// button area while a camera /calibrate is in progress. Tapping CAL A
// or CAL B fires the HTTP request to the camera AND raises the overlay
// for instant feedback (so the operator never wonders whether the tap
// landed). The overlay shows the cam's live `calMsg` plus a coarse
// progress bar driven by hasCalSnap + calContrast.
//
// When the cam transitions from state=1 (CALIBRATING) back to anything
// else, the overlay switches to a 3-second RESULT phase showing OK +
// learned thresholds or FAIL + retry hint, then dismisses itself and
// redraws the underlying placar view.
//
// Touch is *not* fully modal: REST pushes from the cameras (/goal) still
// update placarA/placarB during calibration, but on-screen taps that
// would have hit the placar buttons under the overlay are ignored
// (operator only sees the overlay). One on-overlay "Cancelar" button
// aborts by /reset-ing the cam being calibrated.
// =============================================================
struct CalOverlay {
    int8_t   side;             // 0 = A (HOME), 1 = B (AWAY), -1 = OFF
    int8_t   phase;            // 0=RUNNING, 1=RESULT_OK, 2=RESULT_FAIL
    uint32_t resultUntilMs;    // millis() deadline for auto-dismiss
    int8_t   trackedState;     // last cam.state we observed, for edge detect
};
static volatile CalOverlay calOverlay = { -1, 0, 0, -1 };

// "Cancelar" button on the overlay — only hit-tested while overlay is up.
// Lives centred near the bottom of the overlay rect.
constexpr int CANCEL_X = 280, CANCEL_Y = 340, CANCEL_W = 240, CANCEL_H = 60;

// =============================================================
// Buttons. Hit-tested against raw touch coords; the action callback
// fires the HTTP fan-out from the main loop (not from the touch
// callback, so we never block touch reads waiting on the network).
// =============================================================
enum ActionId {
    ACT_NONE = 0,
    ACT_CAL_A,
    ACT_CAL_B,
    ACT_CAL_CANCEL,
    ACT_START_PAUSE,
    ACT_RESET_ALL,
    ACT_A_PLUS,
    ACT_A_MINUS,
    ACT_B_PLUS,
    ACT_B_MINUS,
};
static volatile ActionId pendingAction = ACT_NONE;

struct Button {
    int x, y, w, h;
    const char* label;
    uint16_t fill;
    uint16_t fillPressed;
    ActionId action;
};
// Two rows of buttons fit comfortably under a slightly compressed score
// area. Top row = referee score adjustments (per side). Bottom row =
// match-control actions.
static Button buttons[] = {
    // Score-adjust row — green +, dark red −. Paired by side so the
    // operator's eye doesn't have to cross the middle to find them.
    {  20, 230, 170, 70, "A +",   0x0640, 0x07E0, ACT_A_PLUS  },
    { 210, 230, 170, 70, "A -",   0x6000, 0xA800, ACT_A_MINUS },
    { 420, 230, 170, 70, "B -",   0x6000, 0xA800, ACT_B_MINUS },
    { 610, 230, 170, 70, "B +",   0x0640, 0x07E0, ACT_B_PLUS  },
    // Match-control row
    {  20, 320, 170, 70, "CAL A", 0xFD20, 0xFEC0, ACT_CAL_A },          // orange
    { 210, 320, 170, 70, "START", 0x0640, 0x07E0, ACT_START_PAUSE },    // label flips runtime
    { 420, 320, 170, 70, "RESET", 0x6000, 0xA800, ACT_RESET_ALL },      // dark red
    { 610, 320, 170, 70, "CAL B", 0xFD20, 0xFEC0, ACT_CAL_B },
};
constexpr int NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);
static int pressedButton = -1;     // index of currently pressed button (-1 none)

// Colour palette (RGB565)
constexpr uint16_t COL_BG    = 0x0000;
constexpr uint16_t COL_SCORE = 0x07E0;
constexpr uint16_t COL_LABEL = 0x528A;
constexpr uint16_t COL_DIM   = 0x39C7;
constexpr uint16_t COL_RED   = 0xF800;
constexpr uint16_t COL_GREEN = 0x07E0;
constexpr uint16_t COL_ORANGE= 0xFD20;
constexpr uint16_t COL_WHITE = 0xFFFF;
constexpr uint16_t COL_BLACK = 0x0000;

// Forward declarations (needed because runAction's optimistic redraw
// path calls back into the renderer).
static void drawScoreDigits();
static void drawHeader();
static void drawSideStatus();
static void drawButton(int idx);

static bool extractIntField(const String& body, const char* key, int* out) {
    String pat = String("\"") + key + "\":";
    int i = body.indexOf(pat);
    if (i < 0) return false;
    int start = i + pat.length();
    int j = start;
    while (j < (int)body.length() && (body[j] == ' ' || body[j] == '-' ||
                                       (body[j] >= '0' && body[j] <= '9'))) j++;
    if (j == start) return false;
    *out = body.substring(start, j).toInt();
    return true;
}

// Lift a JSON string field into a fixed char buffer. Truncates silently.
// Does NOT handle backslash-escaped quotes — the camera's calMsg is a
// curated set of short Portuguese phrases without embedded quotes.
static bool extractStringField(const String& body, const char* key,
                               char* out, size_t outSz) {
    if (outSz == 0) return false;
    String pat = String("\"") + key + "\":\"";
    int i = body.indexOf(pat);
    if (i < 0) return false;
    int start = i + pat.length();
    int end = body.indexOf('"', start);
    if (end < 0) return false;
    size_t len = (size_t)(end - start);
    if (len >= outSz) len = outSz - 1;
    for (size_t k = 0; k < len; k++) out[k] = body.charAt(start + k);
    out[len] = '\0';
    return true;
}

static bool extractBoolField(const String& body, const char* key, bool* out) {
    String pat = String("\"") + key + "\":";
    int i = body.indexOf(pat);
    if (i < 0) return false;
    int start = i + pat.length();
    if (body.startsWith("true", start))  { *out = true;  return true; }
    if (body.startsWith("false", start)) { *out = false; return true; }
    return false;
}

// =============================================================
// REST handlers — mirror src_scoreboard/scoreboard.cpp so cameras and
// the integration test suite can't tell the HMI from the LED placar.
//
// All handlers run on the esp_http_server background task (not the main
// loop), so they touch state via the same placarA/placarB volatile vars
// the touch dispatch path already uses. State writes here race-free
// against single-byte volatile reads on core 1 — same pattern as the
// camera firmware.
// =============================================================
static esp_err_t send_json(httpd_req_t* req, int code, const char* body) {
    if      (code == 400) httpd_resp_set_status(req, "400 Bad Request");
    else if (code == 500) httpd_resp_set_status(req, "500 Internal Server Error");
    // Default 200 OK doesn't need explicit set.
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static int build_status_json(char* buf, size_t n) {
    // WiFi.localIP() returns an IPAddress; toString() builds a String each
    // call. We snprintf the dotted-quad directly to avoid the heap churn.
    IPAddress ip = WiFi.localIP();
    return snprintf(buf, n,
        "{\"role\":\"scoreboard\",\"a\":%d,\"b\":%d,\"ip\":\"%u.%u.%u.%u\"}",
        (int)placarA, (int)placarB,
        ip[0], ip[1], ip[2], ip[3]);
}

// Pull a single query-string key. Returns true if found; the value is
// lowercase-normalized in place for case-insensitive comparison.
static bool query_get_lower(httpd_req_t* req, const char* key,
                            char* out, size_t outsz) {
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) return false;
    if (httpd_query_key_value(buf, key, out, outsz) != ESP_OK) return false;
    for (char* p = out; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 32;
    return true;
}

static esp_err_t handle_status(httpd_req_t* req) {
    char body[96];
    build_status_json(body, sizeof(body));
    return send_json(req, 200, body);
}

static esp_err_t handle_goal(httpd_req_t* req) {
    char side[4];
    if (!query_get_lower(req, "side", side, sizeof(side))) {
        return send_json(req, 400, "{\"ok\":false,\"err\":\"side must be a or b\"}");
    }
    if      (side[0] == 'a' && !side[1]) { if (placarA < 99) { placarA++; dirtyDigits = true; } }
    else if (side[0] == 'b' && !side[1]) { if (placarB < 99) { placarB++; dirtyDigits = true; } }
    else {
        return send_json(req, 400, "{\"ok\":false,\"err\":\"side must be a or b\"}");
    }
    char body[80];
    snprintf(body, sizeof(body), "{\"ok\":true,\"side\":\"%c\",\"a\":%d,\"b\":%d}",
             side[0], (int)placarA, (int)placarB);
    return send_json(req, 200, body);
}

static esp_err_t handle_goal_undo(httpd_req_t* req) {
    char side[4];
    if (!query_get_lower(req, "side", side, sizeof(side))) {
        return send_json(req, 400, "{\"ok\":false,\"err\":\"side must be a or b\"}");
    }
    if      (side[0] == 'a' && !side[1]) { if (placarA > 0) { placarA--; dirtyDigits = true; } }
    else if (side[0] == 'b' && !side[1]) { if (placarB > 0) { placarB--; dirtyDigits = true; } }
    else {
        return send_json(req, 400, "{\"ok\":false,\"err\":\"side must be a or b\"}");
    }
    char body[80];
    snprintf(body, sizeof(body), "{\"ok\":true,\"side\":\"%c\",\"a\":%d,\"b\":%d}",
             side[0], (int)placarA, (int)placarB);
    return send_json(req, 200, body);
}

static esp_err_t handle_api_reset(httpd_req_t* req) {
    placarA = 0; placarB = 0; dirtyDigits = true;
    return send_json(req, 200, "{\"ok\":true,\"a\":0,\"b\":0}");
}

// Legacy / browser-dashboard endpoints — same handlers the LED placar
// exposed, so any existing tooling that pokes /a+, /b+, /az, /bz, /reset
// still works against this device.
static esp_err_t handle_a_plus(httpd_req_t* req) {
    if (placarA < 99) { placarA++; dirtyDigits = true; }
    char body[96]; build_status_json(body, sizeof(body));
    return send_json(req, 200, body);
}
static esp_err_t handle_b_plus(httpd_req_t* req) {
    if (placarB < 99) { placarB++; dirtyDigits = true; }
    char body[96]; build_status_json(body, sizeof(body));
    return send_json(req, 200, body);
}
static esp_err_t handle_a_zero(httpd_req_t* req) {
    placarA = 0; dirtyDigits = true;
    char body[96]; build_status_json(body, sizeof(body));
    return send_json(req, 200, body);
}
static esp_err_t handle_b_zero(httpd_req_t* req) {
    placarB = 0; dirtyDigits = true;
    char body[96]; build_status_json(body, sizeof(body));
    return send_json(req, 200, body);
}
static esp_err_t handle_reset(httpd_req_t* req) {
    return handle_api_reset(req);
}

// Diagnostic: snapshot of what the GT911 has reported lately. Curl this
// from a laptop to check whether the chip is scanning at all without
// needing a serial cable: `curl -s http://192.168.40.89/debug/touch`.
static esp_err_t handle_debug_touch(httpd_req_t* req) {
    char body[384];
    snprintf(body, sizeof(body),
        "{\"prod_id\":\"%s\",\"fw\":\"0x%04X\",\"boot_cfg_ver\":\"0x%02X\","
        "\"post_cfg_ver\":\"0x%02X\",\"wrote_blob\":%s,\"wrote_ok\":%s,"
        "\"poll_count\":%u,\"touch_count\":%u,\"last_status\":\"0x%02X\","
        "\"last_x\":%d,\"last_y\":%d,\"touched_now\":%s}",
        gt911::prodId, gt911::fwVer, gt911::bootCfgVer, gt911::postCfgVer,
        gt911::wroteBlob ? "true" : "false",
        gt911::wroteOk   ? "true" : "false",
        (unsigned)gt911::pollCount, (unsigned)gt911::touchCount,
        gt911::lastStatus, gt911::lastX, gt911::lastY,
        gt911::touched ? "true" : "false");
    return send_json(req, 200, body);
}

// Force-rewrite of the fallback config blob even if a factory config
// is currently loaded. Use to recover a chip whose config got corrupted
// by an earlier experiment. `curl http://192.168.40.89/debug/gt911-rewrite`.
static esp_err_t handle_debug_rewrite(httpd_req_t* req) {
    bool ok = gt911::writeFactoryConfig();
    char body[128];
    snprintf(body, sizeof(body),
        "{\"ok\":%s,\"post_cfg_ver\":\"0x%02X\"}",
        ok ? "true" : "false", gt911::postCfgVer);
    return send_json(req, ok ? 200 : 500, body);
}

// RST-pin sweep was attempted (and DELETED) — almost every general-purpose
// GPIO on the CrowPanel DIS07050H is wired to RGB-display data lines or
// sync signals (0,1,3,4,5,6,7,8,9,14,15,16,21,39,40,41,45,46,47,48,
// plus backlight=2, UART0=43,44). Toggling any of them as a GPIO output
// re-routes the pin away from the LCD peripheral and kills the panel
// signal (manifests as a stuck white screen). The only truly safe pins
// for general toggling are 17, 18, 38, 42. We probed all of those in
// the boot sweep on 2026-06-03 and none silenced the GT911 — so even
// a safe sweep can't find the RST line. This endpoint was deleted to
// prevent accidental panel damage from a stray curl.

static void startWebServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = 80;
    config.ctrl_port        = 32768;
    config.max_uri_handlers = 16;
    config.max_open_sockets = 10;   // up from WebServer's default 5
    config.lru_purge_enable = true; // kick oldest connection when full
    config.stack_size       = 8192;

    if (httpd_start(&server, &config) != ESP_OK) {
        Serial.println("[http] FAILED to start esp_http_server");
        return;
    }

    struct UriDef { const char* uri; esp_err_t (*h)(httpd_req_t*); };
    UriDef routes[] = {
        { "/status",             handle_status        },
        { "/goal",               handle_goal          },
        { "/goal-undo",          handle_goal_undo     },
        { "/api/reset",          handle_api_reset     },
        { "/reset",              handle_reset         },
        { "/a+",                 handle_a_plus        },
        { "/b+",                 handle_b_plus        },
        { "/az",                 handle_a_zero        },
        { "/bz",                 handle_b_zero        },
        { "/debug/touch",        handle_debug_touch   },
        { "/debug/gt911-rewrite",handle_debug_rewrite },
    };
    for (auto& r : routes) {
        httpd_uri_t u = { .uri = r.uri, .method = HTTP_GET,
                          .handler = r.h, .user_ctx = NULL };
        httpd_register_uri_handler(server, &u);
    }
    Serial.println("[http] placar API started on port 80 (esp_http_server)");
}

// =============================================================
// HTTP serialisation — one worker task on core 0 owns the HTTP stack and
// is the only thing that calls HTTPClient. UI actions and polling both
// push commands into the same queue so concurrent HTTPClient use (which
// corrupts state on the ESP32 Arduino stack) can never happen.
// =============================================================
struct HttpCmd { char url[160]; };
static QueueHandle_t httpQueue = nullptr;

static void httpKick(const String& url) {
    if (!httpQueue) { Serial.println("[http] queue not ready, dropping"); return; }
    HttpCmd c{};
    snprintf(c.url, sizeof(c.url), "%s", url.c_str());
    BaseType_t ok = xQueueSend(httpQueue, &c, 0);
    Serial.printf("[http] queued %s (%s)\n", c.url, ok == pdTRUE ? "ok" : "FULL");
}

static void httpExec(const char* url) {
    HTTPClient http;
    if (http.begin(url)) {
        http.setTimeout(1500);
        int code = http.GET();
        Serial.printf("[http] %s → %d\n", url, code);
        http.end();
    } else {
        Serial.printf("[http] %s → begin failed\n", url);
    }
}

static void runAction(ActionId a) {
    Serial.printf("[action] dispatch %d\n", (int)a);
    switch (a) {
        case ACT_CAL_A:
            httpKick(String("http://") + camA.ip + "/calibrate");
            // Raise the overlay immediately for instant feedback. The
            // poll loop will pick up cam.state=1 within 1 s, but the
            // operator shouldn't have to wait that long to know their
            // tap registered.
            calOverlay.side          = 0;
            calOverlay.phase         = 0;
            calOverlay.trackedState  = camA.state;
            calOverlay.resultUntilMs = 0;
            dirtyOverlay             = true;
            break;
        case ACT_CAL_B:
            httpKick(String("http://") + camB.ip + "/calibrate");
            calOverlay.side          = 1;
            calOverlay.phase         = 0;
            calOverlay.trackedState  = camB.state;
            calOverlay.resultUntilMs = 0;
            dirtyOverlay             = true;
            break;
        case ACT_CAL_CANCEL: {
            // Send /reset to the cam so it leaves CALIBRATING immediately,
            // then collapse the overlay. The next poll will see state=0
            // and naturally trigger the dismiss path, but we don't need to
            // wait for it.
            if (calOverlay.side == 0)      httpKick(String("http://") + camA.ip + "/reset");
            else if (calOverlay.side == 1) httpKick(String("http://") + camB.ip + "/reset");
            calOverlay.phase         = 0;
            calOverlay.side          = -1;
            calOverlay.resultUntilMs = 0;
            // Repaint everything the overlay covered.
            dirtyDigits  = true;
            dirtyButtons = true;
            dirtyStatus  = true;
            break;
        }
        case ACT_START_PAUSE: {
            bool anyPlaying = (camA.state == 2) || (camB.state == 2);
            bool anyPaused  = (camA.state == 3) || (camB.state == 3);
            const char* cmd = anyPlaying ? "pause" : (anyPaused ? "resume" : "start");
            httpKick(String("http://") + camA.ip + "/" + cmd);
            httpKick(String("http://") + camB.ip + "/" + cmd);
            break;
        }
        case ACT_RESET_ALL:
            placarA = 0; placarB = 0;
            dirtyDigits = true;
            httpKick(String("http://") + camA.ip + "/reset");
            httpKick(String("http://") + camB.ip + "/reset");
            break;
        // Referee adjustments — local-only, max 99, min 0 (same clamp
        // the placar's /goal and /goal-undo handlers use). Cameras'
        // goalCounts are intentionally NOT touched here; a manual
        // score adjustment doesn't reflect a detection.
        case ACT_A_PLUS:  if (placarA < 99) { placarA++; dirtyDigits = true; } break;
        case ACT_A_MINUS: if (placarA > 0)  { placarA--; dirtyDigits = true; } break;
        case ACT_B_PLUS:  if (placarB < 99) { placarB++; dirtyDigits = true; } break;
        case ACT_B_MINUS: if (placarB > 0)  { placarB--; dirtyDigits = true; } break;
        case ACT_NONE: break;
    }
}

// =============================================================
// Rendering
// =============================================================
static void drawHeader() {
    gfx->fillRect(0, 0, 800, 60, 0x18E3);
    gfx->setTextColor(COL_WHITE, 0x18E3);
    gfx->setTextSize(3);
    gfx->setCursor(20, 18);
    gfx->print("gol-cam");

    // Right-side status pill — green PLACAR when WiFi is up so cameras
    // can reach our /goal endpoints, dark red OFFLINE otherwise.
    bool wifi = (WiFi.status() == WL_CONNECTED);
    const char* tag = wifi ? "PLACAR" : "OFFLINE";
    uint16_t pillCol = wifi ? 0x0640 : 0x8800;
    int pillW = (int)strlen(tag) * 18 + 24;
    gfx->fillRoundRect(800 - pillW - 20, 14, pillW, 32, 6, pillCol);
    gfx->setTextSize(2);
    gfx->setCursor(800 - pillW - 8, 22);
    gfx->print(tag);
}

static void drawScoreDigits() {
    char a[8], b[8];
    snprintf(a, sizeof(a), "%d", placarA < 0 ? 0 : placarA);
    snprintf(b, sizeof(b), "%d", placarB < 0 ? 0 : placarB);

    gfx->fillRect(0, 65, 800, 160, COL_BG);

    // Compressed to text size 12 (60 wide × 84 tall per glyph) so the
    // score still reads from across the room but leaves vertical room
    // for the two button rows underneath.
    gfx->setTextSize(12);
    gfx->setTextColor(COL_SCORE, COL_BG);
    const int yDigit = 85;
    const int aw = (int)strlen(a) * 60;
    const int bw = (int)strlen(b) * 60;
    gfx->setCursor(200 - aw/2, yDigit);
    gfx->print(a);
    gfx->setCursor(600 - bw/2, yDigit);
    gfx->print(b);

    gfx->setTextSize(4);
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(388, 115);
    gfx->print("x");
}

static void drawButton(int idx) {
    const Button& b = buttons[idx];
    bool pressed = (idx == pressedButton);
    uint16_t fill = pressed ? b.fillPressed : b.fill;
    gfx->fillRoundRect(b.x, b.y, b.w, b.h, 10, fill);
    gfx->drawRoundRect(b.x, b.y, b.w, b.h, 10, COL_WHITE);

    // Label — for START button, swap to PAUSE / RESUME based on cam state.
    const char* label = b.label;
    if (b.action == ACT_START_PAUSE) {
        bool anyPlaying = (camA.state == 2) || (camB.state == 2);
        bool anyPaused  = (camA.state == 3) || (camB.state == 3);
        label = anyPlaying ? "PAUSE" : (anyPaused ? "RESUME" : "START");
    }

    gfx->setTextSize(3);
    gfx->setTextColor(COL_WHITE, fill);
    int textW = (int)strlen(label) * 18;
    gfx->setCursor(b.x + b.w/2 - textW/2, b.y + b.h/2 - 12);
    gfx->print(label);
}

static void drawButtons() {
    for (int i = 0; i < NUM_BUTTONS; i++) drawButton(i);
}

// Per-side status: small coloured pill + HOME/AWAY label. Green = ready
// (calibrated + idle/play), orange = calibrating, red = offline, grey =
// idle uncalibrated.
static void drawSideStatus() {
    gfx->fillRect(0, 405, 800, 75, COL_BG);

    auto draw = [&](int cx, const char* label, const CamState& c) {
        gfx->setTextSize(3);
        gfx->setTextColor(COL_LABEL, COL_BG);
        int lw = (int)strlen(label) * 18;
        gfx->setCursor(cx - lw/2, 410);
        gfx->print(label);

        uint16_t pillCol;
        const char* tag;
        if (!c.online)             { pillCol = 0x8800; tag = "OFFLINE"; }
        else if (c.state == 1)     { pillCol = 0xFD20; tag = "CAL...";  }
        else if (c.state == 2)     { pillCol = 0x0640; tag = "PLAY";    }
        else if (c.state == 3)     { pillCol = 0xFFE0; tag = "PAUSE";   }
        else if (c.calibrated)     { pillCol = 0x0640; tag = "READY";   }
        else                       { pillCol = 0x39C7; tag = "IDLE";    }
        int pw = (int)strlen(tag) * 14 + 20;
        gfx->fillRoundRect(cx - pw/2, 445, pw, 28, 6, pillCol);
        gfx->setTextSize(2);
        gfx->setTextColor(COL_WHITE, pillCol);
        gfx->setCursor(cx - pw/2 + 10, 451);
        gfx->print(tag);
    };
    draw(200, "HOME", camA);
    draw(600, "AWAY", camB);
}

static void renderFull() {
    gfx->fillScreen(COL_BG);
    firstFullDraw = false;
    drawHeader();
    drawScoreDigits();
    drawButtons();
    drawSideStatus();
}

// =============================================================
// Calibration overlay rendering. Covers y=65 → y=420 of the panel
// (everything from below the header through the buttons; the
// HOME/AWAY status pills at y=405+ stay visible underneath so the
// operator can still see whether the cam is online during cal).
// =============================================================
constexpr int OVL_X = 20, OVL_Y = 75, OVL_W = 760, OVL_H = 325;

static void drawCalOverlay() {
    // Snapshot the volatile fields one at a time — copying a volatile
    // struct directly isn't allowed by the language.
    CalOverlay ov;
    ov.side          = calOverlay.side;
    ov.phase         = calOverlay.phase;
    ov.resultUntilMs = calOverlay.resultUntilMs;
    ov.trackedState  = calOverlay.trackedState;
    if (ov.side < 0) return;
    const CamState& c = (ov.side == 0) ? camA : camB;
    const char* sideLabel = (ov.side == 0) ? "Lado A (HOME)" : "Lado B (AWAY)";

    // Background panel.
    gfx->fillRoundRect(OVL_X, OVL_Y, OVL_W, OVL_H, 14, 0x18E3);
    gfx->drawRoundRect(OVL_X, OVL_Y, OVL_W, OVL_H, 14, COL_LABEL);

    // Title row.
    char title[64];
    if (ov.phase == 1) {
        snprintf(title, sizeof(title), "Calibrado %s", sideLabel);
    } else if (ov.phase == 2) {
        snprintf(title, sizeof(title), "Falhou %s", sideLabel);
    } else {
        snprintf(title, sizeof(title), "Calibrando %s", sideLabel);
    }
    uint16_t titleCol = (ov.phase == 1) ? COL_GREEN
                       : (ov.phase == 2) ? COL_RED
                       : 0xFD20;  // orange (CAL...)
    gfx->setTextSize(3);
    gfx->setTextColor(titleCol, 0x18E3);
    int tw = (int)strlen(title) * 18;
    gfx->setCursor(OVL_X + (OVL_W - tw) / 2, OVL_Y + 22);
    gfx->print(title);

    if (ov.phase == 0) {
        // RUNNING phase: instructions + live calMsg + progress bar.
        const char* sub = "Coloque o dadinho dentro do gol";
        int sw = (int)strlen(sub) * 12;
        gfx->setTextSize(2);
        gfx->setTextColor(COL_WHITE, 0x18E3);
        gfx->setCursor(OVL_X + (OVL_W - sw) / 2, OVL_Y + 80);
        gfx->print(sub);

        // calMsg from the camera. Truncate at 36 chars to fit on one
        // line at text size 2 in our 760-px-wide overlay.
        char msg[40];
        snprintf(msg, sizeof(msg), "%s", c.calMsg[0] ? c.calMsg : "(aguardando…)");
        int mw = (int)strlen(msg) * 12;
        if (mw > OVL_W - 40) mw = OVL_W - 40;
        gfx->setTextColor(0xFD20, 0x18E3);  // orange
        gfx->setCursor(OVL_X + (OVL_W - mw) / 2, OVL_Y + 130);
        gfx->print(msg);

        // Progress bar: coarse 3-stop track driven by hasCalSnap and
        // the cam's reported calContrast. Just a visual cue — the cam
        // doesn't expose a 0..100 percentage.
        int barX = OVL_X + 60, barY = OVL_Y + 180;
        int barW = OVL_W - 120, barH = 22;
        gfx->drawRoundRect(barX, barY, barW, barH, 6, COL_LABEL);
        int fill = 0;
        if (c.state == 1) fill = 25;             // entered CAL state
        if (c.hasCalSnap) fill = 60;             // snapshot captured
        if (c.calContrast > 0) fill = 90;        // contrast measured
        if (fill > 0) {
            int fw = (barW - 4) * fill / 100;
            gfx->fillRoundRect(barX + 2, barY + 2, fw, barH - 4, 4, 0xFD20);
        }
    } else {
        // RESULT phase: show the learned thresholds (OK) or a hint (FAIL).
        char line1[64], line2[64];
        if (ov.phase == 1) {
            snprintf(line1, sizeof(line1), "Limiares aprendidos:");
            snprintf(line2, sizeof(line2), "mov=%d   cor=%d   cont=%d",
                     c.motionTh, c.colorTh, c.calContrast);
        } else {
            snprintf(line1, sizeof(line1), "Sem dadinho detectado");
            snprintf(line2, sizeof(line2), "Posicione e toque CAL novamente");
        }
        gfx->setTextSize(2);
        gfx->setTextColor(COL_WHITE, 0x18E3);
        int l1w = (int)strlen(line1) * 12;
        gfx->setCursor(OVL_X + (OVL_W - l1w) / 2, OVL_Y + 110);
        gfx->print(line1);
        int l2w = (int)strlen(line2) * 12;
        gfx->setCursor(OVL_X + (OVL_W - l2w) / 2, OVL_Y + 160);
        gfx->print(line2);
    }

    // Cancelar button — only shown in RUNNING phase.
    if (ov.phase == 0) {
        gfx->fillRoundRect(CANCEL_X, CANCEL_Y, CANCEL_W, CANCEL_H, 10, 0x6000);
        gfx->drawRoundRect(CANCEL_X, CANCEL_Y, CANCEL_W, CANCEL_H, 10, COL_WHITE);
        gfx->setTextSize(3);
        gfx->setTextColor(COL_WHITE, 0x6000);
        const char* cancelLabel = "Cancelar";
        int cw = (int)strlen(cancelLabel) * 18;
        gfx->setCursor(CANCEL_X + (CANCEL_W - cw) / 2,
                       CANCEL_Y + (CANCEL_H - 24) / 2);
        gfx->print(cancelLabel);
    }
}

// State machine — called once per loop iteration. Watches cam.state
// transitions to flip the overlay's phase. Dismisses itself when the
// result window expires.
static void updateCalOverlay() {
    if (calOverlay.side < 0) return;
    CamState& c = (calOverlay.side == 0) ? camA : camB;

    if (calOverlay.phase == 0) {
        // RUNNING — watch for cam leaving the CALIBRATING state. The
        // tracked-state field lets us detect the exact 1→0 edge even if
        // the poll loop missed a frame.
        int8_t prev = calOverlay.trackedState;
        calOverlay.trackedState = (int8_t)c.state;
        if (prev == 1 && c.state != 1) {
            calOverlay.phase         = c.calibrated ? 1 : 2;
            calOverlay.resultUntilMs = millis() + 3000;
            dirtyOverlay             = true;
        }
    } else {
        // RESULT_OK or RESULT_FAIL — auto-dismiss after 3 s.
        if ((int32_t)(millis() - calOverlay.resultUntilMs) >= 0) {
            calOverlay.side          = -1;
            calOverlay.phase         = 0;
            calOverlay.resultUntilMs = 0;
            // Repaint everything the overlay was covering.
            dirtyDigits  = true;
            dirtyButtons = true;
            dirtyStatus  = true;
        }
    }
}

static bool overlayActive() { return calOverlay.side >= 0; }

static void renderSplash(const char* msg, uint16_t col) {
    gfx->fillScreen(COL_BG);
    firstFullDraw = false;
    drawHeader();
    gfx->setTextSize(4);
    gfx->setTextColor(col, COL_BG);
    int w = (int)strlen(msg) * 24;
    gfx->setCursor(400 - w/2, 220);
    gfx->print(msg);
}

// =============================================================
// Touch handling — runs every loop iteration; resolves to a button index
// on press, fires the action on release (so the user can drag off a
// button to cancel without firing).
// =============================================================
static int hitTest(int x, int y) {
    // When the calibration overlay is up, the placar buttons are visually
    // obscured. Don't accept taps that fall through to them — only the
    // overlay's Cancelar button should respond. We signal that by
    // returning -2 (vs -1 = "missed all buttons") so the touch loop can
    // route a Cancelar hit to ACT_CAL_CANCEL without firing the wrong
    // placar action.
    if (overlayActive()) {
        if (calOverlay.phase == 0 &&
            x >= CANCEL_X && x < CANCEL_X + CANCEL_W &&
            y >= CANCEL_Y && y < CANCEL_Y + CANCEL_H) {
            return -2;  // Cancelar rect, see serviceTouch()
        }
        return -1;  // tap missed Cancelar (or in RESULT phase) — swallow
    }
    for (int i = 0; i < NUM_BUTTONS; i++) {
        const Button& b = buttons[i];
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    }
    return -1;
}

static void serviceTouch() {
    // Fire-on-press model. The GT911 on this CrowPanel reports touches
    // as one-shot status-bit blips rather than a sustained "data ready"
    // window, so a release-based fire-on-up scheme silently drops every
    // tap (held < MIN_PRESS_MS). Instead: any poll that lands on a
    // button triggers the action immediately, then a POST_FIRE_MS
    // cooldown prevents the same tap from re-firing from a subsequent
    // status blip mid-press. Cooldown also caps rapid taps at ~4 Hz,
    // which is comfortably faster than a referee actually wants.
    constexpr uint32_t POST_FIRE_MS = 250;

    static uint32_t lastFireMs   = 0;
    static int      lastFiredBtn = -1;

    uint32_t now = millis();
    gt911::poll();
    bool isPressed = gt911::touched;
    int btn = isPressed ? hitTest(gt911::lastX, gt911::lastY) : -1;

    // Visual feedback: redraw whichever button just changed pressed-state.
    if (btn != pressedButton) {
        int prev = pressedButton;
        pressedButton = btn;
        if (prev >= 0) drawButton(prev);
        if (btn  >= 0) drawButton(btn);
    }

    bool inCooldown = (now - lastFireMs < POST_FIRE_MS);

    // Fire once when a touch lands on a button. Won't re-fire if the
    // chip blips the touch off-then-on during the cooldown.
    if (!inCooldown && btn >= 0 && btn != lastFiredBtn) {
        pendingAction = buttons[btn].action;
        lastFireMs    = now;
        lastFiredBtn  = btn;
        Serial.printf("[touch] FIRE btn=%d \"%s\" at (%d,%d)\n",
                      btn, buttons[btn].label,
                      gt911::lastX, gt911::lastY);
    }

    // Cancelar on the overlay — hitTest returned -2 to flag this.
    if (!inCooldown && btn == -2 && lastFiredBtn != -2) {
        pendingAction = ACT_CAL_CANCEL;
        lastFireMs    = now;
        lastFiredBtn  = -2;
        Serial.println("[touch] FIRE Cancelar (overlay)");
    }

    // After cooldown AND finger truly off, clear the "last fired" lock
    // so a fresh tap on the same button (e.g. A+ A+ A+) fires again.
    if (!isPressed && now - lastFireMs > POST_FIRE_MS) {
        lastFiredBtn = -1;
    }
}

// =============================================================
// Polling
// =============================================================
static void connectWiFi() {
#ifdef WIFI_STATIC_IP
    IPAddress staticIP, gateway, subnet;
    staticIP.fromString(WIFI_STATIC_IP);
    gateway.fromString(WIFI_GATEWAY);
    subnet.fromString(WIFI_SUBNET);
    if (!WiFi.config(staticIP, gateway, subnet)) {
        Serial.println("[wifi] static config rejected");
    }
#endif
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[wifi] connecting to %s", WIFI_SSID);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected, IP=%s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[wifi] FAILED — will keep retrying in loop()");
    }
}

static void pollCam(CamState& c) {
    HTTPClient http;
    String url = "http://" + String(c.ip) + "/status";
    if (!http.begin(url)) {
        if (c.online) { c.online = false; dirtyStatus = true; }
        return;
    }
    http.setTimeout(1200);
    int code = http.GET();
    if (code != 200) {
        if (c.online) { c.online = false; dirtyStatus = true; }
        http.end();
        return;
    }
    String body = http.getString();
    http.end();
    int st = c.state, goals = c.goals;
    bool cal = c.calibrated;
    int motTh = c.motionTh, colTh = c.colorTh, calCtr = c.calContrast;
    bool hasSnap = c.hasCalSnap;
    char prevMsg[sizeof(c.calMsg)];
    memcpy(prevMsg, c.calMsg, sizeof(prevMsg));

    extractIntField(body, "state", &st);
    extractIntField(body, "goals", &goals);
    extractBoolField(body, "calibrated", &cal);
    extractIntField(body, "motionTh", &motTh);
    extractIntField(body, "colorTh", &colTh);
    extractIntField(body, "calContrast", &calCtr);
    extractBoolField(body, "hasCalSnap", &hasSnap);
    extractStringField(body, "calMsg", c.calMsg, sizeof(c.calMsg));

    bool changed = !c.online || st != c.state || cal != c.calibrated;
    bool msgChanged = (memcmp(prevMsg, c.calMsg, sizeof(prevMsg)) != 0);
    c.online = true;
    c.state = st; c.calibrated = cal; c.goals = goals;
    c.motionTh = motTh; c.colorTh = colTh; c.calContrast = calCtr;
    c.hasCalSnap = hasSnap;
    if (changed) {
        dirtyStatus = true;
        dirtyButtons = true;  // START/PAUSE label tracks cam state (index 5)
    }
    // The calibration overlay re-renders every time the cam's text or
    // progress markers move forward, even within a single state. This
    // is the only path that drives the overlay's live feedback.
    if (changed || msgChanged) dirtyOverlay = true;
}

// Background worker on core 0. Drains queued UI actions and polls the
// cameras (placar polling is gone — we ARE the placar). Single-threaded
// HTTP, no race on HTTPClient.
static void workerTask(void*) {
    uint32_t lastCamA = 0, lastCamB = 0;
    HttpCmd cmd;
    while (true) {
        while (xQueueReceive(httpQueue, &cmd, 0) == pdTRUE) {
            httpExec(cmd.url);
        }
        if (WiFi.status() == WL_CONNECTED) {
            uint32_t now = millis();
            if (now - lastCamA >= CAM_POLL_MS) { pollCam(camA); lastCamA = now; }
            if (now - lastCamB >= CAM_POLL_MS) { pollCam(camB); lastCamB = now; }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void setup() {
    Serial.begin(115200);
    delay(800);
    camA.ip = CAM_A_IP;
    camB.ip = CAM_B_IP;
    Serial.println("\n=== gol-cam HMI (CrowPanel) — Phase 3a starting ===");
    Serial.printf("[boot] PSRAM total = %u, free = %u  heap = %u\n",
                  (unsigned)ESP.getPsramSize(),
                  (unsigned)ESP.getFreePsram(),
                  (unsigned)ESP.getFreeHeap());

    if (!gfx->begin()) Serial.println("[gfx] ERROR: panel init failed");
    else               Serial.println("[gfx] panel initialised");
    gfx->setRotation(0);
    pinMode(pins::LCD_BACKLIGHT, OUTPUT);
    digitalWrite(pins::LCD_BACKLIGHT, HIGH);
    renderSplash("Conectando WiFi...", COL_DIM);

    // I2C bus comes up at the standard 100 kHz; GT911 supports up to 400
    // kHz but we leave headroom for the audio chip at 0x18 on the same bus.
    Wire.begin(pins::TOUCH_SDA, pins::TOUCH_SCL);

    // RST-pin hunt was tried during 2026-06-03 debug session — none of
    // GPIO 0,3,4,5,6,7,15,16,17,18,21,38,39,42,45,46,47,48 silenced
    // the chip when held low or high, and toggling GPIO 43/44 broke
    // the UART (those are UART0 TX/RX). Conclusion: on this CrowPanel
    // revision the GT911 RST line is not MCU-controlled. The wide
    // sweep is no longer run on boot — it's still available on demand
    // via /debug/rst-sweep so the user can re-run it if they suspect
    // intermittent wiring.
    // Scan once at boot so we know which devices are responding.
    Serial.println("[i2c] scanning bus...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[i2c]   device @ 0x%02X\n", addr);
        }
    }
    // Give the GT911 a moment to be ready after power-up before we
    // start hitting its I2C registers.
    delay(50);
    gt911::begin();

    connectWiFi();
    startWebServer();
    renderSplash("Pronto", COL_GREEN);
    Serial.printf("[hmi] HMI is the placar @ %s  camA=%s  camB=%s\n",
                  WiFi.localIP().toString().c_str(), CAM_A_IP, CAM_B_IP);

    // First full draw so all four button rects exist before the first
    // touch event can land on them.
    renderFull();

    // Queue for serialised HTTP commands (depth 8 = enough for a
    // 3-URL RESET burst even while a poll is in flight).
    httpQueue = xQueueCreate(8, sizeof(HttpCmd));
    xTaskCreatePinnedToCore(workerTask, "http", 6144, NULL, 1, NULL, 0);
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(500);
        return;
    }

    // Periodic heartbeat so we can tell from serial whether the touch
    // poll is even running and how many real touches it's seeing.
    static uint32_t lastHb = 0;
    uint32_t hbNow = millis();
    if (hbNow - lastHb >= 5000) {
        Serial.printf("[hb] polls=%u touches=%u  placar=%dx%d\n",
                      (unsigned)gt911::pollCount, (unsigned)gt911::touchCount,
                      (int)placarA, (int)placarB);
        lastHb = hbNow;
    }

    // Touch first so button feedback feels instant.
    serviceTouch();

    // esp_http_server runs on its own background task (configured with
    // 10 max sockets + LRU purge), so there's no handleClient() to call
    // from the main loop. The REST handlers update placarA/placarB +
    // dirtyDigits atomically (single-byte volatile writes), so the main
    // loop re-renders consistently on the next pass through the dirty
    // flags below.

    if (pendingAction != ACT_NONE) {
        ActionId a = pendingAction;
        pendingAction = ACT_NONE;
        runAction(a);
    }

    // Drive the calibration-overlay state machine BEFORE redrawing so
    // any auto-dismiss this iteration sets the underlying dirty flags
    // in time to repaint the placar view on the same frame.
    updateCalOverlay();

    // Re-render whatever the polling task on core 0 marked dirty. Order
    // matters so overlapping rects redraw correctly (header before
    // digits before buttons before status, overlay on top of everything).
    // While the overlay is up, the score/button/status redraws below are
    // wasted pixels — but they're also covered by the overlay rect on
    // the same frame, so the user only ever sees the overlay layer.
    if (dirtyHeader)  { dirtyHeader  = false; drawHeader(); }
    if (dirtyDigits)  { dirtyDigits  = false; drawScoreDigits(); }
    // START is the 6th button (index 5) after the 4 score-adjust buttons.
    if (dirtyButtons) { dirtyButtons = false; drawButton(5); }
    if (dirtyStatus)  { dirtyStatus  = false; drawSideStatus(); }

    // Overlay paints last so it sits on top of the placar layer. It
    // re-renders whenever pollCam() flags new calMsg / state / progress
    // markers, when the state machine flips phase, or right after we
    // raised it from a button tap (runAction sets dirtyOverlay).
    if (overlayActive() && dirtyOverlay) {
        dirtyOverlay = false;
        drawCalOverlay();
    }

    delay(2);  // small yield for FreeRTOS housekeeping
}
