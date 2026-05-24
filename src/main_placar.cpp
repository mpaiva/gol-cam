// =============================================================
// gol-cam — Scoreboard firmware (ESP32 DevKit V1)
//
// Drives 4x MAX7219 8x8 LED matrices (2 per side) showing the
// match score. Listens for goals from the gol-cam camera board
// over WiFi (polling /status as fallback; the camera can also
// push to /sync). Four physical buttons act as manual override.
//
// Connects to the same WiFi as the camera (.env WIFI_SSID/PASSWORD).
// Falls back to standalone AP "PLACAR_WIFI" / 12345678 if STA fails.
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "placar_display.h"
#include "placar_buttons.h"
#include "placar_web.h"

#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Create a .env file with WIFI_SSID=..."
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined."
#endif

// Optional: IP of the camera board, used for goal sync polling.
// If absent, placar runs standalone (only buttons + manual web actions).
#ifndef CAMERA_IP
#define CAMERA_IP ""
#endif

static const char* FALLBACK_AP_SSID = "PLACAR_WIFI";
static const char* FALLBACK_AP_PASS = "12345678";

// ---- Global shared state (read by placar_web.cpp) ----
volatile int placarScoreA = 0;
volatile int placarScoreB = 0;

static PlacarDisplay display;
static PlacarButtons buttons;

// ---- Connection info reporting (read by placar_web.cpp) ----
enum class NetMode { STA, AP };
static NetMode netMode = NetMode::AP;

String placarConnectionInfo() {
    if (netMode == NetMode::STA) {
        return String("STA ") + WiFi.localIP().toString();
    }
    return String("AP ") + FALLBACK_AP_SSID + " / 192.168.4.1";
}

// ---- Score mutation helpers (called by web + buttons + sync) ----
static void refreshDisplay() {
    display.showA(placarScoreA);
    display.showB(placarScoreB);
    Serial.printf("placar: A=%d B=%d\n", placarScoreA, placarScoreB);
}

void placarIncrementA() {
    if (placarScoreA < 99) { placarScoreA++; refreshDisplay(); }
}
void placarIncrementB() {
    if (placarScoreB < 99) { placarScoreB++; refreshDisplay(); }
}
void placarResetA()   { placarScoreA = 0; refreshDisplay(); }
void placarResetB()   { placarScoreB = 0; refreshDisplay(); }
void placarResetAll() { placarScoreA = 0; placarScoreB = 0; refreshDisplay(); }

void placarSetScore(int a, int b) {
    if (a < 0) a = 0; if (a > 99) a = 99;
    if (b < 0) b = 0; if (b > 99) b = 99;
    placarScoreA = a;
    placarScoreB = b;
    refreshDisplay();
}

// ---- Camera polling: detect goal-count increments and apply locally ----
static const char* cameraUrl = CAMERA_IP;
static int lastCameraGoalCount = -1;
static uint32_t lastPollMs = 0;
static const uint32_t POLL_INTERVAL_MS = 500;

// Extract integer value of "key":N from a JSON-ish string.
static bool extractIntField(const String& s, const char* key, int* out) {
    String needle = String("\"") + key + "\":";
    int i = s.indexOf(needle);
    if (i < 0) return false;
    i += needle.length();
    while (i < (int)s.length() && (s[i] == ' ' || s[i] == '\t')) i++;
    int start = i;
    if (i < (int)s.length() && (s[i] == '-' || s[i] == '+')) i++;
    bool any = false;
    while (i < (int)s.length() && isDigit(s[i])) { i++; any = true; }
    if (!any) return false;
    *out = s.substring(start, i).toInt();
    return true;
}

// Extract string field "key":"VALUE" (no escape handling — fine for ASCII).
static bool extractStringField(const String& s, const char* key, String* out) {
    String needle = String("\"") + key + "\":\"";
    int i = s.indexOf(needle);
    if (i < 0) return false;
    i += needle.length();
    int end = s.indexOf('"', i);
    if (end < 0) return false;
    *out = s.substring(i, end);
    return true;
}

static void pollCamera() {
    if (cameraUrl[0] == '\0') return;            // no camera configured
    if (WiFi.status() != WL_CONNECTED) return;   // need STA online

    String url = String("http://") + cameraUrl + "/status";
    HTTPClient http;
    http.setTimeout(800);
    if (!http.begin(url)) return;

    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        int goals = 0;
        if (extractIntField(body, "goalCount", &goals)) {
            if (lastCameraGoalCount < 0) {
                lastCameraGoalCount = goals;     // baseline; no increment on first poll
            } else if (goals > lastCameraGoalCount) {
                int delta = goals - lastCameraGoalCount;
                String side;
                if (!extractStringField(body, "side", &side)) side = "A";
                for (int i = 0; i < delta; i++) {
                    if (side == "B") placarIncrementB();
                    else             placarIncrementA();
                }
                lastCameraGoalCount = goals;
            } else if (goals < lastCameraGoalCount) {
                lastCameraGoalCount = goals;     // camera reset; do not touch placar
            }
        }
    }
    http.end();
}

// ---- WiFi: try STA, fall back to AP ----
static void startWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("placar: connecting to %s ...\n", WIFI_SSID);
    uint32_t deadline = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        netMode = NetMode::STA;
        Serial.printf("placar: STA ok, IP=%s\n", WiFi.localIP().toString().c_str());
    } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS);
        netMode = NetMode::AP;
        Serial.printf("placar: STA failed; AP %s up at %s\n",
                      FALLBACK_AP_SSID, WiFi.softAPIP().toString().c_str());
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("placar: boot");

    display.begin();
    refreshDisplay();   // show 0 x 0

    buttons.begin(placarIncrementA, placarResetA, placarIncrementB, placarResetB);

    startWiFi();
    placarWebBegin();

    if (cameraUrl[0] != '\0') {
        Serial.printf("placar: camera sync target = http://%s/status\n", cameraUrl);
    } else {
        Serial.println("placar: no CAMERA_IP set; running standalone (manual only)");
    }
}

void loop() {
    placarWebHandle();
    buttons.poll();

    uint32_t now = millis();
    if (now - lastPollMs >= POLL_INTERVAL_MS) {
        lastPollMs = now;
        pollCamera();
    }
}
