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
- 4× MAX7219 8×8 LED modules (FC-16 chain): DIN_A=23 CLK_A=18 CS_A=5, DIN_B=19 CLK_B=21 CS_B=22
- 4× push buttons (INPUT_PULLUP): UP_A=32 DW_A=33 UP_B=25 DW_B=26
- Pin definitions: `include/pins_placar.h`

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

# Optional — per-board role
BOARD_ROLE=goal_a               # goal_a | goal_b | single | scoreboard
PEER_IP=192.168.0.101           # other camera (match mode)
SCOREBOARD_IP=192.168.0.110     # placar IP (camera pushes here)
CAMERA_IP=192.168.0.100         # camera IP (placar polls here as fallback)
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
2. `pushGoalToScoreboard()` notifies another task that posts `POST http://${SCOREBOARD_IP}/goal` with `{"side":"A|B"}`. Best-effort; placar polling covers failures.

### Placar board

**Single-threaded main loop** (`src/main_placar.cpp`):
- `setup()` initialises display + buttons, tries WiFi STA against the same `.env`, falls back to AP `PLACAR_WIFI` / `12345678` if STA fails after 15 s, starts the WebServer.
- `loop()` polls buttons (debounced), serves HTTP, and every 500 ms polls `http://${CAMERA_IP}/status` looking for `"goals":N` increments — used as a fallback when the camera's push fails. Baseline-on-first-poll avoids replaying historical goals.

### Cross-board sync protocol

| Direction | Endpoint | When |
|---|---|---|
| Camera → Placar | `POST /goal {"side":"A|B"}` | At goal detection (push, primary) |
| Placar → Camera | `GET /status` every 500 ms | Polling (fallback if push misses) |
| Browser → Placar | `GET /status`, `POST /sync {"a":N,"b":M}` | Match dashboard reads scoreboard state; `/sync` is an absolute setter |
| Browser → Camera | `GET /status`, `GET /stream`, REST control endpoints | Training and match dashboards |

The camera reports its `"side"` in `/status`, derived from `BOARD_ROLE` (`goal_b` → B, others → A). The placar honors that field when applying camera-side increments.

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
- `include/pins_placar.h` — DIN/CLK/CS + button pins + brightness
- `include/placar_display.h` + `src/placar_display.cpp` — `PlacarDisplay` (MAX7219 driver, rotated 90° digit font, `showA/showB(0..99)`)
- `include/placar_buttons.h` + `src/placar_buttons.cpp` — `PlacarButtons` (debounced edge reader, 120 ms)
- `include/placar_web.h` + `src/placar_web.cpp` — `WebServer` on :80 with `/`, `/a+`, `/b+`, `/az`, `/bz`, `/reset`, `/status`, `/sync`, `/goal`
- `src/main_placar.cpp` — entry point, WiFi STA/AP fallback, score state, camera polling

**Web UI** is embedded as C string literals in header files (`*_dashboard.h`, `mode_select.h`). Camera dashboards include i18n support (EN/PT) via inline JavaScript translation maps. The placar UI is Portuguese-only (single page, auto-refresh).

## Board config

- `platformio.ini` — Two environments:
  - `[env:dfr1154]` — `esp32-s3-devkitc-1`, 16 MB flash, OPI PSRAM, custom `partitions.csv`. Excludes `main_placar.cpp` and `placar_*.cpp` via `build_src_filter`.
  - `[env:placar]` — `esp32dev`, depends on `majicdesigns/MD_MAX72XX@^3.5.0`. Excludes `main.cpp` and `web_stream.cpp`.
- `partitions.csv` — Custom partition layout for the 16 MB flash on the DFR1154
- `load_env.py` — PlatformIO pre-script that reads `.env` and injects as `CPPDEFINES`. Whitelist: `WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_STATIC_IP`, `WIFI_GATEWAY`, `WIFI_SUBNET`, `BOARD_ROLE`, `PEER_IP`, `CAMERA_IP`, `SCOREBOARD_IP`.

## Project Tracking

- `.about/` — Value proposition, feature requests with requirements and acceptance criteria
- `.plans/` — Implementation plans (including `consolidation-plan.md`) and session notes
- `.reports/` — Research findings, UX audit, screenshots
- `hardware/` — 3D-printable parts catalog (see `hardware/README.md`)
