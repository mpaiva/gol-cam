# DFR1154 — DFRobot ESP32-S3 AI Camera Module Research Report

**Date:** 2026-03-12
**Sources:** DFRobot product page (product-2899), extracted spec data, GitHub repo, product metadata

---

## 1. Product Overview

**Full Name:** ESP32-S3 AI Camera Module (Edge Image Recognition, Night Vision, ChatGPT Voice Interaction)
**SKU:** DFR1154
**Price:** $18.90 USD
**Weight:** 0.027 kg
**Product URL:** https://www.dfrobot.com/product-2899.html
**Wiki URL:** https://wiki.dfrobot.com/SKU_DFR1154_ESP32_S3_AI_CAM
**Examples Repo:** https://github.com/DFRobot/DFR1154_Examples
**OpenAI SDK Repo:** https://github.com/DFRobot/openai-realtime-embedded-sdk
**Video:** https://dfimg.dfrobot.com/enshop/20250331/dfr1154-en.mp4

The ESP32-S3 AI Camera is an intelligent camera module built around the ESP32-S3 chip, designed for efficient video processing, edge AI, and voice interaction. It features a wide-angle infrared camera, onboard microphone, and speaker.

---

## 2. Technical Specifications

### Basic Parameters
| Parameter | Value |
|---|---|
| Operating Voltage | 3.3V |
| Type-C Input Voltage | 5V DC |
| VIN Input Voltage | 3.7–15V DC |
| Operating Temperature | -10°C to 60°C |
| Module Size | 42mm x 42mm |

### Processor
| Parameter | Value |
|---|---|
| Chip | ESP32-S3 (N16R8 variant) |
| Architecture | Xtensa LX7 dual-core |
| Clock Speed | Up to 240 MHz |
| Flash | 16MB |
| PSRAM | 8MB |
| WiFi | 2.4 GHz 802.11 b/g/n |
| Bluetooth | BLE 5.0 |

### Camera
| Parameter | Value |
|---|---|
| Sensor Model | OV3660 |
| Pixels | 2 Megapixels |
| Field of View | 160° wide-angle |
| Type | Infrared-capable (night vision) |
| IR Illumination | Yes (controlled via IO47) |

### Audio
| Parameter | Value |
|---|---|
| Microphone | I2S PDM microphone (onboard) |
| Speaker Amplifier | MAX98357 I2S amplifier chip |
| Speaker Interface | MX1.25-2P connector |

### Onboard Components
| Component | Description |
|---|---|
| OV3660 | 160° wide-angle infrared camera |
| IR LEDs | Infrared illumination (IO47) |
| MIC | I2S PDM microphone |
| MAX98357 | I2S amplifier chip |
| HM6245 | Power management chip |
| Light Sensor | Ambient light detection |
| Flash | 16MB |

### Interfaces
| Interface | Description |
|---|---|
| USB Type-C | Power + code uploading |
| VIN | 3.7–15V DC input |
| Gravity connector | 4-pin I2C/UART (3.3V output on V1.1) |
| SPK | MX1.25-2P speaker interface |
| SD/TF Card | MicroSD card slot |

### What's in the Box
- ESP32-S3 AI CAM development board (with camera) x1
- Speaker x1
- Gravity-4P I2C/UART sensor connection cable x1

---

## 3. Software & AI Capabilities

### Supported AI Frameworks
- **Edge Impulse** — On-device image recognition and classification
- **YOLOv5** — Object detection (likely running inference on device or via server)
- **OpenCV** — Contour detection, image processing
- **ChatGPT/OpenAI** — Voice-controlled command execution via cloud

### Development Frameworks
- **Arduino IDE** — Supported (ESP32 board manager)
- **ESP-IDF** — Native Espressif framework
- Likely PlatformIO support as well

### Tutorials Available (from DFRobot)
- **Basic:** Camera setup, video transmission, audio recording
- **Advanced:** Image recognition, object classification, OpenCV contour detection
- **Example Code:** Integration with OpenAI for voice/image recognition, custom model training with Edge Impulse

### DFRobot GitHub Repositories

#### DFR1154_Examples (Primary — 14 examples!)
All examples include pre-compiled `.bin` files and Arduino source code.

**Basic Examples (5.x):**
- **5.1 Obtain ambient light data** — Uses `DFRobot_LTR308` library for the onboard light sensor
- **5.2 Recording & Playback** — Records 5s audio via I2S PDM mic, plays back via MAX98357
- **5.3 CameraWebServer** — WiFi camera streaming with web interface (the foundational example)
- **5.4 USBWebCamera** — Use the board as a USB webcam (with Python receiver script)

**Advanced Examples (6.x):**
- **6.1 OpenCV** — Python scripts for contour detection (both WiFi stream and USB modes)
- **6.2 YOLO** — YOLOv5 object detection via Python (runs on computer, streams from board)
- **6.3 Edge Impulse** — On-device person detection using FOMO model
- **6.4 Train the Edge Impulse model** — Custom model training workflow with data capture
- **6.5 Connect to HomeAssistant** — Home automation integration
- **6.6 Xiaozhi** — Chinese AI assistant integration
- **6.7 OpenAI RTC** — Real-time OpenAI voice interaction
- **6.8 OpenAI image recognition** — Image Q&A with OpenAI API
- **6.9 Timed camera** — Periodic photo capture
- **6.10 PlayOnlineMusic** — Stream MP3/AAC audio via WiFi to the speaker

#### openai-realtime-embedded-sdk
SDK for real-time OpenAI voice/vision integration on ESP32-S3.

---

## 4. Complete GPIO Pin Mapping (from source code)

### Camera Pins (OV3660)
```
PWDN_GPIO_NUM    = -1  (not used)
RESET_GPIO_NUM   = -1  (not used)
XCLK_GPIO_NUM    = 5
SIOD_GPIO_NUM    = 8   (SCCB/I2C SDA)
SIOC_GPIO_NUM    = 9   (SCCB/I2C SCL)
Y9_GPIO_NUM      = 4   (D7)
Y8_GPIO_NUM      = 6   (D6)
Y7_GPIO_NUM      = 7   (D5)
Y6_GPIO_NUM      = 14  (D4)
Y5_GPIO_NUM      = 17  (D3)
Y4_GPIO_NUM      = 21  (D2)
Y3_GPIO_NUM      = 18  (D1)
Y2_GPIO_NUM      = 16  (D0)
VSYNC_GPIO_NUM   = 1
HREF_GPIO_NUM    = 2
PCLK_GPIO_NUM    = 15
```

### Audio Pins
```
I2S PDM Microphone:
  CLOCK_PIN  = GPIO 38
  DATA_PIN   = GPIO 39

MAX98357 Speaker Amplifier (I2S):
  I2S_BCLK   = GPIO 45
  I2S_LRC    = GPIO 46
  I2S_DOUT   = GPIO 42
```

### Other Pins
```
LED/IR         = GPIO 47
Light Sensor   = I2C (shared bus with camera SIOD/SIOC on GPIO 8/9)
SD Card (SPI):
  SD_CS      = GPIO 10
  SPI_MOSI   = GPIO 11
  SPI_SCK    = GPIO 12
  SPI_MISO   = GPIO 13
Status LED     = GPIO 3 (used during recording)
```

### Partition Table
```
nvs       data  nvs       0x9000   0x5000   (20KB)
otadata   data  ota       0xe000   0x2000   (8KB)
app0      app   ota_0     0x10000  0x3c0000 (3.75MB)
fr        data            0x3d0000 0x20000  (128KB)
coredump  data  coredump  0x3f0000 0x10000  (64KB)
```

---

## 5. Key Code Patterns (from examples)

### Camera Initialization Pattern
```cpp
#include "esp_camera.h"
camera_config_t config;
config.pixel_format = PIXFORMAT_JPEG;  // or PIXFORMAT_RGB565 for processing
config.frame_size = FRAMESIZE_QVGA;    // 320x240 — good balance of speed/detail
config.fb_location = CAMERA_FB_IN_PSRAM;
config.fb_count = 2;
config.grab_mode = CAMERA_GRAB_LATEST;
config.xclk_freq_hz = 20000000;

// OV3660 needs these adjustments:
sensor_t *s = esp_camera_sensor_get();
if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
}
```

### Audio Recording & Playback Pattern
```cpp
#include "ESP_I2S.h"
I2SClass i2s;   // mic
I2SClass i2s1;  // speaker

// Mic setup
i2s.setPinsPdmRx(GPIO_NUM_38, GPIO_NUM_39);
i2s.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);

// Speaker setup
i2s1.setPins(45, 46, 42);
i2s1.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);

// Record and play
uint8_t *wav_buffer;
size_t wav_size;
wav_buffer = i2s.recordWAV(5, &wav_size);  // 5 seconds
i2s1.playWAV(wav_buffer, wav_size);
```

### Online Audio Playback Pattern (for goal sounds from SD/URL)
```cpp
#include "Audio.h"
Audio audio;
audio.setPinout(45, 46, 42);  // BCLK, LRC, DOUT
audio.setVolume(15);  // 0-21
audio.connecttoFS(SD, "goal.wav");       // from SD card
// or: audio.connecttohost("http://url/goal.mp3");  // from URL
```

### Edge Impulse Inference Pattern
```cpp
#include <Model_inferencing.h>
// Capture at 240x240 for Edge Impulse
config.frame_size = FRAMESIZE_240X240;
// Allocate buffer: 240 * 240 * 3 bytes = 172KB (fits in 8MB PSRAM)
snapshot_buf = (uint8_t*)malloc(240 * 240 * 3);
// Run classifier
ei_impulse_result_t result = { 0 };
run_classifier(&signal, &result, false);
// result.timing.dsp, result.timing.classification — check latency
```

### Important Notes from Examples
- **WiFi credentials** are stored in NVS (Preferences) — persistent across reboots
- **Light sensor init order matters**: initialize camera BEFORE LTR308, otherwise reads return 0
- **OV3660 quirk**: image is flipped vertically by default, needs `set_vflip(s, 1)`
- **YOLO and OpenCV run on the computer**, not on the ESP32 — the board streams JPEG frames via WiFi, Python scripts on the computer do the processing
- **Edge Impulse runs on-device** — true edge AI inference

---

## 6. Relevance to gol-cam Project

### Strengths for Ball Detection
1. **OV3660 camera** — 2MP, 160° wide angle captures the entire goal area easily
2. **ESP32-S3 N16R8** — 16MB Flash + 8MB PSRAM, significantly more memory than basic ESP32-CAM (which has 4MB flash, 4MB PSRAM)
3. **Dual-core 240MHz** — Enough processing power for frame differencing or simple blob detection
4. **Edge Impulse support** — Could train a custom model to detect the ball
5. **IR camera + IR LEDs** — Works in any lighting condition, including poor/variable indoor lighting
6. **Small form factor** — 42mm x 42mm is compact enough to mount on/near a 15cm goal
7. **WiFi + BLE** — Can send goal events to a phone app or central scoreboard
8. **Onboard speaker** — Can play a goal celebration sound directly!
9. **SD card slot** — Can log events or store audio files
10. **$18.90** — Very affordable

### Potential Concerns
1. **OV3660 vs OV2640** — OV3660 is higher resolution but may have lower frame rate at full res. Need to test at lower resolutions (QVGA 320x240 should be fast enough)
2. **160° wide angle** — May introduce barrel distortion. For a small goal area at close range, this could actually be beneficial
3. **4mm ball detection** — At 320x240 resolution, a 4mm ball may only be a few pixels. Need to test minimum detectable size
4. **Processing latency** — Frame differencing should be fast, but Edge Impulse inference may add latency
5. **Power** — Needs 5V USB-C or 3.7–15V on VIN. Battery operation possible but camera is power-hungry

### Recommended Detection Approaches (ranked by simplicity)

1. **Frame differencing** — Compare consecutive frames in the goal region. Simplest, fastest, works on-device. Detect change = ball entered.
2. **Color-based detection** — If the ball is a distinct color, use HSV color filtering. Works well on ESP32-S3 with enough RAM.
3. **Edge Impulse custom model** — Train a small classifier: "ball in goal" vs "no ball". Most accurate but requires training data collection.
4. **OpenCV contour detection** — Detect small circular objects. Possible with the available tutorials from DFRobot.

---

## 5. Community Projects & Reviews

### YouTube Videos Referenced on Product Page
- "Dfrobot ESP32 S3 AI Camera Module | Tutorial | Best Camera Module?"
- "ESP32 AI Camera Review"
- "Vision Chatbot with DFRobot ESP32-S3 AI Camera and OpenAI"
- "Enclosure for the DFRobot ESP32-S3 AI Camera"

### User Reviews (from product page)
- **5/5 stars** — "An amazing all-in-one AI camera module! The ESP32-S3 AI Camera is compact yet extremely powerful for edge computing projects. The image recognition and face detection are fast and reliable, even under low light thanks to the built-in night-vision LEDs."
- **4/5 stars** — "The ESP32-S3 AI Camera Module is a powerful, flexible board for builders and makers who want vision, AI recognition, voice interaction, and wireless connectivity all in one."

### Typical Use Cases from Community
- Electronic peepholes / door cameras
- Baby monitors
- License plate recognition
- Home security / surveillance
- Smart assistants with voice interaction
- Object detection and classification
- AI-controlled surveillance

---

## 6. ESP32-S3 vs Classic ESP32-CAM Comparison

| Feature | ESP32-CAM (AI-Thinker) | DFR1154 (ESP32-S3 AI CAM) |
|---|---|---|
| Chip | ESP32 (single-core effective) | ESP32-S3 (dual-core LX7) |
| Clock | 240 MHz | 240 MHz |
| Flash | 4MB | 16MB |
| PSRAM | 4MB | 8MB |
| Camera | OV2640 (2MP, normal) | OV3660 (2MP, 160° wide, IR) |
| Night Vision | No | Yes (IR LEDs) |
| Microphone | No | Yes (I2S PDM) |
| Speaker | No | Yes (MAX98357 amp) |
| USB | No (needs FTDI adapter) | USB-C built-in |
| SD Card | Yes | Yes |
| AI/ML | Basic | Edge Impulse, YOLO, OpenCV |
| BLE | 4.2 | 5.0 |
| Price | ~$5-8 | $18.90 |
| Size | 27x40mm | 42x42mm |

**Verdict:** The DFR1154 is significantly more capable than the classic ESP32-CAM. The extra memory (16MB flash + 8MB PSRAM), USB-C, IR night vision, and built-in audio make it a much better choice for our project. The built-in speaker alone justifies the price difference — we can play goal sounds directly from the board!

---

## 7. Development Setup (Expected)

### Arduino IDE Setup
1. Install Arduino IDE 2.x
2. Add ESP32 board manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Install "esp32" board package by Espressif
4. Select board: likely "ESP32S3 Dev Module" or a DFRobot-specific board
5. Configure: Flash 16MB, PSRAM Enabled (OPI), USB Mode

### Key Libraries (Expected)
- `esp_camera.h` — Camera driver
- `WiFi.h` — WiFi connectivity
- `ESP32Servo.h` — If using servos
- `driver/i2s.h` — For audio (mic + speaker)
- Edge Impulse SDK — For ML inference

### Pin Mapping (Partial, from product page)
- IO47 — IR LED control
- Camera pins — Standard ESP32-S3 camera interface (likely SCCB/I2C for config, parallel for data)
- I2S — Microphone and speaker (MAX98357)
- Gravity — I2C/UART on dedicated connector

---

## 8. Recommendations for gol-cam

### Phase 1: Basic Setup
1. Get the board, connect via USB-C
2. Flash a basic camera example (CameraWebServer)
3. Verify video stream works
4. Test frame rate at different resolutions (QVGA, VGA)

### Phase 2: Ball Detection Prototype
1. Start with **frame differencing** — simplest approach
2. Define a Region of Interest (ROI) covering the goal opening
3. Compare frames: significant pixel change in ROI = goal detected
4. Test with both 4mm and 6mm balls

### Phase 3: Goal Celebration
1. Store a goal sound (WAV/MP3) on the SD card or in flash
2. Play via the MAX98357 amplifier + speaker on goal detection
3. Add LED flash or use the IR LEDs as visual indicator

### Phase 4: Score Tracking
1. Send goal events via WiFi/BLE to a phone app or OLED display
2. Or use an attached OLED via the Gravity I2C connector

---

## 11. Open Questions (Resolved & Remaining)

### Resolved
- **Pin mapping** — Complete GPIO mapping extracted from source code (see Section 4)
- **Development setup** — Arduino IDE with ESP32 board package, examples repo has pre-compiled bins
- **Audio playback** — Confirmed: can play WAV from RAM, SD card, or stream from URL
- **YOLO/OpenCV** — Clarified: these run on computer, not on-device. Board streams frames.
- **Edge Impulse** — Confirmed: runs on-device with FOMO model at 240x240

### Remaining
- What resolution/frame rate combinations work well for real-time on-device processing?
- Can the OV3660 detect a 4mm ball at the expected camera distance (~5-10cm from goal)?
- What's the power consumption with camera active? Battery life estimate?
- Is there a 3D-printable mount or enclosure available for the 42x42mm board?
- What's the best approach: on-device frame differencing vs streaming to phone for processing?
