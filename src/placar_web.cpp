#include "placar_web.h"
#include <WebServer.h>

// State and actions live in main_placar.cpp
extern volatile int placarScoreA;
extern volatile int placarScoreB;
extern void placarIncrementA();
extern void placarIncrementB();
extern void placarResetA();
extern void placarResetB();
extern void placarResetAll();
extern void placarSetScore(int a, int b);
extern String placarConnectionInfo();   // "STA 192.168.x.x" or "AP PLACAR_WIFI"

static WebServer server(80);

static String renderPage() {
    String h;
    h.reserve(1200);
    h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>");
    h += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
    h += F("<meta http-equiv='refresh' content='2'>");
    h += F("<title>Placar</title>");
    h += F("<style>");
    h += F("body{font-family:system-ui,sans-serif;text-align:center;background:#111;color:#fff;margin:0;padding:20px}");
    h += F(".box{background:#1a1a1a;padding:20px;border-radius:12px;max-width:420px;margin:0 auto;border:1px solid #333}");
    h += F(".score{font-size:64px;font-weight:bold;margin:20px 0;font-variant-numeric:tabular-nums}");
    h += F("button{width:100%;padding:18px;margin:6px 0;font-size:20px;border:none;border-radius:10px;color:#fff;cursor:pointer}");
    h += F(".a{background:#2d89ef}.b{background:#e81123}.r{background:#666}");
    h += F(".info{font-size:13px;color:#888;margin-top:15px}");
    h += F("</style></head><body><div class='box'>");
    h += F("<h1>Placar</h1>");
    h += F("<div class='score'>");
    h += placarScoreA;
    h += F(" x ");
    h += placarScoreB;
    h += F("</div>");
    h += F("<form action='/a+'><button class='a'>A +</button></form>");
    h += F("<form action='/az'><button class='a'>A RESET</button></form>");
    h += F("<form action='/b+'><button class='b'>B +</button></form>");
    h += F("<form action='/bz'><button class='b'>B RESET</button></form>");
    h += F("<form action='/reset'><button class='r'>RESET GERAL</button></form>");
    h += F("<div class='info'>");
    h += placarConnectionInfo();
    h += F("</div></div></body></html>");
    return h;
}

static void redirectRoot() {
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handleRoot()  { server.send(200, "text/html", renderPage()); }
static void handleAplus() { placarIncrementA(); redirectRoot(); }
static void handleBplus() { placarIncrementB(); redirectRoot(); }
static void handleAzero() { placarResetA();     redirectRoot(); }
static void handleBzero() { placarResetB();     redirectRoot(); }
static void handleReset() { placarResetAll();   redirectRoot(); }

static void handleStatus() {
    String j;
    j.reserve(64);
    j += F("{\"role\":\"scoreboard\",\"a\":");
    j += placarScoreA;
    j += F(",\"b\":");
    j += placarScoreB;
    j += F("}");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", j);
}

// Tiny manual JSON int extractor: looks for "key":N pattern.
// Returns true if found, writes int to *out.
static bool extractInt(const String& body, const char* key, int* out) {
    String needle = String("\"") + key + "\":";
    int i = body.indexOf(needle);
    if (i < 0) return false;
    i += needle.length();
    while (i < (int)body.length() && (body[i] == ' ' || body[i] == '\t')) i++;
    int start = i;
    if (i < (int)body.length() && (body[i] == '-' || body[i] == '+')) i++;
    bool any = false;
    while (i < (int)body.length() && isDigit(body[i])) { i++; any = true; }
    if (!any) return false;
    *out = body.substring(start, i).toInt();
    return true;
}

static void handleSync() {
    String body = server.arg("plain");
    int a = placarScoreA, b = placarScoreB;
    bool gotA = extractInt(body, "a", &a);
    bool gotB = extractInt(body, "b", &b);
    if (!gotA && !gotB) {
        server.send(400, "application/json", "{\"error\":\"need a or b\"}");
        return;
    }
    placarSetScore(a, b);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    handleStatus();
}

static void handleOptions() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
}

void placarWebBegin() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/a+", HTTP_GET, handleAplus);
    server.on("/b+", HTTP_GET, handleBplus);
    server.on("/az", HTTP_GET, handleAzero);
    server.on("/bz", HTTP_GET, handleBzero);
    server.on("/reset", HTTP_GET, handleReset);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/sync", HTTP_POST, handleSync);
    server.on("/sync", HTTP_OPTIONS, handleOptions);
    server.begin();
    Serial.println("placar: web server listening on :80");
}

void placarWebHandle() {
    server.handleClient();
}
