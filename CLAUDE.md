# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**gol-cam** — An ESP32-S3 camera system that detects goals in futebol de botao (button soccer) and triggers celebrations. Uses color-based calibration to detect a yellow dadinho (small dice) entering the goal, with a web dashboard served over WiFi.

## Hardware

**Board:** DFRobot ESP32-S3 AI Camera Module (SKU: DFR1154)
- ESP32-S3 N16R8: dual-core Xtensa LX7 @ 240MHz, 16MB Flash, 8MB PSRAM
- Camera: OV3660, 2MP, 160° wide-angle
- Audio: I2S PDM mic + MAX98357 amplifier + speaker
- IR LEDs on IO47, WiFi 2.4GHz, BLE 5.0
- Pin definitions: `include/pins.h`

## Build & Deploy

Uses **PlatformIO** (not Arduino IDE directly). Requires a `.env` file with WiFi credentials.

```bash
# Setup
python -m venv .venv && source .venv/bin/activate  # or .venv\Scripts\activate on Windows
pip install platformio

# Build
pio run

# Upload to board via USB-C
pio run -t upload

# Serial monitor (look for "Dashboard: http://...")
pio device monitor
```

WiFi credentials go in `.env` (git-ignored):
```
WIFI_SSID=your-network
WIFI_PASSWORD=your-password
```

Optional static IP and multi-board settings: `WIFI_STATIC_IP`, `WIFI_GATEWAY`, `WIFI_SUBNET`, `BOARD_ROLE`, `PEER_IP` — all read by `load_env.py` (PlatformIO pre-script that converts `.env` to `-D` build defines).

## Architecture

**Single-threaded main loop** (`src/main.cpp`) with cooperative state machine:
- `STATE_IDLE` → `STATE_CALIBRATING` → `STATE_IDLE` (calibration stores color)
- `STATE_IDLE` → `STATE_PLAYING` ↔ `STATE_PAUSED` → `STATE_IDLE`

**Detection pipeline** (in `loop()`): Captures RGB565 frames at QVGA (320×240). During calibration, finds the most distinct object vs. background edges. During play, counts pixels matching calibrated color within tolerances, applies size/density filters, triggers goal on dice-appears-after-absence pattern with 10s cooldown.

**Key modules:**
- `include/camera.h` — Camera init wrapper for OV3660
- `include/goal_detector.h` — GoalDetector struct (counters, FPS, thresholds)
- `include/frame_store.h` — Thread-safe JPEG frame buffer (PSRAM) for MJPEG streaming
- `include/pins.h` — All DFR1154 GPIO pin definitions
- `src/web_stream.cpp` — ESP HTTP server: MJPEG stream, REST endpoints, serves dashboard HTML
- `src/mode_select.h` — Landing page HTML (mode selection)
- `src/training_dashboard.h` — Training/calibration dashboard HTML (single camera)
- `src/match_dashboard.h` — Match mode dashboard HTML (two-camera unified scoreboard)

**Web UI** is embedded as C string literals in header files (`*_dashboard.h`, `mode_select.h`). The dashboards include i18n support (English/Portuguese) via inline JavaScript translation maps.

## Board config

- `platformio.ini` — Board: `esp32-s3-devkitc-1`, 16MB flash, OPI PSRAM, custom partition table
- `partitions.csv` — Custom partition layout for the 16MB flash
- `load_env.py` — PlatformIO pre-script that reads `.env` and injects as `CPPDEFINES`

## Project Tracking

- `.about/` — Value proposition, feature requests with requirements and acceptance criteria
- `.plans/` — Implementation plans and session notes
- `.reports/` — Research findings, UX audit, screenshots
