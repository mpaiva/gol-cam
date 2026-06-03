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
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TAMC_GT911.h>

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

// GT911 touchscreen. Constructed with sentinel pins for INT/RST + I2C
// addr 0x5D (default for 800-wide panels — alternative 0x14 if comms fail).
TAMC_GT911 ts = TAMC_GT911(pins::TOUCH_SDA, pins::TOUCH_SCL,
                            pins::TOUCH_INT, pins::TOUCH_RST,
                            800, 480);

// =============================================================
// Camera state — both cameras polled every CAM_POLL_MS so we can render
// status pills under HOME/AWAY without serially blocking the touch loop.
// =============================================================
constexpr uint32_t PLACAR_POLL_MS = 500;
constexpr uint32_t CAM_POLL_MS    = 1000;

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

static volatile int placarA = -1;
static volatile int placarB = -1;
static volatile bool placarOnline = false;
static uint32_t lastPlacarPollMs = 0;
static bool firstFullDraw = true;

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
};
static volatile ActionId pendingAction = ACT_NONE;

struct Button {
    int x, y, w, h;
    const char* label;
    uint16_t fill;
    uint16_t fillPressed;
    ActionId action;
};
static Button buttons[] = {
    {  20, 300, 170, 80, "CAL A",  0xFD20, 0xFEC0, ACT_CAL_A },         // orange
    { 210, 300, 170, 80, "START",  0x0640, 0x07E0, ACT_START_PAUSE },   // green; label flips runtime
    { 420, 300, 170, 80, "RESET",  0x6000, 0xA800, ACT_RESET_ALL },     // dark red
    { 610, 300, 170, 80, "CAL B",  0xFD20, 0xFEC0, ACT_CAL_B },
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
// HTTP fan-out — synchronous GETs queued from the main loop. Human-paced
// button clicks (one per second tops) don't justify the complexity of
// background tasks. Worst-case fan-out is 3 hosts × ~200 ms = 600 ms,
// which still leaves the touch read loop responsive.
// =============================================================
static void httpKick(const char* url) {
    HTTPClient http;
    if (!http.begin(url)) return;
    http.setTimeout(1500);
    int code = http.GET();
    Serial.printf("[http] GET %s → %d\n", url, code);
    http.end();
}

static void runAction(ActionId a) {
    char url[160];
    switch (a) {
        case ACT_CAL_A:
            httpKick(("http://" + String(camA.ip) + "/calibrate").c_str());
            break;
        case ACT_CAL_B:
            httpKick(("http://" + String(camB.ip) + "/calibrate").c_str());
            break;
        case ACT_START_PAUSE: {
            // Toggle: if either cam is playing → pause both; if any is paused →
            // resume; otherwise start both. /start no-ops on uncalibrated cams.
            bool anyPlaying = (camA.state == 2) || (camB.state == 2);
            bool anyPaused  = (camA.state == 3) || (camB.state == 3);
            const char* cmd = anyPlaying ? "pause" : (anyPaused ? "resume" : "start");
            snprintf(url, sizeof(url), "http://%s/%s", camA.ip, cmd);
            httpKick(url);
            snprintf(url, sizeof(url), "http://%s/%s", camB.ip, cmd);
            httpKick(url);
            break;
        }
        case ACT_RESET_ALL:
            httpKick(("http://" + String(camA.ip) + "/reset").c_str());
            httpKick(("http://" + String(camB.ip) + "/reset").c_str());
            httpKick(("http://" SCOREBOARD_IP "/api/reset"));
            break;
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

    // Right-side status pill — colour reflects placar reachability.
    const char* tag = placarOnline ? "PLACAR" : "OFFLINE";
    uint16_t pillCol = placarOnline ? 0x0640 : 0x8800;
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

    gfx->fillRect(0, 70, 800, 220, COL_BG);

    gfx->setTextSize(15);
    gfx->setTextColor(COL_SCORE, COL_BG);
    const int yDigit = 110;
    const int aw = (int)strlen(a) * 75;
    const int bw = (int)strlen(b) * 75;
    gfx->setCursor(200 - aw/2, yDigit);
    gfx->print(a);
    gfx->setCursor(600 - bw/2, yDigit);
    gfx->print(b);

    gfx->setTextSize(5);
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(385, 145);
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
    gfx->fillRect(0, 400, 800, 80, COL_BG);

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
        gfx->fillRoundRect(cx - pw/2, 444, pw, 28, 6, pillCol);
        gfx->setTextSize(2);
        gfx->setTextColor(COL_WHITE, pillCol);
        gfx->setCursor(cx - pw/2 + 10, 450);
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
    ts.read();
    bool isPressed = (ts.isTouched && ts.touches > 0);
    if (isPressed) {
        int tx = ts.points[0].x;
        int ty = ts.points[0].y;
        int idx = hitTest(tx, ty);
        if (idx != pressedButton) {
            int prev = pressedButton;
            pressedButton = idx;
            if (prev >= 0) drawButton(prev);
            if (idx >= 0)  drawButton(idx);
        }
    } else if (pressedButton >= 0) {
        // Released — fire the action of the currently-pressed button.
        int idx = pressedButton;
        pressedButton = -1;
        drawButton(idx);
        pendingAction = buttons[idx].action;
        Serial.printf("[touch] release on button %d → action %d\n",
                      idx, (int)pendingAction);
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

static void pollPlacar() {
    HTTPClient http;
    String url = String("http://" SCOREBOARD_IP "/status");
    if (!http.begin(url)) { placarOnline = false; return; }
    http.setTimeout(1200);
    int code = http.GET();
    if (code != 200) { placarOnline = false; http.end(); return; }
    String body = http.getString();
    http.end();
    int newA = placarA, newB = placarB;
    extractIntField(body, "a", &newA);
    extractIntField(body, "b", &newB);
    bool wasOnline = placarOnline;
    placarOnline = true;
    if (!wasOnline || newA != placarA || newB != placarB) {
        placarA = newA;
        placarB = newB;
        drawHeader();     // pill changes colour with online state
        drawScoreDigits();
    }
}

static void pollCam(CamState& c) {
    HTTPClient http;
    String url = "http://" + String(c.ip) + "/status";
    if (!http.begin(url)) { c.online = false; drawSideStatus(); return; }
    http.setTimeout(1200);
    int code = http.GET();
    if (code != 200) {
        bool was = c.online;
        c.online = false;
        http.end();
        if (was) drawSideStatus();
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
        drawSideStatus();
        drawButton(1);  // Start/Pause label depends on cam states
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

    // GT911 i2c init — use the pins shared with the touch SDA/SCL header.
    Wire.begin(pins::TOUCH_SDA, pins::TOUCH_SCL);
    ts.begin();
    ts.setRotation(ROTATION_NORMAL);
    Serial.println("[touch] GT911 initialised (polling mode)");

    connectWiFi();
    renderSplash("Aguardando placar...", COL_DIM);
    Serial.printf("[hmi] placar=%s  camA=%s  camB=%s\n",
                  SCOREBOARD_IP, CAM_A_IP, CAM_B_IP);

    // First full draw so all four button rects exist before the first
    // touch event can land on them.
    renderFull();
}

void loop() {
    uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        delay(500);
        return;
    }

    // Touch is the highest-frequency check — every loop iteration.
    serviceTouch();

    // Fire any queued button action.
    if (pendingAction != ACT_NONE) {
        ActionId a = pendingAction;
        pendingAction = ACT_NONE;
        runAction(a);
    }

    if (now - lastPlacarPollMs >= PLACAR_POLL_MS) {
        lastPlacarPollMs = now;
        pollPlacar();
    }
    if (now - camA.lastPollMs >= CAM_POLL_MS) {
        camA.lastPollMs = now;
        pollCam(camA);
    }
    if (now - camB.lastPollMs >= CAM_POLL_MS) {
        camB.lastPollMs = now;
        pollCam(camB);
    }

    delay(5);  // give the touch IC i2c bus + IDLE for WiFi housekeeping
}
