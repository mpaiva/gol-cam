# Plan: Initial Research & Tech Stack Decision

## Goal
Determine the best hardware and software stack for gol-cam.

## Key Decisions Needed

### 1. Hardware Platform
Options:
- **ESP32-CAM** — Cheap (~$5), WiFi built-in, small form factor, limited processing power
- **Raspberry Pi Zero 2W + Camera Module** — More processing power, runs full Linux/Python, slightly larger
- **Arduino + external camera** — More complex integration

### 2. Detection Approach
Options:
- **Frame differencing** — Simple, detect motion/change in goal zone (lightweight)
- **Color detection** — Track ball by color (works if ball is a distinct color)
- **Edge/blob detection** — OpenCV-based, detect small circular objects
- **ML model** — Train a small model (overkill for this?)

### 3. Output/Feedback
Options:
- **Buzzer + LED** — Simple, self-contained
- **Bluetooth to phone app** — Score display on phone
- **Small OLED display** — Mounted scoreboard
- **Speaker with goal sound** — Fun audio feedback

## Next Steps
- [ ] Research ESP32-CAM vs Raspberry Pi Zero for this use case
- [ ] Prototype basic ball detection with OpenCV
- [ ] Determine power requirements (battery vs USB)
- [ ] Design mounting approach for the goal frame
