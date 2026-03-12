# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**gol-cam** — A mini camera-based goal detection system for button soccer ("futebol de botão"). A small camera attaches to a goal frame (15cm x 4cm) and uses computer vision to detect when a 4mm or 6mm ball enters the goal, triggering a scoring event with sound.

## Hardware

**Board:** DFRobot ESP32-S3 AI Camera Module (SKU: DFR1154, $18.90)
- ESP32-S3 N16R8: dual-core Xtensa LX7 @ 240MHz, 16MB Flash, 8MB PSRAM
- Camera: OV3660, 2MP, 160° wide-angle, infrared night vision (IR LEDs on IO47)
- Audio: I2S PDM microphone + MAX98357 I2S amplifier + speaker
- Connectivity: WiFi 2.4GHz, BLE 5.0
- Interfaces: USB-C, Gravity I2C/UART, MicroSD slot, VIN (3.7-15V)
- Size: 42mm x 42mm
- Power: 3.3V operating, 5V via USB-C

## Detection Approach (Planned)

Primary: Frame differencing in a Region of Interest covering the goal opening.
Fallback: Color-based HSV filtering or Edge Impulse custom model.

## Development

Target framework: Arduino IDE with ESP32 board package by Espressif.
Board manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

## Status

Research phase — board not yet physically available. See `.reports/dfr1154-research.md` for full board research.

## Project Tracking

- `.about/` — Value proposition, feature requests with requirements and acceptance criteria
- `.plans/` — Implementation plans and research
- `.reports/` — Research findings and reports
