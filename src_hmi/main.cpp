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
#include <WebServer.h>
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
        if (!writeReg(0x8100, 0x01)) {
            Serial.println("[gt911] config commit (0x8100<-1) FAILED");
            return false;
        }
        delay(250);  // chip needs ~200 ms to apply + restart scanning
        uint8_t cfgver = 0;
        readRegs(0x8047, &cfgver, 1);
        Serial.printf("[gt911] post-write config version = 0x%02X (want 0x42)\n", cfgver);
        return cfgver == 0x42;
    }

    static void begin() {
        uint8_t id[5] = {0};
        if (readRegs(REG_PROD_ID, id, 4)) {
            Serial.printf("[gt911] product_id = \"%c%c%c%c\"\n",
                          id[0] ? id[0] : '?', id[1] ? id[1] : '?',
                          id[2] ? id[2] : '?', id[3] ? id[3] : '?');
        } else {
            Serial.println("[gt911] FAILED to read product ID");
        }
        uint8_t fw[2] = {0};
        if (readRegs(REG_FW_VER, fw, 2)) {
            Serial.printf("[gt911] firmware = 0x%02X%02X\n", fw[1], fw[0]);
        }

        // Probe config version. 0xFF = no factory config loaded; the
        // chip will refuse to scan until we write a valid blob. Some
        // CrowPanel units ship with the GT911 NVRAM blank, so we fall
        // back to a hardcoded 800x480 config in that case.
        uint8_t cfgver = 0;
        readRegs(0x8047, &cfgver, 1);
        Serial.printf("[gt911] config version = 0x%02X\n", cfgver);
        if (cfgver == 0xFF || cfgver < 0x42) {
            Serial.println("[gt911] no valid config — writing fallback blob");
            writeFactoryConfig();
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
        if (!readRegs(REG_STATUS, &s, 1)) { touched = false; return; }
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
// zero protocol changes.
static WebServer server(80);

// Dirty flags set by the background polling task (core 0). The main loop
// (core 1) checks them every iteration and re-renders only what changed,
// so HTTP latency never blocks touch reads or button feedback.
static volatile bool dirtyHeader  = false;
static volatile bool dirtyDigits  = false;
static volatile bool dirtyStatus  = false;
static volatile bool dirtyButtons = false;

// =============================================================
// Buttons. Hit-tested against raw touch coords; the action callback
// fires the HTTP fan-out from the main loop (not from the touch
// callback, so we never block touch reads waiting on the network).
// =============================================================
enum ActionId {
    ACT_NONE = 0,
    ACT_CAL_A,
    ACT_CAL_B,
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
// =============================================================
static void sendJson(int code, const String& body) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(code, "application/json", body);
}

static String statusJson() {
    String ip = WiFi.localIP().toString();
    return String("{\"role\":\"scoreboard\",\"a\":") + (int)placarA +
           ",\"b\":" + (int)placarB + ",\"ip\":\"" + ip + "\"}";
}

static void handleStatus() { sendJson(200, statusJson()); }

static void handleGoal() {
    String side = server.arg("side"); side.toLowerCase();
    if      (side == "a") { if (placarA < 99) { placarA++; dirtyDigits = true; } }
    else if (side == "b") { if (placarB < 99) { placarB++; dirtyDigits = true; } }
    else { sendJson(400, "{\"ok\":false,\"err\":\"side must be a or b\"}"); return; }
    String body = String("{\"ok\":true,\"side\":\"") + side + "\",\"a\":" + (int)placarA +
                  ",\"b\":" + (int)placarB + "}";
    sendJson(200, body);
}

static void handleGoalUndo() {
    String side = server.arg("side"); side.toLowerCase();
    if      (side == "a") { if (placarA > 0) { placarA--; dirtyDigits = true; } }
    else if (side == "b") { if (placarB > 0) { placarB--; dirtyDigits = true; } }
    else { sendJson(400, "{\"ok\":false,\"err\":\"side must be a or b\"}"); return; }
    String body = String("{\"ok\":true,\"side\":\"") + side + "\",\"a\":" + (int)placarA +
                  ",\"b\":" + (int)placarB + "}";
    sendJson(200, body);
}

static void handleApiReset() {
    placarA = 0; placarB = 0; dirtyDigits = true;
    sendJson(200, "{\"ok\":true,\"a\":0,\"b\":0}");
}

// Legacy / browser-dashboard endpoints — same handlers the LED placar
// exposed, so any existing tooling that pokes /a+, /b+, /az, /bz, /reset
// still works against this device.
static void handleAplus()  { if (placarA < 99) { placarA++; dirtyDigits = true; } sendJson(200, statusJson()); }
static void handleBplus()  { if (placarB < 99) { placarB++; dirtyDigits = true; } sendJson(200, statusJson()); }
static void handleAzero()  { placarA = 0; dirtyDigits = true; sendJson(200, statusJson()); }
static void handleBzero()  { placarB = 0; dirtyDigits = true; sendJson(200, statusJson()); }
static void handleReset()  { handleApiReset(); }

static void startWebServer() {
    server.on("/status",      HTTP_GET, handleStatus);
    server.on("/goal",        HTTP_GET, handleGoal);
    server.on("/goal-undo",   HTTP_GET, handleGoalUndo);
    server.on("/api/reset",   HTTP_GET, handleApiReset);
    server.on("/reset",       HTTP_GET, handleReset);
    server.on("/a+",          HTTP_GET, handleAplus);
    server.on("/b+",          HTTP_GET, handleBplus);
    server.on("/az",          HTTP_GET, handleAzero);
    server.on("/bz",          HTTP_GET, handleBzero);
    server.begin();
    Serial.println("[http] placar API started on port 80");
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
            break;
        case ACT_CAL_B:
            httpKick(String("http://") + camB.ip + "/calibrate");
            break;
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
    for (int i = 0; i < NUM_BUTTONS; i++) {
        const Button& b = buttons[i];
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return i;
    }
    return -1;
}

static void serviceTouch() {
    // Debounce — relaxed values after losing all touches with the first
    // round. The cooldown is the most important; the stability check is
    // turned off (a single hit is enough) because GT911 polls aren't
    // always perfectly periodic on this build.
    constexpr uint32_t MIN_PRESS_MS    = 30;
    constexpr uint32_t POST_FIRE_MS    = 200;
    constexpr int      SAME_BUTTON_HITS = 1;

    static uint32_t pressStartMs   = 0;
    static uint32_t lastFireMs     = 0;
    static int      candidateIdx   = -1;
    static int      candidateHits  = 0;

    uint32_t now = millis();
    if (now - lastFireMs < POST_FIRE_MS) {
        // Cooldown — drain touch state without acting on it so we
        // start the next gesture from a clean baseline.
        gt911::poll();
        if (!gt911::touched) {
            pressedButton = -1;
            candidateIdx = -1;
            candidateHits = 0;
        }
        return;
    }

    gt911::poll();
    bool isPressed = gt911::touched;
    if (isPressed) {
        int tx = gt911::lastX;
        int ty = gt911::lastY;
        int idx = hitTest(tx, ty);

        // Stability — finger must land on the SAME button for two
        // consecutive polls before we lock it in. Sliding across the
        // screen doesn't accidentally pick up whichever button you're
        // passing over.
        if (idx == candidateIdx) {
            candidateHits++;
        } else {
            candidateIdx = idx;
            candidateHits = 1;
        }

        if (candidateHits >= SAME_BUTTON_HITS && candidateIdx != pressedButton) {
            int prev = pressedButton;
            pressedButton = candidateIdx;
            pressStartMs = now;
            static uint32_t lastTouchLog = 0;
            if (now - lastTouchLog > 250) {
                Serial.printf("[touch] press (%d,%d) → btn=%d\n",
                              tx, ty, pressedButton);
                lastTouchLog = now;
            }
            if (prev >= 0)            drawButton(prev);
            if (pressedButton >= 0)   drawButton(pressedButton);
        }
    } else if (pressedButton >= 0) {
        // Released — only fire if the press lasted long enough to
        // count as deliberate. Short brushes are dropped silently.
        int idx = pressedButton;
        uint32_t held = now - pressStartMs;
        pressedButton = -1;
        candidateIdx = -1;
        candidateHits = 0;
        drawButton(idx);
        if (held >= MIN_PRESS_MS) {
            pendingAction = buttons[idx].action;
            lastFireMs = now;
            Serial.printf("[touch] release btn=%d \"%s\" held=%ums → action=%d\n",
                          idx, buttons[idx].label, (unsigned)held,
                          (int)pendingAction);
        } else {
            Serial.printf("[touch] short tap on btn=%d (held=%ums, dropped)\n",
                          idx, (unsigned)held);
        }
    } else {
        // No touch + no pressed button: reset the candidate so a fresh
        // gesture starts from zero.
        candidateIdx = -1;
        candidateHits = 0;
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
    extractIntField(body, "state", &st);
    extractIntField(body, "goals", &goals);
    extractBoolField(body, "calibrated", &cal);
    bool changed = !c.online || st != c.state || cal != c.calibrated;
    c.online = true; c.state = st; c.calibrated = cal; c.goals = goals;
    if (changed) {
        dirtyStatus = true;
        dirtyButtons = true;  // START/PAUSE label tracks cam state (index 5)
    }
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

    // Service HTTP — placar API runs on this thread (handlers update
    // placarA/placarB + dirtyDigits, so no cross-thread renderer call).
    server.handleClient();

    if (pendingAction != ACT_NONE) {
        ActionId a = pendingAction;
        pendingAction = ACT_NONE;
        runAction(a);
    }

    // Re-render whatever the polling task on core 0 marked dirty. Order
    // matters so overlapping rects redraw correctly (header before
    // digits before buttons before status).
    if (dirtyHeader)  { dirtyHeader  = false; drawHeader(); }
    if (dirtyDigits)  { dirtyDigits  = false; drawScoreDigits(); }
    // START is the 6th button (index 5) after the 4 score-adjust buttons.
    if (dirtyButtons) { dirtyButtons = false; drawButton(5); }
    if (dirtyStatus)  { dirtyStatus  = false; drawSideStatus(); }

    delay(2);  // small yield for FreeRTOS housekeeping
}
