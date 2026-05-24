#include <WiFi.h>
#include <WebServer.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// ================= WIFI (MODO DIRETO) =================
const char* ap_ssid = "PLACAR_WIFI";
const char* ap_password = "12345678";

// ================= DISPLAY =================
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 2

// LADO A
#define DIN1 23
#define CLK1 18
#define CS1  5

// LADO B
#define DIN2 19
#define CLK2 21
#define CS2  22

// ================= BOTÕES =================
#define UP1 32
#define DW1 33
#define UP2 25
#define DW2 26

// ================= OBJETOS =================
MD_MAX72XX dspA = MD_MAX72XX(HARDWARE_TYPE, DIN1, CLK1, CS1, MAX_DEVICES);
MD_MAX72XX dspB = MD_MAX72XX(HARDWARE_TYPE, DIN2, CLK2, CS2, MAX_DEVICES);

WebServer server(80);

// ================= VARIÁVEIS =================
int contA = 0;
int contB = 0;

// debounce por borda
bool estadoAntUP1 = HIGH;
bool estadoAntDW1 = HIGH;
bool estadoAntUP2 = HIGH;
bool estadoAntDW2 = HIGH;

unsigned long tempoUP1 = 0;
unsigned long tempoDW1 = 0;
unsigned long tempoUP2 = 0;
unsigned long tempoDW2 = 0;

const unsigned long debounceMs = 120;

// ================= FONTES =================
const uint8_t digitos[10][8] = {
  {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
  {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00}, // 1
  {0x3C,0x66,0x06,0x0C,0x30,0x60,0x7E,0x00}, // 2
  {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
  {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00}, // 4
  {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
  {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
  {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
  {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
  {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0x00}  // 9
};

// ================= DISPLAY =================

void limpaModulo(MD_MAX72XX &dsp, uint8_t modulo)
{
  for (uint8_t row = 0; row < 8; row++)
  {
    dsp.setRow(modulo, row, 0x00);
  }
}

// Rotação 90 graus
void desenhaDigitoRot90(MD_MAX72XX &dsp, uint8_t modulo, int num)
{
  for (uint8_t row = 0; row < 8; row++)
  {
    uint8_t novaLinha = 0;

    for (uint8_t col = 0; col < 8; col++)
    {
      if (digitos[num][7 - col] & (1 << (7 - row)))
      {
        novaLinha |= (1 << (7 - col));
      }
    }

    dsp.setRow(modulo, row, novaLinha);
  }
}

// LADO A
// módulo 0 = esquerda (dezena)
// módulo 1 = direita  (unidade)
void mostrarA(int valor)
{
  int dez = valor / 10;
  int uni = valor % 10;

  if (valor < 10)
    limpaModulo(dspA, 0);
  else
    desenhaDigitoRot90(dspA, 0, dez);

  desenhaDigitoRot90(dspA, 1, uni);
}

// LADO B
// módulo 0 = esquerda (dezena)
// módulo 1 = direita  (unidade)
void mostrarB(int valor)
{
  int dez = valor / 10;
  int uni = valor % 10;

  if (valor < 10)
    limpaModulo(dspB, 0);
  else
    desenhaDigitoRot90(dspB, 0, dez);

  desenhaDigitoRot90(dspB, 1, uni);
}

void atualizarDisplays()
{
  mostrarA(contA);
  mostrarB(contB);

  Serial.print("A: ");
  Serial.print(contA);
  Serial.print(" | B: ");
  Serial.println(contB);
}

// ================= CONTROLE =================

void sobeA()
{
  if (contA < 99)
  {
    contA++;
    atualizarDisplays();
  }
}

void sobeB()
{
  if (contB < 99)
  {
    contB++;
    atualizarDisplays();
  }
}

void zeraA()
{
  contA = 0;
  atualizarDisplays();
}

void zeraB()
{
  contB = 0;
  atualizarDisplays();
}

void resetGeral()
{
  contA = 0;
  contB = 0;
  atualizarDisplays();
}

// ================= WEB =================

String pagina()
{
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='2'>";
  h += "<title>Placar</title>";
  h += "<style>";
  h += "body{font-family:Arial;text-align:center;background:#f2f2f2;margin:20px;}";
  h += ".box{background:white;padding:20px;border-radius:12px;max-width:420px;margin:auto;box-shadow:0 0 10px rgba(0,0,0,0.15);}";
  h += ".score{font-size:48px;font-weight:bold;margin:20px 0;}";
  h += "button{width:100%;padding:15px;margin:6px 0;font-size:20px;border:none;border-radius:10px;}";
  h += ".a{background:#2d89ef;color:white;}";
  h += ".b{background:#e81123;color:white;}";
  h += ".r{background:#666;color:white;}";
  h += ".info{font-size:14px;color:#444;margin-top:15px;}";
  h += "</style></head><body>";
  h += "<div class='box'>";
  h += "<h1>Placar</h1>";
  h += "<div class='score'>" + String(contA) + " x " + String(contB) + "</div>";
  h += "<form action='/a+'><button class='a'>A +</button></form>";
  h += "<form action='/az'><button class='a'>A RESET</button></form>";
  h += "<form action='/b+'><button class='b'>B +</button></form>";
  h += "<form action='/bz'><button class='b'>B RESET</button></form>";
  h += "<form action='/reset'><button class='r'>RESET GERAL</button></form>";
  h += "<div class='info'>Conecte-se ao Wi-Fi PLACAR_WIFI e abra 192.168.4.1</div>";
  h += "</div></body></html>";
  return h;
}

void redireciona()
{
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRoot()  { server.send(200, "text/html", pagina()); }
void handleAmais() { sobeA(); redireciona(); }
void handleBmais() { sobeB(); redireciona(); }
void handleAzero() { zeraA(); redireciona(); }
void handleBzero() { zeraB(); redireciona(); }
void handleReset() { resetGeral(); redireciona(); }

// ================= WIFI =================

void iniciarWiFi()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);

  Serial.println();
  Serial.println("Wi-Fi direto ativado");
  Serial.print("Rede: ");
  Serial.println(ap_ssid);
  Serial.print("Senha: ");
  Serial.println(ap_password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

// ================= BOTÕES =================

void lerBotoes()
{
  bool up1 = digitalRead(UP1);
  bool dw1 = digitalRead(DW1);
  bool up2 = digitalRead(UP2);
  bool dw2 = digitalRead(DW2);

  if (estadoAntUP1 == HIGH && up1 == LOW && millis() - tempoUP1 > debounceMs)
  {
    sobeA();
    tempoUP1 = millis();
  }

  if (estadoAntDW1 == HIGH && dw1 == LOW && millis() - tempoDW1 > debounceMs)
  {
    zeraA();
    tempoDW1 = millis();
  }

  if (estadoAntUP2 == HIGH && up2 == LOW && millis() - tempoUP2 > debounceMs)
  {
    sobeB();
    tempoUP2 = millis();
  }

  if (estadoAntDW2 == HIGH && dw2 == LOW && millis() - tempoDW2 > debounceMs)
  {
    zeraB();
    tempoDW2 = millis();
  }

  estadoAntUP1 = up1;
  estadoAntDW1 = dw1;
  estadoAntUP2 = up2;
  estadoAntDW2 = dw2;
}

// ================= SETUP =================

void setup()
{
  Serial.begin(115200);
  delay(300);

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

  server.on("/", handleRoot);
  server.on("/a+", handleAmais);
  server.on("/b+", handleBmais);
  server.on("/az", handleAzero);
  server.on("/bz", handleBzero);
  server.on("/reset", handleReset);
  server.begin();

  Serial.println("Servidor web iniciado");
}

// ================= LOOP =================

void loop()
{
  server.handleClient();
  lerBotoes();
}