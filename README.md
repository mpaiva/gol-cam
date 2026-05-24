# gol-cam

A complete electronic system for **futebol de botao** (button soccer): a camera that detects goals by computer vision, an LED scoreboard that displays the score, and a 3D-printed frame that puts it all together. Built for the **Dadinho** variant — the small 4-6 mm dice used as the ball.

## System overview

Three coordinated components in one repository:

| Component | Hardware | Role |
|---|---|---|
| **Camera** | DFRobot DFR1154 (ESP32-S3 + OV3660) | Detects goals via edge-based vision; serves the web dashboard and MJPEG stream |
| **Placar** | ESP32 DevKit V1 + 4× MAX7219 8×8 LED matrices + 4 buttons | Shows the score on physical LED digits; receives goal pushes from the camera and supports manual override |
| **Hardware** | 3D-printed enclosures (`hardware/`) | Trave (goal frame), camera box, scoreboard case, accessories |

**Network topology:**

```
                  ┌──────────────┐
                  │  Smartphone  │
                  │  / tablet    │
                  └───────┬──────┘
                          │ HTTP dashboard (browser polls)
                          │
       ┌──────────────────┼──────────────────┐
       │                  │                  │
       ▼                  ▼                  ▼
┌────────────┐     ┌────────────┐     ┌────────────┐
│ Camera A   │     │ Camera B   │     │ Placar     │
│ DFR1154    │     │ DFR1154    │     │ ESP32 +    │
│ side=A     │     │ side=B     │     │ MAX7219    │
└──────┬─────┘     └──────┬─────┘     └─────▲──────┘
       │                  │                 │
       └──────────────────┴─────────────────┘
              POST /goal {"side":"A|B"}   on detection
              (placar polling fallback @500 ms via /status)
```

For a one-camera setup, omit Camera B and the scoreboard's `PEER_IP`. The placar can also operate **standalone** — if the WiFi connection fails it falls back to an AP `PLACAR_WIFI` (192.168.4.1), and the 4 physical buttons remain functional.

## How detection works

A DFR1154 (ESP32-S3 + OV3660 wide-angle camera) watches the goal area. You calibrate by placing the dadinho in view, and the system learns its edge signature. During play, it detects the cube entering the goal, captures a snapshot, plays a celebration sound through the I2S speaker, pushes a goal event to the placar, and keeps score — all over WiFi.

Features:
- Live MJPEG camera stream
- One-click calibration with visual feedback
- Automatic goal detection with 10-second cooldown
- Goal photo log with timestamps
- VAR (Video Assistant Referee) review system
- Game controls: pause, resume, reset, end game
- Physical LED scoreboard with manual override buttons
- Standalone scoreboard operation when camera is offline

## Futebol de Botao and the Dadinho in Rio de Janeiro

**Futebol de botao** (button football) is a tabletop sport born in Brazil in the 1930s, where players flick round discs to move a smaller piece — the ball — across a miniature pitch. The game emerged from the creativity of Brazilian kids who fashioned buttons, bottle caps, and coins into teams, playing on dining tables and wooden boards across the country.

### Origins

The sport's roots trace to the 1930s and 1940s, when Geraldo Decourt of Sao Paulo published the first formal rules in 1930. What started as a children's pastime quickly grew into an organized competitive activity. By the mid-20th century, clubs and federations had formed, and regional rule variations flourished.

### Rio de Janeiro and the Regra Carioca

Rio de Janeiro became one of the sport's most important centers, developing its own distinctive style known as the **Regra Carioca** (Carioca Rules) or **3 Toques** (3 Touches). Under these rules, a player gets up to three flicks per turn to advance the ball, creating a faster, more tactical game compared to other regional variants.

### The Dadinho

The **Dadinho** (little dice) variant represents a distinct branch of button football. Instead of traditional round discs, players use small dice — typically 6mm yellow cubes — as the playing pieces. The name comes from "dado" (dice) with the diminutive "-inho" suffix.

Dadinho was officially recognized by the Confederacao Brasileira de Futebol de Mesa (CBFM) in 2010, giving it the same institutional standing as the traditional disc-based modalities. The variant has its own dedicated competitive circuit, with the Brazilian Dadinho Championship reaching its 16th edition in 2025.

## Hardware

### Bill of Materials (BOM)

**Camera board (×1 single or ×2 match mode):**
- 1× DFRobot DFR1154 (ESP32-S3 + OV3660, 16 MB flash, 8 MB PSRAM)
- 1× MAX98357 I2S amplifier + small speaker (already included on the DFR1154 carrier)
- USB-C cable for power and programming

**Placar board (×1, optional):**
- 1× ESP32 DevKit V1 (any ESP32 with ≥8 free GPIOs)
- 4× MAX7219 8×8 LED matrix modules (FC-16 chain), 2 per side
- 4× push buttons (momentary, normally open)
- Jumpers and 5 V supply (USB to the DevKit works)

**Mechanical:**
- 3D-printed parts from `hardware/` (PLA, see `hardware/README.md` for slicer settings)

### Placar wiring (ESP32 DevKit V1)

**Display A** (left score, 2 chained modules, DOUT → DIN):

| MAX7219 | GPIO | Macro (`include/pins_placar.h`) |
|---|---|---|
| DIN | 23 | `PLACAR_DIN_A` |
| CLK | 18 | `PLACAR_CLK_A` |
| CS  | 5  | `PLACAR_CS_A`  |
| VCC | 5V (VIN) | — |
| GND | GND | — |

**Display B** (right score, 2 chained modules):

| MAX7219 | GPIO | Macro |
|---|---|---|
| DIN | 19 | `PLACAR_DIN_B` |
| CLK | 21 | `PLACAR_CLK_B` |
| CS  | 22 | `PLACAR_CS_B`  |

**Buttons** (each from GPIO to GND, `INPUT_PULLUP` — no external resistors):

| Button | GPIO | Macro | Action |
|---|---|---|---|
| UP A | 32 | `PLACAR_BTN_UP_A` | Increment side A |
| DW A | 33 | `PLACAR_BTN_DW_A` | Reset side A |
| UP B | 25 | `PLACAR_BTN_UP_B` | Increment side B |
| DW B | 26 | `PLACAR_BTN_DW_B` | Reset side B |

### Camera pinout

See `include/pins.h` — preconfigured for the DFR1154 carrier. No wiring needed.

### 3D-printed parts

See `hardware/README.md` for the parts catalog (trave V4, camera box V5.2, placar case, speaker mount, cooler grille) with recommendations for slicing.

## Getting Started

### Prerequisites

- [Git](https://git-scm.com/downloads)
- [Python 3](https://www.python.org/downloads/) (check "Add to PATH" on Windows installer)
- USB-C cable for the DFR1154; USB cable for the ESP32 DevKit
- **Windows only:** [CP210x USB driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (may be needed for serial communication)

### Step 1 — Clone the repo

```bash
git clone https://github.com/mpaiva/gol-cam.git
cd gol-cam
```

### Step 2 — Create a Python virtual environment

**Mac (Terminal):**
```bash
python3 -m venv .venv
source .venv/bin/activate
pip install platformio
```

**Windows (PowerShell):**
```powershell
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install platformio
```

> If PowerShell blocks the script, run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` first.

### Step 3 — Configure `.env`

Create `.env` in the project root (git-ignored):

```ini
# Required — same WiFi for camera and placar
WIFI_SSID=your-network-name
WIFI_PASSWORD=your-password

# Optional — static IPs help in match mode
WIFI_STATIC_IP=192.168.0.100
WIFI_GATEWAY=192.168.0.1
WIFI_SUBNET=255.255.255.0

# Optional — role tags for match mode
BOARD_ROLE=goal_a            # or goal_b, single, scoreboard
PEER_IP=192.168.0.101        # IP of the other camera (match mode)
SCOREBOARD_IP=192.168.0.110  # IP of the placar (camera pushes goals here)
CAMERA_IP=192.168.0.100      # IP of the camera (placar polls this as fallback)
```

`load_env.py` reads these and injects them as `-D` build defines, so they are baked into the firmware at compile time. Different boards therefore need different `.env` values — flash one, change `.env`, flash the next.

### Step 4 — Build & flash

Two PlatformIO environments live in the same project:

```bash
# Camera (default — same as `pio run`)
pio run -e dfr1154 -t upload

# Placar
pio run -e placar -t upload
```

The default environment is `dfr1154`, so plain `pio run` builds the camera. Flash each board one at a time, adjusting `.env` between flashes if you want different `BOARD_ROLE` or static IP values.

### Step 5 — Find the IP and open the dashboard

```bash
pio device monitor   # baud 115200, look for "Dashboard: http://..." on the camera
```

Open the camera's IP in a browser. You'll see the mode selector:
- **Training** — single camera, calibration and practice
- **Match** — two cameras + optional placar; configure IPs on first load (saved to `localStorage`)

The placar serves its own simple UI at its IP (manual increment / reset, plus current connection status).

## Operation

### Standard match flow

1. **Calibrate each camera** — place the dadinho in front of the goal, click "Calibrate"
2. **Start the game** — match dashboard shows live feeds and unified scoreboard
3. **Play** — the camera detects goals, plays a celebration sound, pushes the event to the placar, increments the LED scoreboard
4. **VAR review** — click any goal entry to confirm or annul (annulled goals deduct from the right side)
5. **Pause / resume / reset / end** — controls reach all boards simultaneously

### Placar fallback (no camera or camera offline)

If the camera is unreachable, the placar still works:
- WiFi STA failed → AP mode `PLACAR_WIFI` / `12345678`, open `http://192.168.4.1`
- Physical buttons always work (UP / RESET per side)
- Manual web UI mirrors the buttons

## Repository layout

```
gol-cam/
├── platformio.ini              # Dual environments: dfr1154 + placar
├── partitions.csv              # 16 MB flash layout for the camera
├── load_env.py                 # .env → -D build defines
├── .env                        # WiFi + role/IP overrides (git-ignored)
├── include/
│   ├── camera.h, pins.h        # Camera (DFR1154)
│   ├── frame_store.h, goal_detector.h, gol_sound*.h
│   ├── pins_placar.h           # Placar GPIOs
│   ├── placar_display.h        # MAX7219 driver
│   ├── placar_buttons.h        # debounced button reader
│   └── placar_web.h            # placar HTTP server contract
├── src/
│   ├── main.cpp                # Camera entry + detection loop + goal push
│   ├── web_stream.cpp          # Camera HTTP server (esp_http_server)
│   ├── mode_select.h, training_dashboard.h, match_dashboard.h
│   ├── main_placar.cpp         # Placar entry + WiFi/AP + camera polling
│   ├── placar_display.cpp      # MAX7219 rendering
│   ├── placar_buttons.cpp      # button debounce
│   └── placar_web.cpp          # placar WebServer + /goal /sync /status
├── hardware/                   # 3D-printed enclosures
├── .about/                     # Value proposition, feature requests
├── .plans/                     # Implementation plans, session notes
└── .reports/                   # Research, UX audit, screenshots
```

## License

MIT
