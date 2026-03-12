#pragma once

#include <Arduino.h>
#include "esp_camera.h"

// =============================================================
// Goal Detector — Frame Differencing in a Region of Interest
//
// Compares consecutive grayscale frames within a defined ROI.
// When pixel change exceeds a threshold, a goal is detected.
// A cooldown period prevents double-counting.
// =============================================================

class GoalDetector {
public:
    // ROI defined as fraction of frame (0.0 to 1.0)
    float roiX      = 0.1f;   // left edge (10% from left)
    float roiY      = 0.1f;   // top edge (10% from top)
    float roiW      = 0.8f;   // width (80% of frame)
    float roiH      = 0.8f;   // height (80% of frame)

    // Detection parameters
    uint8_t pixelThreshold  = 30;    // min brightness change per pixel
    float   changeThreshold = 0.08f; // 8% of ROI pixels must change
    uint32_t cooldownMs     = 3000;  // 3 second cooldown after goal

    // Stats (readable from outside)
    float lastChangeRatio = 0.0f;
    uint32_t goalCount    = 0;
    uint32_t frameCount   = 0;
    uint32_t fps          = 0;

    bool begin(uint16_t width, uint16_t height) {
        _width = width;
        _height = height;
        _prevFrame = (uint8_t*)ps_malloc(width * height);
        if (!_prevFrame) {
            Serial.println("GoalDetector: failed to allocate prev frame buffer");
            return false;
        }
        memset(_prevFrame, 0, width * height);
        _hasPrev = false;
        _lastGoalTime = 0;
        _lastFpsTime = millis();
        _fpsCounter = 0;
        Serial.printf("GoalDetector: initialized %dx%d, ROI(%.0f%%,%.0f%%,%.0f%%,%.0f%%)\n",
            width, height, roiX*100, roiY*100, roiW*100, roiH*100);
        return true;
    }

    // Process a grayscale frame. Returns true if goal detected.
    bool processFrame(const uint8_t* frameData, size_t len) {
        frameCount++;
        _fpsCounter++;

        // Update FPS every second
        uint32_t now = millis();
        if (now - _lastFpsTime >= 1000) {
            fps = _fpsCounter;
            _fpsCounter = 0;
            _lastFpsTime = now;
        }

        if (len < _width * _height) {
            Serial.println("GoalDetector: frame too small");
            return false;
        }

        if (!_hasPrev) {
            memcpy(_prevFrame, frameData, _width * _height);
            _hasPrev = true;
            return false;
        }

        // Calculate ROI bounds in pixels
        uint16_t rx = (uint16_t)(roiX * _width);
        uint16_t ry = (uint16_t)(roiY * _height);
        uint16_t rw = (uint16_t)(roiW * _width);
        uint16_t rh = (uint16_t)(roiH * _height);

        // Count changed pixels in ROI
        uint32_t changedPixels = 0;
        uint32_t totalPixels = 0;

        for (uint16_t y = ry; y < ry + rh && y < _height; y++) {
            for (uint16_t x = rx; x < rx + rw && x < _width; x++) {
                uint32_t idx = y * _width + x;
                int diff = abs((int)frameData[idx] - (int)_prevFrame[idx]);
                if (diff > pixelThreshold) {
                    changedPixels++;
                }
                totalPixels++;
            }
        }

        // Store current frame as previous
        memcpy(_prevFrame, frameData, _width * _height);

        // Calculate change ratio
        lastChangeRatio = (totalPixels > 0) ? (float)changedPixels / totalPixels : 0.0f;

        // Check if goal detected (above threshold and not in cooldown)
        bool inCooldown = (now - _lastGoalTime) < cooldownMs;

        if (lastChangeRatio >= changeThreshold && !inCooldown) {
            _lastGoalTime = now;
            goalCount++;
            Serial.printf("GOAL! #%d (change: %.1f%%, threshold: %.1f%%)\n",
                goalCount, lastChangeRatio * 100, changeThreshold * 100);
            return true;
        }

        return false;
    }

private:
    uint16_t _width = 0;
    uint16_t _height = 0;
    uint8_t* _prevFrame = nullptr;
    bool _hasPrev = false;
    uint32_t _lastGoalTime = 0;
    uint32_t _lastFpsTime = 0;
    uint32_t _fpsCounter = 0;
};
