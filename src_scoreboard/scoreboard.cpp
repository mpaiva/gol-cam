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

// Display brightness 0-15. Phone-oriented power banks shut off when steady
// draw stays under their idle threshold (~50-150 mA depending on model). At
// brightness 3 with both displays showing zeros the steady draw falls under
// that threshold; even brief brightness pulses up to 15 didn't help (banks
// look at averaged or minimum current, not peaks).
//
// Solution: keep the displays bright enough that the steady draw is solidly
// above any threshold. 13 ≈ 90% of max — very visible, draws ~600 mA total
// with both sides lit, which any bank treats as "real load". On wall-USB it
// looks great; on battery it survives.
const uint8_t NORMAL_BRIGHTNESS = 13;

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
// Static HTML/JS — score updates via /status polling, no meta-refresh.
// Team names stored in browser localStorage (defaults A / B).
static const char PAGINA_HTML[] PROGMEM = R"PG(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>
<title>gol-placar</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
:root{
--bg:#070707;--card:#141414;--border:#262626;--muted:#888;
--a:#2d89ef;--a-glow:rgba(45,137,239,0.45);
--b:#e81123;--b-glow:rgba(232,17,35,0.45);
--ok:#0a0;--warn:#c00
}
html,body{height:100%}
body{font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:#fff;
min-height:100vh;display:flex;flex-direction:column;overflow:hidden;
background-image:radial-gradient(ellipse at top,#1a1a1a 0%,var(--bg) 60%)}
header{display:flex;align-items:center;justify-content:space-between;
padding:10px 16px;border-bottom:1px solid var(--border);font-size:0.85em}
header h1{font-size:1em;font-weight:700;letter-spacing:1px;color:#aaa}
#wifi{display:flex;align-items:center;gap:6px;color:var(--muted);font-family:ui-monospace,monospace;font-size:0.75em}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ok);box-shadow:0 0 8px var(--ok)}
main{flex:1;display:grid;grid-template-columns:1fr auto 1fr;align-items:center;
gap:12px;padding:8px 16px;min-height:0;position:relative}
.side{text-align:center;cursor:default;padding:12px;border-radius:18px;transition:background 0.4s}
.side .name{font-size:0.95em;letter-spacing:3px;color:var(--muted);font-weight:700;
margin-bottom:8px;cursor:text;padding:4px 10px;border-radius:6px;display:inline-block;min-width:80px}
.side .name:hover{background:rgba(255,255,255,0.05)}
.side .name[contenteditable=true]:focus{outline:1px dashed currentColor;background:rgba(0,0,0,0.4)}
.side .num{font-size:clamp(7em,28vw,18em);font-weight:900;line-height:0.85;
font-variant-numeric:tabular-nums;letter-spacing:-0.03em}
.side.a .num{color:var(--a);text-shadow:0 0 30px var(--a-glow)}
.side.b .num{color:var(--b);text-shadow:0 0 30px var(--b-glow)}
.side.a .name{color:var(--a)}
.side.b .name{color:var(--b)}
.side.flash{animation:flash 1.2s ease-out}
.side.a.flash{background:var(--a-glow)}
.side.b.flash{background:var(--b-glow)}
@keyframes flash{
0%{transform:scale(1)} 30%{transform:scale(1.05)} 60%{transform:scale(0.99)} 100%{transform:scale(1)}
}
.x{font-size:clamp(2em,8vw,5em);color:#333;font-weight:300;align-self:center}
footer{display:flex;flex-wrap:wrap;gap:6px;padding:10px 16px 14px;border-top:1px solid var(--border)}
footer .row{display:flex;gap:6px;width:100%}
.btn{flex:1;padding:14px 8px;border:none;border-radius:10px;font-size:0.95em;
font-weight:700;cursor:pointer;color:#fff;transition:transform 0.08s,filter 0.15s;letter-spacing:0.5px}
.btn:active{transform:scale(0.96)}
.btn-a{background:var(--a);box-shadow:0 0 18px rgba(45,137,239,0.25) inset}
.btn-b{background:var(--b);box-shadow:0 0 18px rgba(232,17,35,0.25) inset}
.btn-mini{flex:0 0 auto;background:#222;color:#888;font-size:0.72em;font-weight:600;
padding:8px 12px;letter-spacing:1px}
.btn-reset{background:#1d1d1d;color:#888;font-size:0.78em;border:1px solid #2a2a2a}
@media(orientation:landscape) and (max-height:540px){
header{padding:6px 12px}
main{padding:4px 12px}
.side .num{font-size:clamp(6em,30vh,14em)}
}
@media(orientation:portrait){
main{grid-template-columns:1fr;grid-template-rows:1fr auto 1fr;gap:0}
.x{transform:rotate(90deg)}
.side .num{font-size:clamp(8em,40vw,16em)}
}
.flash-overlay{position:fixed;inset:0;pointer-events:none;opacity:0;
transition:opacity 0.4s;z-index:99}
.flash-overlay.a{background:radial-gradient(circle,var(--a-glow) 0%,transparent 70%)}
.flash-overlay.b{background:radial-gradient(circle,var(--b-glow) 0%,transparent 70%)}
.flash-overlay.on{opacity:1}
</style></head><body>
<header>
<h1>GOL · PLACAR</h1>
<div id='wifi'><span class='dot'></span><span id='ip'>%IP%</span></div>
</header>
<div id='flash' class='flash-overlay'></div>
<main>
<div class='side a' id='side-a'>
<div class='name' contenteditable='true' spellcheck='false' data-side='a'>A</div>
<div class='num' id='num-a'>0</div>
</div>
<div class='x'>×</div>
<div class='side b' id='side-b'>
<div class='name' contenteditable='true' spellcheck='false' data-side='b'>B</div>
<div class='num' id='num-b'>0</div>
</div>
</main>
<footer>
<div class='row'>
<button class='btn btn-a' onclick="bump('a')">A +1</button>
<button class='btn btn-b' onclick="bump('b')">B +1</button>
</div>
<div class='row' style='margin-top:6px'>
<button class='btn btn-mini' onclick="zero('a')">A&nbsp;0</button>
<button class='btn btn-reset' onclick="resetAll()">RESET ALL</button>
<button class='btn btn-mini' onclick="zero('b')">B&nbsp;0</button>
</div>
</footer>
<script>
const $=id=>document.getElementById(id);
let state={a:0,b:0};
['a','b'].forEach(s=>{
 const el=document.querySelector(`.name[data-side="${s}"]`);
 const saved=localStorage.getItem('placar-name-'+s);
 if(saved)el.textContent=saved;
 el.addEventListener('blur',()=>{
   const v=el.textContent.trim().slice(0,8).toUpperCase()||(s==='a'?'A':'B');
   el.textContent=v;
   localStorage.setItem('placar-name-'+s,v);
 });
 el.addEventListener('keydown',e=>{
   if(e.key==='Enter'){e.preventDefault();el.blur();}
 });
});
function flash(side){
 const e=$('side-'+side);
 e.classList.remove('flash');void e.offsetWidth;e.classList.add('flash');
 const o=$('flash');o.className='flash-overlay '+side+' on';
 setTimeout(()=>o.className='flash-overlay '+side,400);
}
function render(d){
 if(d.a!==state.a){state.a=d.a;$('num-a').textContent=d.a;}
 if(d.b!==state.b){state.b=d.b;$('num-b').textContent=d.b;}
 if(d.ip)$('ip').textContent=d.ip;
}
async function bump(s){
 try{const r=await fetch('/goal?side='+s);const d=await r.json();
 if(d.ok){flash(s);render({a:d.a,b:d.b});}}catch(e){}
}
async function zero(s){
 try{await fetch(s==='a'?'/az':'/bz');poll();}catch(e){}
}
async function resetAll(){
 try{await fetch('/api/reset');poll();}catch(e){}
}
async function poll(){
 try{const r=await fetch('/status',{cache:'no-store'});
 const d=await r.json();
 if(d.a>state.a)flash('a');
 if(d.b>state.b)flash('b');
 render(d);}catch(e){}
}
setInterval(poll,1000);poll();
</script></body></html>)PG";

static String pagina() {
  String h(PAGINA_HTML);
  h.replace("%IP%", WiFi.localIP().toString());
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
  dspA.control(MD_MAX72XX::INTENSITY, NORMAL_BRIGHTNESS);
  dspB.control(MD_MAX72XX::INTENSITY, NORMAL_BRIGHTNESS);
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
