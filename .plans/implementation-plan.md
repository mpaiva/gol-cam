# Implementation Plan: gol-cam v1

## Goal
Detect when a 4-6mm ball enters a button soccer goal (15cm x 4cm) using the DFR1154 ESP32-S3 AI Camera, and play a goal celebration sound.

## Phase 1: Hello World — Camera Stream
**Objective:** Verify the board works, camera streams, and dev environment is set up.
- [ ] Set up Arduino IDE with ESP32-S3 board package
- [ ] Flash the CameraWebServer example (5.3 from DFR1154_Examples)
- [ ] Verify video stream in browser
- [ ] Test different resolutions (QVGA 320x240, QQVGA 160x120)
- [ ] Confirm frame rate at each resolution

## Phase 2: Audio Test
**Objective:** Verify speaker plays audio.
- [ ] Flash the Recording & Playback example (5.2)
- [ ] Test playing a WAV file from SD card (using Audio.h library from 6.10)
- [ ] Prepare a "GOOOL!" sound file (WAV, 16-bit, 16kHz mono)
- [ ] Copy sound file to MicroSD card

## Phase 3: Ball Detection — Frame Differencing
**Objective:** Detect motion (ball entering) in a defined goal region.
- [ ] Capture frames in RGB565 or GRAYSCALE at QVGA
- [ ] Define a Region of Interest (ROI) covering the goal opening
- [ ] Implement frame differencing: compare current vs previous frame in ROI
- [ ] Calculate pixel change percentage in ROI
- [ ] When change exceeds threshold → trigger goal event
- [ ] Tune threshold to avoid false positives (player hand, shadows)
- [ ] Add cooldown period after goal detection (prevent double-counting)

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
| Change threshold | 15% of ROI pixels | Tune with real ball |
| Cooldown | 3 seconds | Prevent double-counting |
| Frame compare interval | Every frame | May skip frames if too slow |
