// =============================================================
// Placar Eletrônico Wi-Fi — adapted from
//   https://github.com/mpaiva/placar-eletronico-wifi
// for integration with gol-cam: STA mode, .env-driven Wi-Fi,
// HTTP /goal + /status endpoints so the cameras can push goals.
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// ---------- Wi-Fi (from .env via load_env.py) ----------
#ifndef WIFI_SSID
#error "WIFI_SSID not defined. Add WIFI_SSID to .env"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not defined. Add WIFI_PASSWORD to .env"
#endif

// ---------- Display ----------
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 2

// Side A
#define DIN1 23
#define CLK1 18
#define CS1  5

// Side B
#define DIN2 19
#define CLK2 21
#define CS2  22

// ---------- Buttons ----------
#define UP1 32
#define DW1 33
#define UP2 25
#define DW2 26

// ---------- Globals ----------
MD_MAX72XX dspA = MD_MAX72XX(HARDWARE_TYPE, DIN1, CLK1, CS1, MAX_DEVICES);
MD_MAX72XX dspB = MD_MAX72XX(HARDWARE_TYPE, DIN2, CLK2, CS2, MAX_DEVICES);
WebServer server(80);

int contA = 0;
int contB = 0;

bool estadoAntUP1 = HIGH, estadoAntDW1 = HIGH, estadoAntUP2 = HIGH, estadoAntDW2 = HIGH;
unsigned long tempoUP1 = 0, tempoDW1 = 0, tempoUP2 = 0, tempoDW2 = 0;
const unsigned long debounceMs = 120;

// ---------- 7×8 digit font (rotated 90° later) ----------
const uint8_t digitos[10][8] = {
  {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
  {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00},
  {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00},
  {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
  {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00},
  {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
  {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00},
  {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
  {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
  {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}
};

// ---------- Display helpers ----------
static void limpaModulo(MD_MAX72XX &dsp, uint8_t modulo) {
  for (uint8_t row = 0; row < 8; row++) dsp.setRow(modulo, row, 0x00);
}

static void desenhaDigitoRot90(MD_MAX72XX &dsp, uint8_t modulo, int num) {
  for (uint8_t row = 0; row < 8; row++) {
    uint8_t novaLinha = 0;
    for (uint8_t col = 0; col < 8; col++) {
      if (digitos[num][7 - col] & (1 << (7 - row))) {
        novaLinha |= (1 << (7 - col));
      }
    }
    dsp.setRow(modulo, row, novaLinha);
  }
}

static void mostrarA(int valor) {
  int dez = valor / 10, uni = valor % 10;
  if (valor < 10) limpaModulo(dspA, 0); else desenhaDigitoRot90(dspA, 0, dez);
  desenhaDigitoRot90(dspA, 1, uni);
}

static void mostrarB(int valor) {
  int dez = valor / 10, uni = valor % 10;
  if (valor < 10) limpaModulo(dspB, 0); else desenhaDigitoRot90(dspB, 0, dez);
  desenhaDigitoRot90(dspB, 1, uni);
}

static void atualizarDisplays() {
  mostrarA(contA);
  mostrarB(contB);
  Serial.printf("[score] A:%d B:%d\n", contA, contB);
}

// ---------- Score control ----------
static void sobeA() { if (contA < 99) { contA++; atualizarDisplays(); } }
static void sobeB() { if (contB < 99) { contB++; atualizarDisplays(); } }
static void zeraA() { contA = 0; atualizarDisplays(); }
static void zeraB() { contB = 0; atualizarDisplays(); }
static void resetGeral() { contA = 0; contB = 0; atualizarDisplays(); }

// ---------- Web ----------
static String pagina() {
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='2'>";
  h += "<title>Placar</title>";
  h += "<style>"
       "body{font-family:system-ui,sans-serif;text-align:center;background:#0a0a0a;color:#fff;margin:0;padding:24px}"
       ".box{background:#161616;border:1px solid #262626;padding:24px;border-radius:14px;max-width:420px;margin:auto}"
       "h1{margin:0 0 12px;font-size:1.4em;letter-spacing:0.5px}"
       ".score{font-size:5em;font-weight:800;margin:14px 0;color:#0a0;text-shadow:0 0 20px rgba(0,255,80,0.4)}"
       ".x{color:#444;font-size:0.6em;margin:0 12px;vertical-align:middle}"
       "form{margin:6px 0}"
       "button{width:100%;padding:14px;font-size:1.05em;border:none;border-radius:10px;font-weight:700;cursor:pointer}"
       ".a{background:#2d89ef;color:#fff}.b{background:#e81123;color:#fff}.r{background:#444;color:#fff}"
       ".info{font-size:0.78em;color:#888;margin-top:14px}"
       "</style></head><body>";
  h += "<div class='box'><h1>Placar</h1>";
  h += "<div class='score'>" + String(contA) + "<span class='x'>×</span>" + String(contB) + "</div>";
  h += "<form action='/a+'><button class='a'>A +</button></form>";
  h += "<form action='/az'><button class='a'>A RESET</button></form>";
  h += "<form action='/b+'><button class='b'>B +</button></form>";
  h += "<form action='/bz'><button class='b'>B RESET</button></form>";
  h += "<form action='/reset'><button class='r'>RESET GERAL</button></form>";
  h += "<div class='info'>" + WiFi.localIP().toString() + "</div>";
  h += "</div></body></html>";
  return h;
}

static void redireciona() {
  server.sendHeader("Location", "/");
  server.send(303);
}

static void sendJson(int code, const String& body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

static void handleRoot()  { server.send(200, "text/html; charset=UTF-8", pagina()); }
static void handleAmais() { sobeA(); redireciona(); }
static void handleBmais() { sobeB(); redireciona(); }
static void handleAzero() { zeraA(); redireciona(); }
static void handleBzero() { zeraB(); redireciona(); }
static void handleReset() { resetGeral(); redireciona(); }

// New: programmatic goal endpoint for the gol-cams.
//   GET /goal?side=a   → +1 to side A
//   GET /goal?side=b   → +1 to side B
static void handleGoal() {
  String side = server.arg("side");
  side.toLowerCase();
  if (side == "a")      { sobeA(); }
  else if (side == "b") { sobeB(); }
  else {
    sendJson(400, "{\"ok\":false,\"err\":\"side must be a or b\"}");
    return;
  }
  String body = "{\"ok\":true,\"side\":\"" + side + "\",\"a\":" + contA + ",\"b\":" + contB + "}";
  sendJson(200, body);
}

// New: status endpoint for dashboard polling.
static void handleStatus() {
  String body = "{\"role\":\"scoreboard\",\"a\":" + String(contA) + ",\"b\":" + String(contB) +
                ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  sendJson(200, body);
}

// New: programmatic reset.
static void handleResetApi() {
  resetGeral();
  sendJson(200, "{\"ok\":true,\"a\":0,\"b\":0}");
}

// ---------- Wi-Fi (STA) ----------
static void iniciarWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("gol-placar");
#ifdef SCOREBOARD_STATIC_IP
  IPAddress staticIP, gateway, subnet;
  staticIP.fromString(SCOREBOARD_STATIC_IP);
  gateway.fromString(SCOREBOARD_GATEWAY);
  subnet.fromString(SCOREBOARD_SUBNET);
  IPAddress dns(8, 8, 8, 8);
  WiFi.config(staticIP, gateway, subnet, dns);
  Serial.printf("[wifi] static IP requested: %s\n", SCOREBOARD_STATIC_IP);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[wifi] connected — IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[wifi] connect FAILED — running offline");
  }
}

// ---------- Buttons ----------
static void lerBotoes() {
  bool up1 = digitalRead(UP1);
  bool dw1 = digitalRead(DW1);
  bool up2 = digitalRead(UP2);
  bool dw2 = digitalRead(DW2);

  if (estadoAntUP1 == HIGH && up1 == LOW && millis() - tempoUP1 > debounceMs) {
    sobeA(); tempoUP1 = millis();
  }
  if (estadoAntDW1 == HIGH && dw1 == LOW && millis() - tempoDW1 > debounceMs) {
    zeraA(); tempoDW1 = millis();
  }
  if (estadoAntUP2 == HIGH && up2 == LOW && millis() - tempoUP2 > debounceMs) {
    sobeB(); tempoUP2 = millis();
  }
  if (estadoAntDW2 == HIGH && dw2 == LOW && millis() - tempoDW2 > debounceMs) {
    zeraB(); tempoDW2 = millis();
  }
  estadoAntUP1 = up1; estadoAntDW1 = dw1;
  estadoAntUP2 = up2; estadoAntDW2 = dw2;
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== placar-eletronico starting ===");

  pinMode(UP1, INPUT_PULLUP);
  pinMode(DW1, INPUT_PULLUP);
  pinMode(UP2, INPUT_PULLUP);
  pinMode(DW2, INPUT_PULLUP);

  dspA.begin();
  dspB.begin();
  dspA.control(MD_MAX72XX::INTENSITY, 3);
  dspB.control(MD_MAX72XX::INTENSITY, 3);
  dspA.clear();
  dspB.clear();
  atualizarDisplays();

  iniciarWiFi();

  // Manual web UI (kept compatible with the original sketch)
  server.on("/", handleRoot);
  server.on("/a+", handleAmais);
  server.on("/b+", handleBmais);
  server.on("/az", handleAzero);
  server.on("/bz", handleBzero);
  server.on("/reset", handleReset);

  // New programmatic API for the gol-cams + dashboards
  server.on("/goal", handleGoal);
  server.on("/status", handleStatus);
  server.on("/api/reset", handleResetApi);

  server.begin();
  Serial.println("[http] server started on port 80");
  Serial.printf("[http] open  http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.println("=== placar-eletronico ready ===");
}

void loop() {
  server.handleClient();
  lerBotoes();
}
