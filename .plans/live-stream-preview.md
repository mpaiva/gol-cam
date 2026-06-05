# Plan — Live MJPEG cam preview on the HMI

**Date:** 2026-06-04
**User prompt:** *"scope it as a follow-up plan"* — referring to the
hybrid approach where the HMI pulls the cam's `/stream` MJPEG at low
fps and renders it into the calibration overlay (and possibly the
main placar view), keeping the placar/CAL buttons as native overlays.

The original question was *"can we access the cam's UI from the
display?"* — the realistic version of that, given ESP32-S3 has no
browser engine, is to mirror the cam's live video feed and keep the
operator UI native on the HMI.

---

## 1. Current state (what's already shipped)

| Piece | Status | Where |
|---|---|---|
| Cam's `/stream` endpoint | Working — serves multipart/x-mixed-replace MJPEG at QVGA 320×240 | `src/web_stream.cpp` `stream_handler()` |
| HMI's static cam-snapshot preview | Working — fetches `/cal-snapshot` JPEG once per CAL trigger, decodes via TJpg_Decoder, renders into a 320×240 region of the calibration overlay | `src_hmi/main.cpp` `fetchCalSnapshot()` + `drawCalOverlay()`, commit `ae41369` |
| TJpg_Decoder integration | Done — 32 KB PSRAM JPEG buffer, decode callback writes RGB565 to `gfx->draw16bitRGBBitmap` | `src_hmi/main.cpp`:`calSnapTjpgOutput` |
| Version-gated snap redraw | Done — `snapVersion` bumps on each fetch, `drawCalOverlay` only re-decodes when version changes (avoids flicker) | `src_hmi/main.cpp`:`drawCalOverlay` (snap-area block) |
| 4-strip background fill | Done — overlay's white background is filled around the snap area, not through it, so the cached JPEG survives non-content repaints | `src_hmi/main.cpp`:`drawCalOverlay` (top of function) |

So the **rendering pipeline** is ready. What's missing is a feed that
delivers a *fresh* JPEG every ~200 ms instead of one shot per CAL tap.

---

## 2. Three scope levels (pick one)

### Level A — Live preview during calibration overlay only *(recommended)*
- When the calibration overlay is RUNNING (phase 0), switch the
  snapshot-fetch loop from one-shot `/cal-snapshot` to continuous
  `/stream` polling at 5 fps.
- When the overlay transitions to RESULT phase, freeze on the last
  frame and do one final `/cal-snapshot` to lock in the actual
  calibration frame.
- When the overlay dismisses, stop the stream.
- **Closes the blind-calibration gap** — the operator sees what the
  cam sees *before* deciding the dadinho is positioned right.
- **Doesn't touch the main placar view** — same scoreboard layout,
  same touch behaviour outside the overlay.
- **Cost:** ~150 LOC. Adds an MJPEG-multipart parser (~50 LOC), a
  fetch-loop mode switch in the worker task, and a phase-aware
  fetch frequency. Re-uses the existing TJpg_Decoder pipeline
  unchanged.
- **Risk:** medium. WiFi jitter during continuous stream could
  stall frames; recoverable by reopening the stream on read
  timeout.

### Level B — Live preview always-on
- Reserve a 320×240 region on the main placar view (compact score
  digits would need to shrink) and stream the active-side cam
  continuously, even when no calibration is in progress.
- Lets the operator monitor the cam without ever opening a phone.
- **Cost:** medium-large. Layout redesign of the main placar (~50
  LOC), plus everything Level A adds.
- **Risk:** medium-high. Continuous 5 fps × 24 hours during a
  partida puts ~4 GB/day across WiFi — should be fine, but ESP32
  WiFi stacks have been known to hiccup on long-lived connections.
  Need a watchdog to reopen on stall.

### Level C — Two-up live preview
- Stream BOTH cams at 320×180 (or smaller) side by side, on either
  the main view or the overlay.
- Bandwidth doubles, CPU doubles. Probably hits the ESP32-S3 limit.
- Not recommended yet — would need profiling and likely a 2–3 fps
  cap.

**Recommendation:** ship Level A first. It addresses the original
"cam-blind during calibration" complaint without rearchitecting
anything. Level B comes later if the always-on monitoring UX
proves valuable in a real partida. Level C is parking-lot.

---

## 3. Implementation breakdown (Level A)

All work in `src_hmi/main.cpp` unless noted.

### 3.1 MJPEG-multipart frame reader
- The cam's `/stream` returns
  `Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321`
  with each part:
  ```
  --123456789000000000000987654321\r\n
  Content-Type: image/jpeg\r\n
  Content-Length: <n>\r\n
  X-Timestamp: <ms>\r\n
  \r\n
  <n bytes JPEG>\r\n
  ```
- New helper: `fetchMjpegFrame(WiFiClient& s, uint8_t* buf, size_t maxLen, size_t* outLen)`.
  - Reads until it sees `Content-Length:` header.
  - Parses N (integer ASCII).
  - Reads through the `\r\n\r\n` separator.
  - Reads exactly N bytes into `buf`.
  - Returns true if buffer filled, false on timeout / disconnect.
- Uses a 2 s read deadline per frame; abort and reconnect on
  failure.
- ~50 LOC.

### 3.2 Stream-mode worker loop
- Promote `snapFetchReq` from a single-shot flag to a `StreamMode`
  enum:
  - `STREAM_NONE` — idle, no fetch
  - `STREAM_ONE_SHOT` — current behaviour, fetch `/cal-snapshot`
    once then go back to NONE
  - `STREAM_LIVE` — keep a persistent `/stream` connection open;
    pull one frame per `frameInterval` ms (default 200 ms = 5 fps)
- Worker task (`workerTask` around `src_hmi/main.cpp:1311`) gains
  a new branch that, in `STREAM_LIVE` mode, owns an `HTTPClient`
  for the entire stream lifetime, calls `fetchMjpegFrame` in a
  loop, and bumps `snapVersion` on each successful frame.
- Mode transitions:
  - `runAction(ACT_CAL_A/B)` sets `STREAM_LIVE` with cam IP
  - Result-phase transition (`updateCalOverlay`, `1 → 0`) sets
    `STREAM_ONE_SHOT` and triggers a final `/cal-snapshot` fetch
  - Overlay dismiss (`updateCalOverlay` auto-dismiss + `ACT_CAL_CANCEL`)
    sets `STREAM_NONE`
- ~50 LOC.

### 3.3 Frame-rate throttling + back-pressure
- After each successful frame, sleep `max(0, frameInterval -
  decodeTime)` ms so we don't over-fetch.
- If `snapVersion` is bumped while `lastPaintedSnapVer` in
  drawCalOverlay hasn't caught up (main loop slower than worker),
  skip the next fetch — frames are disposable.
- `frameInterval` defaults to 200 ms but is gated by a constexpr
  so we can tune it from one place.

### 3.4 drawCalOverlay no-change
- Already version-gated, already decodes new JPEGs on
  `snapVersion` bump. **No changes needed** — the only difference
  from today's behaviour is that snapVersion now bumps every
  ~200 ms during RUNNING phase instead of once per CAL tap.

### 3.5 Reconnect-on-stall watchdog
- If `fetchMjpegFrame` returns false 3 times in a row, close the
  HTTPClient and start a fresh one. The cam's `/stream` is
  stateless — reopening just resumes from the latest frame.
- Track via a `streamStallCount` field.
- ~15 LOC.

---

## 4. Acceptance criteria

| # | Criterion | How to verify |
|---|---|---|
| AC1 | When CAL A is tapped, the snap area starts updating at ~5 fps with live cam frames within 500 ms. | Manual: tap CAL A, watch overlay. Snap area should visibly refresh; moving a hand in front of the cam should be visible on the HMI within ~200 ms. |
| AC2 | When the cam transitions to RESULT phase, the snap area shows the FINAL calibration frame (not the live stream's last-arrived frame). | Tap CAL A with a dadinho slowly moving across the ROI; the result frame should be the one the cam locked on, not whatever was on screen at the dismiss instant. |
| AC3 | When the overlay dismisses, the worker stops fetching MJPEG. | Watch serial; `[snap]` traffic should stop after dismiss. |
| AC4 | A WiFi blip during the stream doesn't leave the HMI stuck — it reconnects within ~3 s. | Toggle WiFi router off briefly; HMI should resume streaming after reconnect. |
| AC5 | All 70 integration tests still pass. | `python3 tests/integration/test_system.py` |
| AC6 | HMI's REST surface stays responsive during live streaming (no esp_http_server lockup). | Run `test_system.py` while the calibration overlay is up. |

---

## 5. Open questions

- **Heap pressure with TJpgDec at 5 fps.** Each decode allocates
  some internal work memory. If we see fragmentation over a long
  partida, switch the work buffer to a pre-allocated PSRAM region
  (TJpgDec supports this).
- **What if the user taps CAL again during live stream?** Simplest:
  treat it as "restart streaming for the other side". Stream from
  cam B if user tapped CAL B mid-CAL-A overlay.
- **Should the live stream pause during result phase?** Default in
  this plan: yes, freeze on the static snapshot for the 6 s result
  window. Alternative: keep streaming behind the result text. The
  static snapshot is more diagnostic.

---

## 6. Pickup steps

1. Read `src/web_stream.cpp` `stream_handler` (~line 73) to confirm
   the multipart boundary string format. Note: cam may auto-generate
   the boundary; the HMI parser should extract it from the response
   `Content-Type` header, not hard-code it.
2. Implement `fetchMjpegFrame()` as a standalone helper near
   `fetchCalSnapshot()` in `src_hmi/main.cpp` (~line 700).
3. Promote `snapFetchReq` to an enum + add the live-stream worker
   branch in `workerTask` (~line 1311).
4. Wire mode transitions in `runAction` (ACT_CAL_A/B, ACT_CAL_CANCEL)
   and `updateCalOverlay` (state-transition path).
5. Build + flash + verify ACs above + run integration suite.
6. Commit + push.

---

## 7. Not-in-scope (defer)

- Two-cam side-by-side preview (Level C).
- Always-on main-view live preview (Level B).
- Adding `/snapshot-now` to the camera firmware (a dedicated
  single-frame endpoint without the multipart wrapper). The cam's
  existing `/cal-snapshot` + `/stream` cover all the cases this
  plan needs.
- Compressing or downscaling the JPEG. QVGA at the cam's current
  quality is ~5–15 KB per frame, fine for our budget.
- Audio. The HMI's speaker isn't wired (this is the CrowPanel HMI,
  not the camera board which has the I2S amp).
