# Placar Eletrônico Wi-Fi (ESP32)

Placar de partidas com **dois lados (A e B, 0–99)** exibido em matrizes de LED 8×8 baseadas no chip MAX7219, controlado por **botões físicos** e por uma **interface web** servida pelo próprio ESP32 em modo Access Point — não precisa de roteador nem internet.

![Interface web do placar](Screenshot%202026-04-07%20082105.png)

---

## Funcionalidades

- Dois displays independentes (lado A e lado B), cada um com 2 dígitos (até 99).
- Quatro botões físicos: incrementar/zerar cada lado.
- Servidor web embutido com botões grandes para celular (`A+`, `B+`, reset individual, reset geral).
- Auto-refresh da página a cada 2 s, mostrando o placar em tempo real.
- Modo **Access Point** (rede `PLACAR_WIFI`, IP `192.168.4.1`) — qualquer celular se conecta direto.
- Debounce de 120 ms por borda de descida nos botões.

---

## Lista de materiais (BOM)

| Qtd. | Componente | Observações |
|---|---|---|
| 1 | Placa ESP32 DevKit V1 (USB-C ou micro-USB) | Qualquer ESP32 com pelo menos 8 GPIOs livres serve |
| 4 | Módulos MAX7219 8×8 (FC-16) | 2 módulos por lado, encadeados (DOUT → DIN) |
| 4 | Botões push (momentâneos, normalmente abertos) | Ligados entre GPIO e GND, usam `INPUT_PULLUP` interno |
| — | Jumpers / fios e fonte 5 V (USB serve) | Os MAX7219 funcionam bem alimentados pelo `5V`/`VIN` do ESP32 |

> Se usar mais módulos ou um chassi diferente, ajuste `MAX_DEVICES` e a função de desenho do dígito.

---

## Diagrama de ligação

### Pinout do ESP32 (referência)

![Pinout do ESP32](Pinos%20ESP32.jpeg)

### Display Lado A (2 módulos MAX7219)

| Sinal MAX7219 | GPIO ESP32 | `#define` no código |
|---|---|---|
| DIN  | **23** | `DIN1` |
| CLK  | **18** | `CLK1` |
| CS   | **5**  | `CS1`  |
| VCC  | 5V (`VIN`) | — |
| GND  | GND | — |

### Display Lado B (2 módulos MAX7219)

| Sinal MAX7219 | GPIO ESP32 | `#define` no código |
|---|---|---|
| DIN  | **19** | `DIN2` |
| CLK  | **21** | `CLK2` |
| CS   | **22** | `CS2`  |
| VCC  | 5V (`VIN`) | — |
| GND  | GND | — |

> Em cada lado, ligue o `DOUT` do primeiro módulo no `DIN` do segundo (encadeamento). Os pinos `CLK` e `CS` são compartilhados entre os módulos do mesmo lado.

### Botões

Cada botão liga um GPIO ao **GND**. Os pinos são configurados como `INPUT_PULLUP`, então **não precisa de resistor externo**.

| Botão | GPIO ESP32 | Ação |
|---|---|---|
| UP1 | **32** | Soma 1 ao lado A |
| DW1 | **33** | Zera lado A |
| UP2 | **25** | Soma 1 ao lado B |
| DW2 | **26** | Zera lado B |

### Definições no código

A imagem abaixo mostra os `#define` originais no sketch — útil se você for renumerar pinos:

![Definições de pinos no código](Ligacao%20dos%20Pinos.png)

---

## Instalação do software

### 1. Arduino IDE

Instale a versão **2.x** do Arduino IDE: <https://www.arduino.cc/en/software>.

### 2. Suporte ao ESP32

Em **File → Preferences → Additional Boards Manager URLs**, cole:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Em **Tools → Board → Boards Manager**, busque por `esp32` (Espressif Systems) e instale.

Depois selecione a placa correta (ex.: **ESP32 Dev Module**) e a porta COM em **Tools → Board** / **Tools → Port**.

### 3. Bibliotecas necessárias

Em **Sketch → Include Library → Manage Libraries**, instale:

- **MD_MAX72XX** (de *majicDesigns*)
- **MD_Parola** (instalada automaticamente como dependência; opcional aqui)

`WiFi.h`, `WebServer.h` e `SPI.h` já vêm com o core do ESP32.

### 4. Compilar e gravar

1. Abra `Placar_Eletronico_Wi-Fi/Placar_Eletronico_Wi-Fi.ino`.
2. Conecte o ESP32 via USB.
3. Clique em **Upload** (seta para a direita). Em algumas placas é preciso segurar `BOOT` durante o início do upload.
4. Abra o **Serial Monitor** a 115200 baud para ver mensagens de status (placar atual, IP do AP, etc.).

---

## Como usar

### Botões físicos

Aperte `UP1` ou `UP2` para somar pontos; `DW1` ou `DW2` para zerar o lado correspondente.

### Interface web

1. No celular, abra a lista de Wi-Fi e conecte-se à rede:
   - **SSID:** `PLACAR_WIFI`
   - **Senha:** `12345678`
2. Abra o navegador em <http://192.168.4.1>.
3. Use os botões: `A +`, `A RESET`, `B +`, `B RESET`, `RESET GERAL`.

A página se atualiza sozinha a cada 2 segundos.

---

## Personalização

| O que mudar | Onde no código |
|---|---|
| SSID / senha do AP | `ap_ssid`, `ap_password` no topo do `.ino` |
| Pontuação máxima (atual: 99) | Função `sobeA()` / `sobeB()` (`if (contX < 99)`) |
| Brilho dos displays (0–15) | `dspA.control(MD_MAX72XX::INTENSITY, 3)` no `setup()` |
| Tempo de debounce dos botões | Constante `debounceMs` (padrão 120 ms) |
| Pinos GPIO | Bloco de `#define` no início do arquivo |

---

## Estrutura do repositório

```
.
├── Placar_Eletronico_Wi-Fi/
│   └── Placar_Eletronico_Wi-Fi.ino   # Sketch principal
├── Ligacao dos Pinos.png              # Referência: #define dos pinos
├── Pinos ESP32.jpeg                   # Referência: pinout do ESP32 DevKit
├── New Scene.3mf                      # Modelo 3D (caixa/case)
├── Screenshot 2026-04-07 082105.png   # Captura da interface web
├── Screenshot 2026-04-07 082421.png   # Captura da interface web
├── .gitignore
└── README.md
```

---

## Solução de problemas

- **Display aceso só de um lado** — verifique alimentação e ordem `DIN → DOUT` entre os dois módulos do lado afetado.
- **Dígito aparece girado errado** — a função `desenhaDigitoRot90` aplica rotação de 90°. Se sua matriz estiver montada de cabeça para baixo, troque `7 - col` / `7 - row` ou use `MD_MAX72XX::PAROLA_HW` em vez de `FC16_HW`.
- **Botão não responde** — confira se está ligado entre o GPIO e o **GND** (não 3.3 V); o código usa pull-up interno.
- **Não vejo a rede `PLACAR_WIFI`** — abra o Serial Monitor a 115200 e procure por `Wi-Fi direto ativado`. Se não aparecer, reflashe o sketch.
