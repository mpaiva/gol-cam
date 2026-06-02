// =============================================================
// gol-cam HMI — CrowPanel ESP32-S3 5" 800x480 touchscreen placar
//
// Phase 2: passive placar mirror, *with* on-screen rendering. The
// firmware connects to WiFi, polls the existing MAX7219 placar at
// ${SCOREBOARD_IP}/status every 500 ms, and draws "A × B" in huge
// green digits on the panel. No touch yet — Phase 3 adds GT911
// + on-screen calibrate/start/VAR controls.
//
// Hardware: CrowPanel DIS07050H
//   MCU         ESP32-S3-WROOM-1-N4R8 (240 MHz, 4 MB Flash, 8 MB PSRAM)
//   Display     800x480 IPS, parallel RGB (ILI6122 / ILI5960)
//   Touch       Capacitive, I2C (typically GT911)        [Phase 3]
//   Audio       I2S speaker header                        [Phase 3]
//   Storage     microSD over SPI                          [Phase 3]
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>

#ifndef WIFI_SSID
#error "WIFI_SSID not defined — set it in .env (see CLAUDE.md)"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined — set it in .env"
#endif
#ifndef SCOREBOARD_IP
#error "SCOREBOARD_IP not defined — set it in .env (the MAX7219 placar IP)"
#endif

// CrowPanel DIS07050H pinout (from Elecrow wiki). Bare B0/B1/.., R0/R1/..
// collide with Arduino's bit-position macros so we prefix with the channel.
namespace pins {
    // Parallel RGB data lines (LSB → MSB per channel)
    constexpr int LCD_B0=8,  LCD_B1=3,  LCD_B2=46, LCD_B3=9,  LCD_B4=1;
    constexpr int LCD_G0=5,  LCD_G1=6,  LCD_G2=7,  LCD_G3=15, LCD_G4=16, LCD_G5=4;
    constexpr int LCD_R0=45, LCD_R1=48, LCD_R2=47, LCD_R3=21, LCD_R4=14;
    // Control signals
    constexpr int LCD_HSYNC=39, LCD_VSYNC=41, LCD_DE=40, LCD_PCLK=0;
    // Backlight (active high)
    constexpr int LCD_BACKLIGHT = 2;
    // Capacitive touch (GT911) on I2C — Phase 3
    constexpr int TOUCH_SDA=19, TOUCH_SCL=20;
    // I2S audio — Phase 3
    constexpr int I2S_BCK=18, I2S_WS=42, I2S_DATA=17;
    // microSD (SPI) — Phase 3
    constexpr int SD_CS=10, SD_MOSI=11, SD_CLK=12, SD_MISO=13;
}

// Parallel RGB panel + auto-flushed canvas. Arduino_GFX allocates the
// framebuffer (800×480×2 = 750 KB) in PSRAM under the hood when
// PSRAM is available — no manual buffer management required.
//
// Timing values are the standard "loose porch" config that works for
// most 800×480 panels. If the screen shows garbled output, the first
// thing to tune is hsync/vsync front/back porch widths.
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    pins::LCD_DE, pins::LCD_VSYNC, pins::LCD_HSYNC, pins::LCD_PCLK,
    pins::LCD_R0, pins::LCD_R1, pins::LCD_R2, pins::LCD_R3, pins::LCD_R4,
    pins::LCD_G0, pins::LCD_G1, pins::LCD_G2, pins::LCD_G3, pins::LCD_G4, pins::LCD_G5,
    pins::LCD_B0, pins::LCD_B1, pins::LCD_B2, pins::LCD_B3, pins::LCD_B4,
    1, 8, 4, 8,    // hsync_polarity, hsync_front_porch, hsync_pulse_width, hsync_back_porch
    1, 8, 4, 8,    // vsync_polarity, vsync_front_porch, vsync_pulse_width, vsync_back_porch
    1,             // pclk_active_neg
    14000000       // prefer_speed (14 MHz — conservative)
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    800, 480, rgbpanel, 0 /*rotation*/, true /*auto_flush*/);

// Latest score read from the placar.
static volatile int placarA = -1;       // -1 forces a redraw on the first poll
static volatile int placarB = -1;
static volatile bool placarOnline = false;
static bool firstDraw = true;

// RGB565 colours
constexpr uint16_t COL_BG    = 0x0000;  // black
constexpr uint16_t COL_SCORE = 0x07E0;  // green
constexpr uint16_t COL_LABEL = 0x528A;  // muted grey
constexpr uint16_t COL_DIM   = 0x39C7;  // very dim grey for placeholder
constexpr uint16_t COL_RED   = 0xF800;
constexpr uint16_t COL_WHITE = 0xFFFF;

// Tiny JSON int extractor that doesn't pull in ArduinoJson.
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

// Draw the static header bar (called once at boot + on full redraws).
static void drawHeader() {
    gfx->fillRect(0, 0, 800, 60, 0x18E3);
    gfx->setTextColor(COL_WHITE, 0x18E3);
    gfx->setTextSize(3);
    gfx->setCursor(20, 18);
    gfx->print("gol-cam");
    // Status pill on the right
    const char* tag = placarOnline ? "PLACAR" : "OFFLINE";
    uint16_t pillCol = placarOnline ? 0x0640 : 0x8800;  // dark green / dark red
    int pillW = (int)strlen(tag) * 18 + 24;
    gfx->fillRoundRect(800 - pillW - 20, 14, pillW, 32, 6, pillCol);
    gfx->setTextSize(2);
    gfx->setCursor(800 - pillW - 8, 22);
    gfx->print(tag);
}

// Render the score screen. Two huge digits separated by × with HOME/AWAY
// labels underneath. Called on every change to placarA/B.
static void renderScore() {
    if (firstDraw) {
        gfx->fillScreen(COL_BG);
        firstDraw = false;
    }

    drawHeader();

    // Clear the digit area
    gfx->fillRect(0, 80, 800, 340, COL_BG);

    char a[8], b[8];
    snprintf(a, sizeof(a), "%d", placarA < 0 ? 0 : placarA);
    snprintf(b, sizeof(b), "%d", placarB < 0 ? 0 : placarB);

    // Built-in 5x7 font at setTextSize(20) → 100 wide × 140 tall per character.
    // A 1- or 2-digit score sits comfortably in the 0..380 / 420..800 columns.
    gfx->setTextSize(20);
    gfx->setTextColor(COL_SCORE, COL_BG);

    int aw = (int)strlen(a) * 100;
    int bw = (int)strlen(b) * 100;
    int yDigit = 130;
    gfx->setCursor(190 - aw/2, yDigit);  // centred in the left half
    gfx->print(a);
    gfx->setCursor(610 - bw/2, yDigit);  // centred in the right half
    gfx->print(b);

    // × in the middle, smaller and dimmer
    gfx->setTextSize(8);
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(380, 180);
    gfx->print("x");

    // HOME / AWAY labels
    gfx->fillRect(0, 420, 800, 60, COL_BG);
    gfx->setTextSize(3);
    gfx->setTextColor(COL_LABEL, COL_BG);
    gfx->setCursor(155, 432);
    gfx->print("HOME");
    gfx->setCursor(575, 432);
    gfx->print("AWAY");
}

// Splash screen shown until the first poll succeeds.
static void renderSplash(const char* msg, uint16_t col) {
    if (firstDraw) {
        gfx->fillScreen(COL_BG);
        firstDraw = false;
    }
    drawHeader();
    gfx->fillRect(0, 80, 800, 340, COL_BG);
    gfx->setTextSize(4);
    gfx->setTextColor(col, COL_BG);
    int w = (int)strlen(msg) * 24;
    gfx->setCursor(400 - w/2, 220);
    gfx->print(msg);
}

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

void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println("\n=== gol-cam HMI (CrowPanel) starting ===");
    Serial.printf("[boot] PSRAM total = %u bytes, free = %u bytes\n",
                  (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());
    Serial.printf("[boot] heap free   = %u bytes\n",
                  (unsigned)ESP.getFreeHeap());

    // Bring up the panel BEFORE WiFi so the operator sees the splash
    // immediately, instead of staring at a black screen for ~3 s.
    if (!gfx->begin()) {
        Serial.println("[gfx] ERROR: panel init failed");
    } else {
        Serial.println("[gfx] panel initialised");
    }
    pinMode(pins::LCD_BACKLIGHT, OUTPUT);
    digitalWrite(pins::LCD_BACKLIGHT, HIGH);
    renderSplash("Conectando WiFi...", COL_DIM);

    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        renderSplash("Aguardando placar...", COL_DIM);
    } else {
        renderSplash("WiFi falhou", COL_RED);
    }
    Serial.printf("[hmi] target placar = http://%s/status (poll @ 500 ms)\n",
                  SCOREBOARD_IP);
}

void loop() {
    static uint32_t lastPoll = 0;
    static uint32_t lastWifiLog  = 0;
    uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        if (now - lastWifiLog >= 5000) {
            Serial.println("[wifi] disconnected — reconnecting");
            renderSplash("WiFi caiu, reconectando...", COL_RED);
            lastWifiLog = now;
        }
        WiFi.reconnect();
        delay(500);
        return;
    }

    if (now - lastPoll < 500) {
        delay(10);
        return;
    }
    lastPoll = now;

    HTTPClient http;
    String url = String("http://") + SCOREBOARD_IP + "/status";
    if (!http.begin(url)) {
        Serial.println("[hmi] http.begin failed");
        if (placarOnline) {
            placarOnline = false;
            renderSplash("Placar inalcancavel", COL_RED);
        }
        return;
    }
    http.setTimeout(1500);
    int code = http.GET();
    if (code != 200) {
        if (placarOnline) {
            Serial.printf("[hmi] placar offline (HTTP %d)\n", code);
            placarOnline = false;
            renderSplash("Placar offline", COL_RED);
        }
        http.end();
        return;
    }
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
        Serial.printf("[hmi] placar = %d x %d  (redrawing)\n", placarA, placarB);
        renderScore();
    }
}
