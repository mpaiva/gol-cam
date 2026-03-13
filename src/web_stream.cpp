// =============================================================
// MJPEG streaming server — reads JPEG from FrameStore
// (main loop is the only camera consumer)
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include "pins.h"
#include "goal_detector.h"
#include "frame_store.h"

extern GoalDetector detector;
extern FrameStore frameStore;
extern volatile bool goalJustScored;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    char part_buf[64];

    // Allocate a read buffer in PSRAM
    uint8_t* jpg_buf = (uint8_t*)ps_malloc(64 * 1024);
    if (!jpg_buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) { free(jpg_buf); return res; }

    uint32_t lastSeq = 0;
    while (true) {
        // Wait for a new frame (don't send duplicates)
        uint32_t seq = frameStore.seq();
        if (seq == lastSeq) {
            delay(10);
            continue;
        }

        size_t jpg_len = frameStore.get(jpg_buf, 64 * 1024, &lastSeq);
        if (jpg_len == 0) {
            delay(10);
            continue;
        }

        size_t hlen = snprintf(part_buf, 64, STREAM_PART, jpg_len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);

        if (res != ESP_OK) break;
    }

    free(jpg_buf);
    return res;
}

static esp_err_t capture_handler(httpd_req_t *req) {
    uint8_t* jpg_buf = (uint8_t*)ps_malloc(64 * 1024);
    if (!jpg_buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t jpg_len = frameStore.get(jpg_buf, 64 * 1024);
    if (jpg_len == 0) {
        free(jpg_buf);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, (const char *)jpg_buf, jpg_len);
    free(jpg_buf);
    return res;
}

static esp_err_t status_handler(httpd_req_t *req) {
    char buf[256];
    bool scored = goalJustScored;
    if (scored) goalJustScored = false;
    snprintf(buf, sizeof(buf),
        "{\"goals\":%d,\"fps\":%d,\"change\":%.2f,\"frames\":%d,\"scored\":%s}",
        detector.goalCount, detector.fps, detector.lastChangeRatio * 100,
        detector.frameCount, scored ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, strlen(buf));
}

static esp_err_t index_handler(httpd_req_t *req) {
    const char* html =
        "<!DOCTYPE html><html><head>"
        "<title>gol-cam</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{background:#111;color:#fff;font-family:sans-serif;"
        "display:flex;flex-direction:column;align-items:center;margin:0;padding:20px}"
        "img{max-width:100%;border:2px solid #333;border-radius:8px}"
        "h1{margin:10px 0 5px;font-size:2em}"
        "#score{font-size:4em;font-weight:bold;margin:10px 0;transition:all 0.3s}"
        "#goal-flash{position:fixed;top:0;left:0;width:100%;height:100%;"
        "background:rgba(0,255,0,0.3);pointer-events:none;opacity:0;transition:opacity 0.5s}"
        "#goal-text{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);"
        "font-size:6em;font-weight:bold;color:#0f0;opacity:0;transition:opacity 0.5s;"
        "pointer-events:none;text-shadow:0 0 30px #0f0}"
        "#stats{color:#666;font-size:0.8em;margin:5px}"
        ".active{opacity:1 !important}"
        "</style></head><body>"
        "<div id='goal-flash'></div>"
        "<div id='goal-text'>GOOOL!</div>"
        "<h1>gol-cam</h1>"
        "<div id='score'>0</div>"
        "<img id='cam'/>"
        "<div id='stats'>connecting...</div>"
        "<script>"
        "document.getElementById('cam').src="
        "'http://'+location.hostname+':81/stream';"
        "const score=document.getElementById('score');"
        "const flash=document.getElementById('goal-flash');"
        "const goalTxt=document.getElementById('goal-text');"
        "const stats=document.getElementById('stats');"
        "setInterval(async()=>{"
        "try{const r=await fetch('/status');const d=await r.json();"
        "score.textContent=d.goals;"
        "stats.textContent='fps:'+d.fps+' change:'+d.change.toFixed(1)+'%';"
        "if(d.scored){"
        "flash.classList.add('active');goalTxt.classList.add('active');"
        "setTimeout(()=>{flash.classList.remove('active');"
        "goalTxt.classList.remove('active')},2000);"
        "}}catch(e){stats.textContent='disconnected';}},500);"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

void startCameraServer() {
    // Main server on port 80 — page, status, capture
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
        httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
        httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &capture_uri);
        httpd_register_uri_handler(server, &status_uri);
        Serial.println("Web server started on port 80");
    }

    // Stream server on port 81 — long-lived MJPEG connection
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
