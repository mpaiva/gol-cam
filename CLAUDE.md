# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**gol-cam** — A mini camera-based goal detection system for button soccer ("futebol de botão"). A small camera attaches to a goal frame (15cm x 4cm) and uses computer vision to detect when a 4mm or 6mm ball enters the goal, triggering a scoring event.

## Status

Early stage — researching hardware platform and detection approach. No code yet.

## Architecture (Planned)

The system has three layers:
1. **Capture** — Camera module continuously captures the goal area
2. **Detection** — Image processing identifies ball presence in goal zone
3. **Output** — Goal event triggers feedback (sound, light, score update)

Hardware candidates: ESP32-CAM or Raspberry Pi Zero 2W.
Detection candidates: frame differencing, color tracking, or OpenCV blob detection.

## Project Tracking

- `.about/` — Value proposition, feature requests with requirements and acceptance criteria
- `.plans/` — Implementation plans and research
- `.reports/` — Research findings and reports
