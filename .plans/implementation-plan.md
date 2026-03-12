# Implementation Plan: gol-cam v1

## Goal
Detect when a 4-6mm ball enters a button soccer goal (15cm x 4cm) using the DFR1154 ESP32-S3 AI Camera, and play a goal celebration sound.

## Phase 1: Hello World — Camera Stream ✅
**Status:** COMPLETE — PlatformIO project scaffolded, MJPEG stream at http://192.168.40.55
- [x] Set up PlatformIO with ESP32-S3 board (esp32-s3-devkitc-1)
- [x] Flash camera streaming firmware
- [x] Verified video stream in browser at QVGA (320x240)

## Phase 2: Audio Test
**Objective:** Verify speaker plays audio.
- [ ] Flash the Recording & Playback example (5.2)
- [ ] Test playing a WAV file from SD card (using Audio.h library from 6.10)
- [ ] Prepare a "GOOOL!" sound file (WAV, 16-bit, 16kHz mono)
- [ ] Copy sound file to MicroSD card

## Phase 3: Ball Detection — Frame Differencing ✅
**Status:** COMPLETE — Pixel-level grayscale detection working at 12fps
- [x] Capture frames in native PIXFORMAT_GRAYSCALE at QVGA
- [x] Define ROI (center 80% of frame)
- [x] Pixel-level frame differencing with per-pixel threshold
- [x] frame2jpg() conversion for web streaming from grayscale
- [x] Goal detection with 3s cooldown, stable-frame requirement
- [x] Web dashboard with live stream, score counter, GOOOL! flash
- [x] Confirmed detection via LED self-test (5-23% change on motion)
- [ ] Tune threshold with real ball at actual goal (PIXEL_THRESHOLD=15, CHANGE_THRESHOLD=3%)

## Phase 4: Goal Celebration
**Objective:** When goal detected, celebrate!
- [ ] Play "GOOOL!" WAV via speaker on detection
- [ ] Flash IR LED as visual indicator
- [ ] Print goal event to Serial for debugging

## Phase 5: Polish & Mount
**Objective:** Make it game-ready.
- [ ] Design/print a mount for the 42x42mm board on the goal frame
- [ ] Tune detection sensitivity for real game conditions
- [ ] Add score counter (optional — serial output or OLED)
- [ ] Test with both 4mm and 6mm balls
- [ ] Battery power test (USB power bank)

## Architecture
```
┌─────────────────────────────────────────┐
│              ESP32-S3 (DFR1154)         │
│                                         │
│  ┌──────────┐    ┌──────────────────┐   │
│  │ OV3660   │───→│ Frame Capture    │   │
│  │ Camera   │    │ (QVGA grayscale) │   │
│  └──────────┘    └────────┬─────────┘   │
│                           │             │
│                  ┌────────▼─────────┐   │
│                  │ Frame Differencing│   │
│                  │ (ROI comparison)  │   │
│                  └────────┬─────────┘   │
│                           │             │
│                  ┌────────▼─────────┐   │
│                  │ Goal Detector    │   │
│                  │ (threshold +     │   │
│                  │  cooldown)       │   │
│                  └────────┬─────────┘   │
│                           │             │
│              ┌────────────▼──────────┐  │
│              │ Goal Event Handler    │  │
│              │ - Play WAV (speaker)  │  │
│              │ - Flash IR LED        │  │
│              │ - Serial log          │  │
│              └───────────────────────┘  │
└─────────────────────────────────────────┘
```

## Key Parameters to Tune
| Parameter | Starting Value | Notes |
|---|---|---|
| Resolution | QVGA (320x240) | Balance of detail vs speed |
| Pixel format | GRAYSCALE | Simpler math, faster processing |
| ROI | Center 60% of frame | Adjust to actual goal position |
| Pixel threshold | 15 brightness units | Per-pixel change to count as "changed" |
| Change threshold | 3% of ROI pixels | Tune with real ball |
| Cooldown | 3 seconds | Prevent double-counting |
| Frame compare interval | Every frame | May skip frames if too slow |
