# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**gol-cam** — A two-board electronic system for futebol de botao (button soccer):

1. **Camera board** (DFR1154 / ESP32-S3) — detects goals via edge-based computer vision, plays celebration audio, serves training and match web dashboards.
2. **Placar board** (ESP32 DevKit + 4× MAX7219) — physical LED scoreboard, increments on camera push or manual button override.

Both firmwares live in this single PlatformIO project; `hardware/` holds the 3D-printable enclosures imported from the auxiliary `GOOOL Elatronico` project. The original `Placar Eletronico` project was consolidated into this repo on 2026-05-24 — see `.plans/consolidation-plan.md`.

## Hardware

**Camera — DFRobot ESP32-S3 AI Camera Module (SKU: DFR1154)**
- ESP32-S3 N16R8: dual-core Xtensa LX7 @ 240 MHz, 16 MB Flash, 8 MB PSRAM
- Camera: OV3660, 2 MP, 160° wide-angle
- Audio: I2S PDM mic + MAX98357 amplifier + speaker
- IR LEDs on IO47, WiFi 2.4 GHz, BLE 5.0
- Pin definitions: `include/pins.h`

**Placar — generic ESP32 DevKit V1**
- 4× MAX7219 8×8 LED modules (FC-16 chain): DIN1=23 CLK1=18 CS1=5, DIN2=19 CLK2=21 CS2=22
- 4× push buttons (INPUT_PULLUP): UP1=32 DW1=33 UP2=25 DW2=26
- Pin definitions inline at the top of `src_scoreboard/scoreboard.cpp`

## Build & Deploy

Uses **PlatformIO** (not Arduino IDE directly). Requires a `.env` file with WiFi credentials.

```bash
# Setup (once)
python -m venv .venv && source .venv/bin/activate  # or .venv\Scripts\activate on Windows
pip install platformio

# Build the camera (default env — same as `pio run`)
pio run -e dfr1154

# Build the placar
pio run -e placar

# Upload via USB
pio run -e dfr1154 -t upload   # plug in the DFR1154
pio run -e placar -t upload    # plug in the ESP32 DevKit

# Serial monitor (look for "Dashboard: http://...")
pio device monitor
```

`.env` schema (git-ignored):

```
# Required
WIFI_SSID=your-network
WIFI_PASSWORD=your-password

# Optional — same on every board for fixed IPs
WIFI_STATIC_IP=192.168.0.100
WIFI_GATEWAY=192.168.0.1
WIFI_SUBNET=255.255.255.0

# Optional — per-board role (cameras only; the placar firmware ignores these)
BOARD_ROLE=home                 # home → side B  |  away → side A  |  unset → side A
SCOREBOARD_SIDE=b               # explicit override (a|b), wins over BOARD_ROLE
PEER_IP=192.168.0.101           # other camera (match mode)
SCOREBOARD_IP=192.168.0.110     # placar IP (camera pushes here)
```

`load_env.py` reads `.env` and injects each whitelisted variable as a `-D` build define. Different boards therefore need different `.env` values at flash time.

## Architecture

### Camera board

**Single-threaded main loop** (`src/main.cpp`) with cooperative state machine:
- `STATE_IDLE` → `STATE_CALIBRATING` → `STATE_IDLE` (calibration stores edge signature)
- `STATE_IDLE` → `STATE_PLAYING` ↔ `STATE_PAUSED` → `STATE_IDLE`

**Detection pipeline** (in `loop()`): captures grayscale frames at QVGA (320×240). During calibration, finds the most distinct object vs. background edges (Sobel gradient). During play, counts edge pixels matching calibrated threshold within ROI, applies size/density filters, triggers goal on dadinho-appears-after-absence with 10 s cooldown.

**Side effects on goal:**
1. `playGoalSound()` notifies a persistent FreeRTOS task that installs the I2S driver, plays one of three audio clips (Brasil/Flamengo/Vasco), then uninstalls to keep the amp silent.
2. `pushGoalToScoreboard()` notifies another task that fires `GET http://${SCOREBOARD_IP}/goal?side=a|b`. Best-effort, fire-and-forget (no retry).

### Placar board

**Single-threaded main loop** (`src_scoreboard/scoreboard.cpp`):
- `setup()` initialises the MAX7219 chain + buttons, connects WiFi STA (with reduced TX power + modem-sleep so cheap power banks don't trip overcurrent), starts the `WebServer`.
- `loop()` polls buttons (debounced) and serves HTTP. The placar does **not** poll the cameras — score increments come from physical buttons, the `/goal?side=a|b` push from cameras, or the dashboard's `/a+` / `/b+` / `/api/reset` endpoints.

### Cross-board sync protocol

| Direction | Endpoint | When |
|---|---|---|
| Camera → Placar | `GET /goal?side=a\|b` | At goal detection (push, fire-and-forget) |
| Browser → Placar | `GET /status` (1 s poll from the placar's own HTML), `GET /a+`, `/b+`, `/az`, `/bz`, `/reset`, `/api/reset` | Scoreboard dashboard |
| Browser → Camera | `GET /status`, `GET /stream`, REST control endpoints | Training and match dashboards |

The camera reports its `"side"` in `/status`, resolved at boot from `SCOREBOARD_SIDE` (explicit override) then `BOARD_ROLE` (`home`→`B`, `away`→`A`, anything else→`A`). See `resolveScoreboardSide()` in `src/main.cpp`; `web_stream.cpp` reads the same global so `/status.side` always agrees with the push.

## Key modules

### Camera
- `include/camera.h` — OV3660 init wrapper
- `include/goal_detector.h` — GoalDetector struct (counters, FPS, thresholds)
- `include/frame_store.h` — Thread-safe JPEG frame buffer (PSRAM) for MJPEG streaming
- `include/pins.h` — DFR1154 GPIO definitions
- `src/main.cpp` — entry point, detection loop, audio + scoreboard push tasks
- `src/web_stream.cpp` — `esp_http_server`: MJPEG stream, REST endpoints, dashboard HTML
- `src/mode_select.h`, `src/training_dashboard.h`, `src/match_dashboard.h` — embedded HTML/JS

### Placar
- `src_scoreboard/scoreboard.cpp` — single-file firmware: MAX7219 driver (rotated 90° digit font, `sobeA/sobeB(0..99)`), debounced buttons, WiFi STA, `WebServer` on :80, dashboard HTML embedded as a `PROGMEM` string literal. Routes: `/`, `/a+`, `/b+`, `/az`, `/bz`, `/reset`, `/goal?side=a|b`, `/status`, `/api/reset`.

**Web UI** is embedded as C string literals (camera HTML/JS lives in header files `*_dashboard.h` and `mode_select.h`; placar HTML is a `PROGMEM` constant inside `scoreboard.cpp`). Camera dashboards include i18n support (EN/PT) via inline JavaScript translation maps. The placar UI is Portuguese-only, single page, polls `/status` from the browser every 1 s.

## Board config

- `platformio.ini` — Two environments:
  - `[env:dfr1154]` — `esp32-s3-devkitc-1`, 16 MB flash, OPI PSRAM, custom `partitions.csv`. Compiles `src/` only (excludes `../src_scoreboard/`).
  - `[env:placar]` — `esp32dev`, depends on `majicdesigns/MD_MAX72XX@^3.5.0`. Compiles `../src_scoreboard/` only (excludes `src/`).
- `partitions.csv` — Custom partition layout for the 16 MB flash on the DFR1154
- `load_env.py` — PlatformIO pre-script that reads `.env` and injects whitelisted variables as `CPPDEFINES`: `WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_STATIC_IP`, `WIFI_GATEWAY`, `WIFI_SUBNET`, `BOARD_ROLE`, `PEER_IP`, `CAMERA_IP`, `SCOREBOARD_IP`, `SCOREBOARD_SIDE`, `SCOREBOARD_STATIC_IP`, `SCOREBOARD_GATEWAY`, `SCOREBOARD_SUBNET`.

## Project Tracking

- `.about/` — Value proposition, feature requests with requirements and acceptance criteria
- `.plans/` — Implementation plans (including `consolidation-plan.md`) and session notes
- `.reports/` — Research findings, UX audit, screenshots
- `hardware/` — 3D-printable parts catalog (see `hardware/README.md`)
