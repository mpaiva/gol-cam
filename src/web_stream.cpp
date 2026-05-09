// =============================================================
// Web server with calibration UI + MJPEG stream
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include "pins.h"
#include "goal_detector.h"
#include "frame_store.h"
#include "mode_select.h"
#include "match_dashboard.h"
#include "training_dashboard.h"

extern GoalDetector detector;
extern FrameStore frameStore;
extern volatile bool goalJustScored;

// Game state (defined in main.cpp)
enum GameState { STATE_IDLE, STATE_CALIBRATING, STATE_PLAYING, STATE_PAUSED };
extern volatile GameState gameState;
extern volatile int calContrastMin;
extern volatile int calPixelCount, calBboxW, calBboxH;
extern volatile int roiOffsetX, roiOffsetY;
extern volatile int roiW, roiH;

// Calibration snapshot (defined in main.cpp)
extern uint8_t* calSnapshotBuf;
extern size_t calSnapshotLen;
extern SemaphoreHandle_t calSnapshotMutex;
extern char calFeedback[];

// Goal snapshot (defined in main.cpp)
extern uint8_t* goalSnapshotBuf;
extern size_t goalSnapshotLen;
extern SemaphoreHandle_t goalSnapshotMutex;
extern volatile uint32_t goalSnapshotSeq;
extern volatile uint32_t lastGoalTimeMs;

// Actions (defined in main.cpp)
extern void requestCalibration();
extern volatile int goalAudioSelection;
extern volatile int contrastThreshold;
extern volatile int speakerVolume;
extern void playGoalSound();
extern volatile int audioPreview;
extern void requestStart();
extern void requestPause();
extern void requestResume();
extern void requestStop();
extern void requestDeduct();
extern void requestReset();

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    char part_buf[64];
    uint8_t* jpg_buf = (uint8_t*)ps_malloc(64 * 1024);
    if (!jpg_buf) { httpd_resp_send_500(req); return ESP_FAIL; }

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) { free(jpg_buf); return res; }

    uint32_t lastSeq = 0;
    while (true) {
        uint32_t seq = frameStore.seq();
        if (seq == lastSeq) { delay(10); continue; }
        size_t jpg_len = frameStore.get(jpg_buf, 64 * 1024, &lastSeq);
        if (jpg_len == 0) { delay(10); continue; }
        size_t hlen = snprintf(part_buf, 64, STREAM_PART, jpg_len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);
        if (res != ESP_OK) break;
    }
    free(jpg_buf);
    return res;
}

static esp_err_t status_handler(httpd_req_t *req) {
    char buf[896];
    bool scored = goalJustScored;
    if (scored) goalJustScored = false;
    extern volatile int lastMatchCount, lastBboxW, lastBboxH, lastMinPx, lastMaxPx, lastMaxBbox;
    extern volatile float lastDensity;
    extern volatile const char* lastRejectReason;
    uint32_t now = millis();
    uint32_t elapsed = now - lastGoalTimeMs;
    int cdRemain = (lastGoalTimeMs > 0 && elapsed < 10000) ? (int)(10000 - elapsed) : 0;
    const char* role = "single";
    const char* peer = "";
#ifdef BOARD_ROLE
    role = BOARD_ROLE;
#endif
#ifdef PEER_IP
    peer = PEER_IP;
#endif
    snprintf(buf, sizeof(buf),
        "{\"goals\":%d,\"fps\":%d,\"change\":%.2f,\"frames\":%d,\"scored\":%s,"
        "\"state\":%d,\"calibrated\":%s,\"calContrast\":%d,"
        "\"calPx\":%d,\"calW\":%d,\"calH\":%d,"
        "\"matchPx\":%d,\"bboxW\":%d,\"bboxH\":%d,\"density\":%.0f,"
        "\"minPx\":%d,\"maxPx\":%d,\"maxBbox\":%d,\"reject\":\"%s\","
        "\"calMsg\":\"%s\",\"hasSnap\":%s,\"goalSeq\":%d,\"cdRemain\":%d,"
        "\"role\":\"%s\",\"peer\":\"%s\",\"roiX\":%d,\"roiY\":%d,\"roiW\":%d,\"roiH\":%d}",
        detector.goalCount, detector.fps, detector.lastChangeRatio * 100,
        detector.frameCount, scored ? "true" : "false",
        (int)gameState, calContrastMin > 0 ? "true" : "false",
        (int)calContrastMin,
        (int)calPixelCount, (int)calBboxW, (int)calBboxH,
        (int)lastMatchCount, (int)lastBboxW, (int)lastBboxH, (float)lastDensity,
        (int)lastMinPx, (int)lastMaxPx, (int)lastMaxBbox,
        lastRejectReason ? lastRejectReason : "",
        calFeedback,
        calSnapshotLen > 0 ? "true" : "false",
        (int)goalSnapshotSeq, cdRemain,
        role, peer, (int)roiOffsetX, (int)roiOffsetY, (int)roiW, (int)roiH);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, strlen(buf));
}

static esp_err_t cal_snapshot_handler(httpd_req_t *req) {
    if (!calSnapshotBuf || !calSnapshotMutex || calSnapshotLen == 0) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    xSemaphoreTake(calSnapshotMutex, portMAX_DELAY);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t res = httpd_resp_send(req, (const char*)calSnapshotBuf, calSnapshotLen);
    xSemaphoreGive(calSnapshotMutex);
    return res;
}

static esp_err_t goal_snapshot_handler(httpd_req_t *req) {
    if (!goalSnapshotBuf || !goalSnapshotMutex || goalSnapshotLen == 0) {
        httpd_resp_send_404(req);
        return ESP_OK;
    }
    xSemaphoreTake(goalSnapshotMutex, portMAX_DELAY);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t res = httpd_resp_send(req, (const char*)goalSnapshotBuf, goalSnapshotLen);
    xSemaphoreGive(goalSnapshotMutex);
    return res;
}

static esp_err_t calibrate_handler(httpd_req_t *req) {
    requestCalibration();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t start_handler(httpd_req_t *req) {
    requestStart();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t pause_handler(httpd_req_t *req) {
    requestPause();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t resume_handler(httpd_req_t *req) {
    requestResume();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t stop_handler(httpd_req_t *req) {
    requestStop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t deduct_handler(httpd_req_t *req) {
    requestDeduct();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t cam_handler(httpd_req_t *req) {
    char buf[128];
    char resp[128] = "{\"ok\":true}";
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
            char val[16];
            // Presets: ?preset=N
            if (httpd_query_key_value(buf, "preset", val, sizeof(val)) == ESP_OK) {
                int p = atoi(val);
                if (p == 0) { // Vivid (default)
                    s->set_saturation(s, 2); s->set_contrast(s, 2); s->set_brightness(s, 1);
                    s->set_sharpness(s, 2); s->set_special_effect(s, 0);
                    s->set_whitebal(s, 0); s->set_awb_gain(s, 0);
                    digitalWrite(LED_PIN, LOW);
                } else if (p == 1) { // Night vision (IR LEDs + high gain)
                    s->set_saturation(s, 2); s->set_contrast(s, 2); s->set_brightness(s, 2);
                    s->set_sharpness(s, 2); s->set_special_effect(s, 0);
                    s->set_whitebal(s, 0); s->set_awb_gain(s, 0);
                    s->set_gainceiling(s, (gainceiling_t)6);
                    digitalWrite(LED_PIN, HIGH);
                } else if (p == 2) { // Grayscale
                    s->set_special_effect(s, 2);
                    s->set_contrast(s, 2); s->set_brightness(s, 1);
                    digitalWrite(LED_PIN, LOW);
                } else if (p == 3) { // Negative
                    s->set_special_effect(s, 1);
                    s->set_contrast(s, 1); s->set_brightness(s, 0);
                    digitalWrite(LED_PIN, LOW);
                } else if (p == 4) { // High contrast B&W
                    s->set_special_effect(s, 2);
                    s->set_contrast(s, 2); s->set_brightness(s, 2);
                    s->set_sharpness(s, 2);
                    digitalWrite(LED_PIN, LOW);
                } else if (p == 5) { // IR Night + Grayscale
                    s->set_special_effect(s, 2);
                    s->set_contrast(s, 2); s->set_brightness(s, 2);
                    s->set_sharpness(s, 2);
                    s->set_gainceiling(s, (gainceiling_t)6);
                    digitalWrite(LED_PIN, HIGH);
                } else if (p == 6) { // Max contrast
                    s->set_saturation(s, 2); s->set_contrast(s, 2);
                    s->set_brightness(s, 2); s->set_sharpness(s, 2);
                    s->set_special_effect(s, 0);
                    s->set_whitebal(s, 0); s->set_awb_gain(s, 0);
                    s->set_gainceiling(s, (gainceiling_t)6);
                    s->set_exposure_ctrl(s, 1); s->set_aec2(s, 1);
                    s->set_gain_ctrl(s, 1);
                    s->set_raw_gma(s, 1); s->set_lenc(s, 1);
                    digitalWrite(LED_PIN, LOW);
                }
            }
            // Individual controls: ?sat=N&con=N&bri=N&sharp=N&effect=N&ir=0|1
            if (httpd_query_key_value(buf, "sat", val, sizeof(val)) == ESP_OK)
                s->set_saturation(s, atoi(val));
            if (httpd_query_key_value(buf, "con", val, sizeof(val)) == ESP_OK)
                s->set_contrast(s, atoi(val));
            if (httpd_query_key_value(buf, "bri", val, sizeof(val)) == ESP_OK)
                s->set_brightness(s, atoi(val));
            if (httpd_query_key_value(buf, "sharp", val, sizeof(val)) == ESP_OK)
                s->set_sharpness(s, atoi(val));
            if (httpd_query_key_value(buf, "aec", val, sizeof(val)) == ESP_OK)
                s->set_aec_value(s, atoi(val));
            if (httpd_query_key_value(buf, "gain", val, sizeof(val)) == ESP_OK)
                s->set_agc_gain(s, atoi(val));
            if (httpd_query_key_value(buf, "gceil", val, sizeof(val)) == ESP_OK)
                s->set_gainceiling(s, (gainceiling_t)atoi(val));
            if (httpd_query_key_value(buf, "gma", val, sizeof(val)) == ESP_OK)
                s->set_raw_gma(s, atoi(val));
            if (httpd_query_key_value(buf, "lenc", val, sizeof(val)) == ESP_OK)
                s->set_lenc(s, atoi(val));
            if (httpd_query_key_value(buf, "effect", val, sizeof(val)) == ESP_OK)
                s->set_special_effect(s, atoi(val));
            if (httpd_query_key_value(buf, "ir", val, sizeof(val)) == ESP_OK)
                ledcWrite(7, atoi(val) ? 255 : 0);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t roi_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(buf, "dx", val, sizeof(val)) == ESP_OK)
            roiOffsetX += atoi(val);
        if (httpd_query_key_value(buf, "dy", val, sizeof(val)) == ESP_OK)
            roiOffsetY += atoi(val);
        if (httpd_query_key_value(buf, "x", val, sizeof(val)) == ESP_OK)
            roiOffsetX = atoi(val);
        if (httpd_query_key_value(buf, "y", val, sizeof(val)) == ESP_OK)
            roiOffsetY = atoi(val);
        if (httpd_query_key_value(buf, "dw", val, sizeof(val)) == ESP_OK)
            roiW += atoi(val);
        if (httpd_query_key_value(buf, "dh", val, sizeof(val)) == ESP_OK)
            roiH += atoi(val);
        if (httpd_query_key_value(buf, "w", val, sizeof(val)) == ESP_OK)
            roiW = atoi(val);
        if (httpd_query_key_value(buf, "h", val, sizeof(val)) == ESP_OK)
            roiH = atoi(val);
        // Clamp size (min 32px, max full frame)
        if (roiW < 32) roiW = 32;
        if (roiH < 32) roiH = 32;
        if (roiW > 320) roiW = 320;
        if (roiH > 240) roiH = 240;
        // Clamp offset so ROI stays within frame
        int maxOx = (320 - roiW) / 2, maxOy = (240 - roiH) / 2;
        if (roiOffsetX > maxOx) roiOffsetX = maxOx;
        if (roiOffsetX < -maxOx) roiOffsetX = -maxOx;
        if (roiOffsetY > maxOy) roiOffsetY = maxOy;
        if (roiOffsetY < -maxOy) roiOffsetY = -maxOy;
    }
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"roiX\":%d,\"roiY\":%d,\"roiW\":%d,\"roiH\":%d}",
        (int)roiOffsetX, (int)roiOffsetY, (int)roiW, (int)roiH);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t reset_handler(httpd_req_t *req) {
    requestReset();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t mode_select_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, MODE_SELECT_HTML, strlen(MODE_SELECT_HTML));
}

static esp_err_t match_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, MATCH_DASHBOARD_HTML, strlen(MATCH_DASHBOARD_HTML));
}

static esp_err_t training_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    return httpd_resp_send(req, TRAINING_DASHBOARD_HTML, strlen(TRAINING_DASHBOARD_HTML));
}

static esp_err_t test_sound_handler(httpd_req_t *req) {
    playGoalSound();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t volume_handler(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(buf, "val", val, sizeof(val)) == ESP_OK) {
            int v = atoi(val);
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            speakerVolume = v;
        }
    }
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"volume\":%d}", speakerVolume);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t led_handler(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(buf, "val", val, sizeof(val)) == ESP_OK) {
            int v = atoi(val);
            digitalWrite(LED_PIN, v ? HIGH : LOW);
            Serial.printf("[led] set=%d pin=%d\n", v, LED_PIN);
        }
    }
    int state = digitalRead(LED_PIN);
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"led\":%d}", state);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t threshold_handler(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(buf, "val", val, sizeof(val)) == ESP_OK) {
            int v = atoi(val);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            contrastThreshold = v;
        }
    }
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"threshold\":%d}", contrastThreshold);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t audio_handler(httpd_req_t *req) {
    char buf[32];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(buf, "id", val, sizeof(val)) == ESP_OK) {
            int id = atoi(val);
            if (id >= 0 && id <= 2) {
                goalAudioSelection = id;
                audioPreview = 1;
                playGoalSound();
            }
        }
    }
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"audio\":%d}", goalAudioSelection);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 21;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = mode_select_handler };
        httpd_uri_t training_uri = { .uri = "/training", .method = HTTP_GET, .handler = training_handler };
        httpd_uri_t match_uri = { .uri = "/match", .method = HTTP_GET, .handler = match_handler };
        httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
        httpd_uri_t cal_uri = { .uri = "/calibrate", .method = HTTP_GET, .handler = calibrate_handler };
        httpd_uri_t cal_snap_uri = { .uri = "/cal-snapshot", .method = HTTP_GET, .handler = cal_snapshot_handler };
        httpd_uri_t goal_snap_uri = { .uri = "/goal-snapshot", .method = HTTP_GET, .handler = goal_snapshot_handler };
        httpd_uri_t start_uri = { .uri = "/start", .method = HTTP_GET, .handler = start_handler };
        httpd_uri_t pause_uri = { .uri = "/pause", .method = HTTP_GET, .handler = pause_handler };
        httpd_uri_t resume_uri = { .uri = "/resume", .method = HTTP_GET, .handler = resume_handler };
        httpd_uri_t stop_uri = { .uri = "/stop", .method = HTTP_GET, .handler = stop_handler };
        httpd_uri_t deduct_uri = { .uri = "/deduct", .method = HTTP_GET, .handler = deduct_handler };
        httpd_uri_t reset_uri = { .uri = "/reset", .method = HTTP_GET, .handler = reset_handler };
        httpd_uri_t roi_uri = { .uri = "/roi", .method = HTTP_GET, .handler = roi_handler };
        httpd_uri_t cam_uri = { .uri = "/cam", .method = HTTP_GET, .handler = cam_handler };
        httpd_uri_t test_sound_uri = { .uri = "/test-sound", .method = HTTP_GET, .handler = test_sound_handler };
        httpd_uri_t volume_uri = { .uri = "/volume", .method = HTTP_GET, .handler = volume_handler };
        httpd_uri_t led_uri = { .uri = "/led", .method = HTTP_GET, .handler = led_handler };
        httpd_uri_t threshold_uri = { .uri = "/threshold", .method = HTTP_GET, .handler = threshold_handler };
        httpd_uri_t audio_uri = { .uri = "/audio", .method = HTTP_GET, .handler = audio_handler };
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &training_uri);
        httpd_register_uri_handler(server, &match_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &cal_uri);
        httpd_register_uri_handler(server, &cal_snap_uri);
        httpd_register_uri_handler(server, &goal_snap_uri);
        httpd_register_uri_handler(server, &start_uri);
        httpd_register_uri_handler(server, &pause_uri);
        httpd_register_uri_handler(server, &resume_uri);
        httpd_register_uri_handler(server, &stop_uri);
        httpd_register_uri_handler(server, &deduct_uri);
        httpd_register_uri_handler(server, &reset_uri);
        httpd_register_uri_handler(server, &roi_uri);
        httpd_register_uri_handler(server, &cam_uri);
        httpd_register_uri_handler(server, &test_sound_uri);
        httpd_register_uri_handler(server, &volume_uri);
        httpd_register_uri_handler(server, &led_uri);
        httpd_register_uri_handler(server, &threshold_uri);
        httpd_register_uri_handler(server, &audio_uri);
        Serial.println("Web server started on port 80");
    }

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port = 32769;
    stream_config.stack_size = 8192;

    httpd_handle_t stream_server = NULL;
    if (httpd_start(&stream_server, &stream_config) == ESP_OK) {
        httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
        httpd_register_uri_handler(stream_server, &stream_uri);
        Serial.println("Stream server started on port 81");
    }
}
