// =============================================================
// gol-cam HMI — CrowPanel ESP32-S3 5" 800x480 touchscreen placar
//
// Phase 1 scaffold (this file): connects to WiFi, polls the existing
// MAX7219 placar at ${SCOREBOARD_IP}/status every 500 ms, parses the
// {"a":N,"b":M} response, and logs score changes to Serial. The LVGL
// display + touch path is stubbed at the TODO markers — the goal of
// this commit is to validate the network-side wiring against the
// running placar before pulling in the display lib (~MB of compiled
// code + new failure modes).
//
// Hardware: CrowPanel DIS07050H
//   MCU         ESP32-S3-WROOM-1-N4R8 (240 MHz, 4 MB Flash, 8 MB PSRAM)
//   Display     800x480 IPS, parallel RGB (ILI6122 / ILI5960)
//   Touch       Capacitive, I2C (typically GT911)
//   Audio       I2S speaker header
//   Storage     microSD over SPI
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#ifndef WIFI_SSID
#error "WIFI_SSID not defined — set it in .env (see CLAUDE.md)"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined — set it in .env"
#endif
#ifndef SCOREBOARD_IP
#error "SCOREBOARD_IP not defined — set it in .env (the MAX7219 placar IP)"
#endif

// CrowPanel DIS07050H pinout (from Elecrow wiki). Kept here for the
// LVGL display init that lands in Phase 2 — unused for now.
// Avoid bare B0/B1/... and R0/R1/... — those collide with Arduino's
// bit-position macros. Prefix with the channel.
namespace pins {
    // Parallel RGB data lines (LSB → MSB per channel)
    constexpr int LCD_B0=8,  LCD_B1=3,  LCD_B2=46, LCD_B3=9,  LCD_B4=1;
    constexpr int LCD_G0=5,  LCD_G1=6,  LCD_G2=7,  LCD_G3=15, LCD_G4=16, LCD_G5=4;
    constexpr int LCD_R0=45, LCD_R1=48, LCD_R2=47, LCD_R3=21, LCD_R4=14;
    // Control signals
    constexpr int LCD_HSYNC=39, LCD_VSYNC=41, LCD_DE=40, LCD_PCLK=0;
    // Backlight (active high)
    constexpr int LCD_BACKLIGHT = 2;
    // Capacitive touch (GT911) on I2C
    constexpr int TOUCH_SDA=19, TOUCH_SCL=20;
    // I2S audio (mirrors the gol-cam camera's speaker wiring)
    constexpr int I2S_BCK=18, I2S_WS=42, I2S_DATA=17;
    // microSD (SPI)
    constexpr int SD_CS=10, SD_MOSI=11, SD_CLK=12, SD_MISO=13;
}

// Latest score read from the placar. Kept volatile because Phase 2 will
// have an LVGL task on the other core re-rendering when these change.
static volatile int placarA = 0;
static volatile int placarB = 0;
static volatile bool placarOnline = false;

// Tiny JSON int extractor that doesn't pull in ArduinoJson. The placar
// /status response is short and stable enough that substring slicing
// is fine here.
//   In:  body = '{"role":"scoreboard","a":3,"b":1,"ip":"192.168.40.89"}'
//        key  = "a"
//   Out: 3
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

    // Phase 2 will initialise the RGB panel + GT911 touch + LVGL here.
    // For now, hold the backlight off so the screen stays dark while
    // the scaffold is just polling the placar.
    pinMode(pins::LCD_BACKLIGHT, OUTPUT);
    digitalWrite(pins::LCD_BACKLIGHT, LOW);

    connectWiFi();
    Serial.printf("[hmi] target placar = http://%s/status (poll @ 500 ms)\n",
                  SCOREBOARD_IP);
}

void loop() {
    static uint32_t lastPoll = 0;
    static uint32_t lastLog  = 0;
    uint32_t now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        if (now - lastLog >= 5000) {
            Serial.println("[wifi] disconnected — reconnecting");
            lastLog = now;
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
        placarOnline = false;
        return;
    }
    http.setTimeout(1500);
    int code = http.GET();
    if (code != 200) {
        if (placarOnline) {
            Serial.printf("[hmi] placar offline (HTTP %d)\n", code);
        }
        placarOnline = false;
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
        Serial.printf("[hmi] placar = %d x %d  (LVGL render goes here)\n",
                      placarA, placarB);
        // TODO Phase 2: invalidate the LVGL score labels so the next
        // lv_task_handler() draws the new digits onto the panel.
    }
}
