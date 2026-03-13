// =============================================================
// Web server with calibration UI + MJPEG stream
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

// Game state (defined in main.cpp)
enum GameState { STATE_IDLE, STATE_CALIBRATING, STATE_PLAYING };
extern volatile GameState gameState;
extern volatile int16_t calR, calG, calB;
extern volatile int calPixelCount, calBboxW, calBboxH;

// Calibration snapshot (defined in main.cpp)
extern uint8_t* calSnapshotBuf;
extern size_t calSnapshotLen;
extern SemaphoreHandle_t calSnapshotMutex;
extern char calFeedback[];

// Actions (defined in main.cpp)
extern void requestCalibration();
extern void requestStart();
extern void requestStop();

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
    char buf[768];
    bool scored = goalJustScored;
    if (scored) goalJustScored = false;
    extern volatile int lastMatchCount, lastBboxW, lastBboxH, lastMinPx, lastMaxPx, lastMaxBbox;
    extern volatile float lastDensity;
    extern volatile const char* lastRejectReason;
    snprintf(buf, sizeof(buf),
        "{\"goals\":%d,\"fps\":%d,\"change\":%.2f,\"frames\":%d,\"scored\":%s,"
        "\"state\":%d,\"calibrated\":%s,\"calR\":%d,\"calG\":%d,\"calB\":%d,"
        "\"calPx\":%d,\"calW\":%d,\"calH\":%d,"
        "\"matchPx\":%d,\"bboxW\":%d,\"bboxH\":%d,\"density\":%.0f,"
        "\"minPx\":%d,\"maxPx\":%d,\"maxBbox\":%d,\"reject\":\"%s\","
        "\"calMsg\":\"%s\",\"hasSnap\":%s}",
        detector.goalCount, detector.fps, detector.lastChangeRatio * 100,
        detector.frameCount, scored ? "true" : "false",
        (int)gameState, calR >= 0 ? "true" : "false",
        (int)calR, (int)calG, (int)calB,
        (int)calPixelCount, (int)calBboxW, (int)calBboxH,
        (int)lastMatchCount, (int)lastBboxW, (int)lastBboxH, (float)lastDensity,
        (int)lastMinPx, (int)lastMaxPx, (int)lastMaxBbox,
        lastRejectReason ? lastRejectReason : "",
        calFeedback,
        calSnapshotLen > 0 ? "true" : "false");
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

static esp_err_t stop_handler(httpd_req_t *req) {
    requestStop();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}

static esp_err_t index_handler(httpd_req_t *req) {
    const char* html =
        "<!DOCTYPE html><html><head>"
        "<title>gol-cam</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{background:#111;color:#fff;font-family:system-ui,sans-serif;"
        "display:flex;flex-direction:column;align-items:center;margin:0;padding:15px}"
        "h1{margin:5px 0;font-size:1.8em}"
        "img{max-width:100%;border:2px solid #333;border-radius:8px;margin:10px 0}"
        "#score{font-size:5em;font-weight:bold;margin:5px 0;transition:all 0.3s}"
        "#goal-flash{position:fixed;top:0;left:0;width:100%;height:100%;"
        "background:rgba(0,255,0,0.3);pointer-events:none;opacity:0;transition:opacity 0.5s}"
        "#goal-text{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);"
        "font-size:6em;font-weight:bold;color:#0f0;opacity:0;transition:opacity 0.5s;"
        "pointer-events:none;text-shadow:0 0 30px #0f0}"
        ".active{opacity:1 !important}"
        ".btn{padding:12px 30px;font-size:1.1em;border:none;border-radius:8px;"
        "cursor:pointer;margin:5px;font-weight:bold;transition:all 0.2s}"
        ".btn:active{transform:scale(0.95)}"
        "#btn-cal{background:#f90;color:#000}"
        "#btn-start{background:#0a0;color:#fff}"
        "#btn-stop{background:#c00;color:#fff;display:none}"
        "#info{color:#888;font-size:0.85em;margin:5px;text-align:center}"
        "#cal-info{color:#f90;font-size:0.9em;margin:5px;display:none}"
        "#cal-snap{max-width:100%;border:2px solid #f90;border-radius:8px;margin:8px 0;display:none}"
        "#cal-feedback{color:#fff;font-size:0.85em;margin:5px;padding:8px 12px;"
        "background:#222;border-radius:6px;display:none;max-width:400px;text-align:center}"
        ".cal-ok{border-left:3px solid #0f0}.cal-fail{border-left:3px solid #f44}"
        "#console{width:100%;max-width:400px;height:180px;background:#0a0a0a;"
        "border:1px solid #333;border-radius:6px;margin:10px 0;padding:8px;"
        "font-family:monospace;font-size:0.7em;color:#0f0;overflow-y:auto;"
        "line-height:1.4}"
        ".log-reject{color:#f44}.log-dice{color:#0f0}.log-cal{color:#f90}"
        ".controls{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;margin:8px 0}"
        "#state-badge{font-size:0.8em;padding:4px 12px;border-radius:12px;margin:5px}"
        ".s-idle{background:#333;color:#888}"
        ".s-cal{background:#f90;color:#000}"
        ".s-play{background:#0a0;color:#fff}"
        "</style></head><body>"
        "<div id='goal-flash'></div>"
        "<div id='goal-text'>GOOOL!</div>"
        "<h1>gol-cam</h1>"
        "<div id='state-badge' class='s-idle'>IDLE</div>"
        "<div id='score' style='display:none'>0</div>"
        "<img id='cam'/>"
        "<div class='controls'>"
        "<button class='btn' id='btn-cal' onclick='calibrate()'>Calibrate Dice</button>"
        "<button class='btn' id='btn-start' onclick='startGame()'>Start Game</button>"
        "<button class='btn' id='btn-stop' onclick='stopGame()'>Stop Game</button>"
        "</div>"
        "<div id='cal-feedback'></div>"
        "<img id='cal-snap'/>"
        "<div id='cal-info'></div>"
        "<div id='info'>Place the dadinho in view, then press Calibrate</div>"
        "<div id='console'></div>"
        "<script>"
        "document.getElementById('cam').src='http://'+location.hostname+':81/stream';"
        "const $=id=>document.getElementById(id);"
        "let lastState=-1;"
        "const con=$('console');"
        "function log(msg,cls){"
        "const d=document.createElement('div');"
        "if(cls)d.className=cls;"
        "d.textContent=msg;"
        "con.appendChild(d);"
        "if(con.children.length>100)con.removeChild(con.firstChild);"
        "con.scrollTop=con.scrollHeight;}"

        "async function calibrate(){"
        "$('btn-cal').textContent='Calibrating...';"
        "$('btn-cal').disabled=true;"
        "$('cal-feedback').style.display='block';"
        "$('cal-feedback').textContent='Analyzing frame...';"
        "$('cal-feedback').className='';"
        "await fetch('/calibrate');"
        "setTimeout(async()=>{"
        "try{const r=await fetch('/status');const d=await r.json();"
        "if(d.calMsg){"
        "$('cal-feedback').textContent=d.calMsg;"
        "$('cal-feedback').className=d.calibrated?'cal-ok':'cal-fail';}"
        "if(d.hasSnap){"
        "$('cal-snap').src='/cal-snapshot?t='+Date.now();"
        "$('cal-snap').style.display='block';}"
        "}catch(e){}"
        "$('btn-cal').textContent='Calibrate Dice';"
        "$('btn-cal').disabled=false;},1500);}"

        "async function startGame(){await fetch('/start');"
        "$('cal-snap').style.display='none';$('cal-feedback').style.display='none';}"
        "async function stopGame(){await fetch('/stop');}"

        "setInterval(async()=>{"
        "try{const r=await fetch('/status');const d=await r.json();"
        // Update state badge
        "if(d.state!==lastState){"
        "lastState=d.state;"
        "const b=$('state-badge');"
        "if(d.state===0){b.textContent='IDLE';b.className='s-idle';"
        "$('score').style.display='none';"
        "$('btn-cal').style.display='';$('btn-start').style.display=d.calibrated?'':'none';"
        "$('btn-stop').style.display='none';"
        "$('info').textContent=d.calibrated?'Calibrated! Press Start Game':'Place the dadinho in view, then press Calibrate';}"
        "else if(d.state===1){b.textContent='CALIBRATING...';b.className='s-cal';}"
        "else if(d.state===2){b.textContent='PLAYING';b.className='s-play';"
        "$('score').style.display='';$('btn-cal').style.display='none';"
        "$('btn-start').style.display='none';$('btn-stop').style.display='';}}"
        // Update calibration info
        "if(d.calibrated){$('cal-info').style.display='';"
        "$('cal-info').textContent='Dice: '+d.calPx+'px, '+d.calW+'x'+d.calH+"
        "'px (RGB565: '+d.calR+','+d.calG+','+d.calB+')';}"
        // Update score
        "$('score').textContent=d.goals;"
        "$('info').textContent='fps:'+d.fps;"
        // Console log
        "if(d.state===1)log('Calibrating...','log-cal');"
        "if(d.calibrated&&lastState===1&&d.state===0){"
        "log('Calibrated: color=RGB565('+d.calR+','+d.calG+','+d.calB+"
        "') size='+d.calPx+'px bbox='+d.calW+'x'+d.calH+'px','log-cal');"
        "log('Limits: pixels '+Math.max(5,Math.floor(d.calPx/3))+'-'+(d.calPx*4)+"
        "', bbox<='+Math.max(d.calW,d.calH)*2+'px','log-cal');}"
        "if(d.state===2&&d.matchPx>0){"
        "const cls=d.reject==='DICE'?'log-dice':'log-reject';"
        "log(d.reject+': '+d.matchPx+'px bbox='+d.bboxW+'x'+d.bboxH+"
        "' dens='+d.density+'% (lim:'+d.minPx+'-'+d.maxPx+' bbox<='+d.maxBbox+')',cls);}"
        // Goal flash
        "if(d.scored){"
        "$('goal-flash').classList.add('active');$('goal-text').classList.add('active');"
        "setTimeout(()=>{$('goal-flash').classList.remove('active');"
        "$('goal-text').classList.remove('active')},2000);}"
        "}catch(e){$('info').textContent='disconnected';}},500);"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 10;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler };
        httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler };
        httpd_uri_t cal_uri = { .uri = "/calibrate", .method = HTTP_GET, .handler = calibrate_handler };
        httpd_uri_t cal_snap_uri = { .uri = "/cal-snapshot", .method = HTTP_GET, .handler = cal_snapshot_handler };
        httpd_uri_t start_uri = { .uri = "/start", .method = HTTP_GET, .handler = start_handler };
        httpd_uri_t stop_uri = { .uri = "/stop", .method = HTTP_GET, .handler = stop_handler };
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &cal_uri);
        httpd_register_uri_handler(server, &cal_snap_uri);
        httpd_register_uri_handler(server, &start_uri);
        httpd_register_uri_handler(server, &stop_uri);
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
