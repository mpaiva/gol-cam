# Plan — HMI cam-polling stability (stop "placar losing connection with cams")

**Date:** 2026-06-04
**User prompt:** *"scope it as a follow-up plan"* — referring to the
persistent-HTTPClient + throttled-polling fix for the HMI's recurring
WebServer lockup, which the user perceived as *"the placar keeps
losing connection with the cams"* during a calibration session.

---

## 1. Symptom (what the user sees)

While the user calibrates the cameras from their phone (talking
directly to each cam's web dashboard at `192.168.40.90` /
`192.168.40.91`), the HMI display sporadically marks both cams as
`OFFLINE` in the HOME/AWAY side-status pills. The HMI's own
`/status` endpoint also bounces between `HTTP 200` and `Connection
refused`. A power-cycle (or a stronger DTR/RTS reset pulse) brings
it back, but the wedge recurs.

---

## 2. Root cause

The HMI shares a single 10-socket LWIP pool between two roles:

| Role | Who | Throughput | How |
|---|---|---|---|
| Server | `esp_http_server` (`max_open_sockets=10`, LRU purge) | Phone clients + my probes + integration-test runs | Inbound `/status`, `/goal`, `/api/reset`, `/debug/touch`, `/debug/gt911-rewrite`, `/a+`, `/b+`, … |
| Client | Arduino `HTTPClient` | `pollCam` 1 Hz × 2 cams + snapshot fetches + `httpKick` fan-out for CAL/START/RESET | Outbound `/status`, `/calibrate`, `/cal-snapshot`, `/reset`, etc. |

Every outbound `HTTPClient` call opens a fresh TCP socket and
closes it after the response. Each closed socket lingers in
`TIME_WAIT` for ~60 s. Steady-state churn:

```
  pollCam:       2 sockets / second  (1 Hz × 2 cams)
  cal-snapshot:  2 sockets / CAL tap
  httpKick:      1 socket  / CAL/START/RESET tap
  server:        1+ sockets / second under typical client polling
```

At 3+ sockets/sec × 60 s TIME_WAIT = 180 sockets in flight against
a 10-socket budget. LWIP usually recycles ports faster than that,
but under bursty load it can transiently exhaust → port 80 starts
refusing AND outbound `HTTPClient` calls start failing → the side-
status pills flash `OFFLINE`.

Earlier work (commit `77ec6ec`) replaced the Arduino `WebServer`
with `esp_http_server` and LRU purge, which fixed the *server-only*
lockup. The remaining bottleneck is the *client-side* socket
churn — `HTTPClient` opens a new socket per `pollCam` and per
snapshot fetch.

---

## 3. Proposed fix (4 levels — apply A + B, defer C, try D as belt-and-suspenders)

### Level A — Persistent HTTPClient per cam *(must-have)*
- Maintain one long-lived `HTTPClient` instance per cam (`camAClient`,
  `camBClient`), each with a permanent keep-alive connection to the
  cam's `/status` (and snapshot) endpoint.
- The cam's `esp_http_server` already supports HTTP/1.1 keep-alive,
  so the cam side needs no changes.
- Saves ~2 sockets/sec of client-side churn (the entire steady-state
  pollCam load).
- Same pattern that fixed the integration-test runner in commit
  `651b116`.
- **Cost:** ~50 LOC in `src_hmi/main.cpp`.
- **Risk:** low. If a keep-alive socket dies (cam reboots, WiFi blip),
  we drop and re-open on the next call — same drop-and-retry logic
  as the test runner.

### Level B — Throttle pollCam while overlay is up *(must-have)*
- During the calibration overlay's RUNNING phase, drop pollCam
  frequency from 1 Hz to 0.5 Hz (or even skip entirely — the
  overlay is already pulling fresh cam state via `/cal-snapshot`
  and via the cam's own polling-loop output).
- During RESULT phase, return to 1 Hz so the side pills resume
  accurate state immediately after dismiss.
- **Cost:** ~10 LOC.
- **Risk:** very low.

### Level C — Reuse the same keep-alive connection for snapshot + status *(polish)*
- The persistent client per cam from Level A can serve BOTH
  `/status` (small JSON) and `/cal-snapshot` (~2 KB JPEG) on the
  same TCP connection.
- Saves the 2 sockets/CAL the snapshot fetch currently churns.
- **Cost:** ~20 LOC. Need a small mutex / serialization so a
  pollCam and a snapshot fetch don't race on the same client.
- **Risk:** low–medium. The serialization can become a small
  latency contributor if not careful.

### Level D — Bump LWIP socket cap via build flag *(defense in depth)*
- Try `-DCONFIG_LWIP_MAX_SOCKETS=16` (or 24) in
  `platformio.ini`'s `[env:crowpanel_hmi]` `build_flags`.
- May or may not take effect — Arduino-ESP32 ships a pre-built
  LWIP, so this might need `-DLWIP_MAX_SOCKETS=16` and a custom
  sdkconfig override.
- Worth one experiment with a build, flash, and confirm the
  reported sockets-in-use under load.
- **Cost:** 1 LOC + 30 min of testing.
- **Risk:** none (worst case: flag is ignored, no behaviour change).

**Recommendation:** ship A + B together (they touch overlapping
code), measure for an hour of continuous use, then add C if the
lockup still recurs. D is a parallel experiment.

---

## 4. Implementation breakdown (Levels A + B)

All work in `src_hmi/main.cpp`.

1. **Add a per-cam `HTTPClient` + keep-alive helper**
   (~`src_hmi/main.cpp:280` near `CamState`)
   - New fields on `CamState`: `HTTPClient* httpClient`, `bool
     httpClientOpen`.
   - New helper `ensureCamClient(CamState& c)` — lazily allocates
     the `HTTPClient`, calls `http.setReuse(true)`, sets a 1500 ms
     timeout, and primes the connection by issuing the first GET
     when needed.
   - New helper `closeCamClient(CamState& c)` — only called on
     reboot / shutdown.
2. **Rewrite `pollCam`** (~`src_hmi/main.cpp:864`)
   - Drop the local `HTTPClient` instance and `http.begin()` /
     `http.end()` pattern.
   - Use `c.httpClient->GET()` with the persistent connection.
   - On failure (`code <= 0` or read timeout), call
     `c.httpClient->end()` + free + null out, so the NEXT call
     re-opens. Same drop-and-retry that the test runner uses.
3. **Rewrite `fetchCalSnapshot`** (~`src_hmi/main.cpp:730`) to
   look up the cam from URL prefix and route the GET through
   that cam's persistent client (Level C — second pass). Until
   Level C lands, keep `fetchCalSnapshot` using its own
   short-lived client — Level A alone already eliminates the
   biggest source of churn.
4. **Add overlay-aware throttle**
   (`workerTask` at `src_hmi/main.cpp:1311`)
   - Replace the hard-coded `CAM_POLL_MS = 1000` constant with a
     dynamic interval:
     ```cpp
     uint32_t camPollMs = overlayActive() && calOverlay.phase == 0
                          ? 2000   // 0.5 Hz while RUNNING
                          : 1000;  // 1 Hz baseline
     ```
   - The existing `now - lastCamA >= CAM_POLL_MS` check uses this
     dynamic value.
5. **Move pollCam to a slower interval entirely when cam is in
   STATE_AUTOTUNE** (`state == 4`) — the autotune sweep busy-loops
   the cam for ~6 s and we don't need 1 Hz status during that.
   ~10 LOC.

---

## 5. Acceptance criteria

| # | Criterion | How to verify |
|---|---|---|
| AC1 | After 1 hour of continuous calibration sessions, HMI port 80 stays responsive. | Leave the HMI on, trigger CAL A/B every 30 s for an hour, curl `/status` between each → all 200. |
| AC2 | Side-status pills don't flicker to OFFLINE under steady cam polling. | Run `for i in 1..600; do curl /debug/touch; sleep 1; done` while a partida is going. Pill state should match actual cam reachability, not bounce randomly. |
| AC3 | All 70 integration tests still pass. | `python3 tests/integration/test_system.py` |
| AC4 | `--with-autotune` tests still pass. | `python3 tests/integration/test_system.py --with-autotune` |
| AC5 | A cam reboot (or WiFi blip) doesn't permanently break pollCam. | Power-cycle cam A while HMI is running; HMI should recover the connection within ~5 s and resume polling. |
| AC6 | `/debug/touch` continues to return JSON during a calibration overlay session. | `curl http://192.168.40.89/debug/touch` while the overlay is up should return cleanly, not block on the worker. |

---

## 6. Risks / open questions

- **Memory.** Each persistent `HTTPClient` holds a `WiFiClient` +
  small read buffer + URL string. Two clients × ~5 KB each = ~10 KB
  resident. Negligible against our 320 KB available RAM + 8 MB
  PSRAM.
- **Stale keep-alive sockets.** If the cam's `httpd_keep_alive` is
  shorter than our polling interval (default is 5 s on
  `esp_http_server`), the connection drops between polls and we
  re-open every time anyway. Worth checking the cam's
  `httpd_config_t.keep_alive_idle` value or just bumping our poll
  rate to faster than that.
- **Throttle vs perceived responsiveness.** Dropping pollCam to
  0.5 Hz during overlay might delay the OFFLINE/READY pill update
  by an extra second. Probably fine since the user is staring at
  the overlay anyway.
- **Level D may need sdkconfig override.** Arduino-ESP32 v2.x
  ships a fixed LWIP build; the `-D` flag might not propagate.
  If it doesn't, that's not a regression — we still have A + B.

---

## 7. Pickup steps

1. Read the integration test runner's `_get_conn` / drop-and-retry
   pattern in `tests/integration/test_system.py` (commit `651b116`)
   for the keep-alive shape we want to mirror.
2. Add the two new helpers + persistent client fields on `CamState`.
3. Rewrite `pollCam` to use the persistent client.
4. Add the overlay-aware throttle in `workerTask`.
5. Flash + leave running for 30 min while curling `/status` once a
   second → confirm port 80 never refuses.
6. Run `python3 tests/integration/test_system.py` and
   `--with-autotune` → confirm no regression.
7. Commit + push.

---

## 7b. Update — 2026-06-04 attempt

Level A (persistent `HTTPClient` per cam with `setReuse(true)`) was
implemented and tested. Result: a 30 s, 1 Hz soak against the HMI's
`/status` went from baseline **~20 % success → still 20 %**. No
improvement, possibly slightly worse. Reverted.

Then ran the SAME 30 s, 1 Hz soak against all three boards in
parallel:

| Board | Success |
|---|---|
| HMI `/status` @ `.89` | **6 / 30** (20 %) |
| Cam A `/status` @ `.90` | **30 / 30** (100 %) |
| Cam B `/status` @ `.91` | **30 / 30** (100 %) |

Same `esp_http_server` library, same protocol, same WiFi network.
The cams handle the load perfectly; the HMI doesn't. Conclusion:
the lockup is NOT primarily about socket-pool churn on the HMI's
client side. It's something HMI-specific.

Most likely candidate: **RGB-display DMA contention with WiFi on
ESP32-S3**. The HMI continuously DMAs the 800×480×16 bpp framebuffer
to the LCD peripheral at 60 Hz; WiFi also needs DMA channels for
TX/RX. Known issue in the ESP32-S3 + parallel-RGB-display +
Arduino-ESP32 stack: long-lived high-bandwidth LCD DMA can starve
the WiFi MAC and drop incoming SYN packets.

Possible mitigations to investigate next:
- Pin the WebServer task to the SAME core as the LCD driver (or
  the OPPOSITE core), to control whether they compete for cache /
  bus access.
- Lower the LCD pixel clock (currently 16 MHz) to reduce DMA
  burst pressure.
- Use psram framebuffer (Arduino_GFX supports `psramBuffer` ctor
  arg) so LCD DMA reads from PSRAM instead of internal SRAM,
  freeing internal SRAM for WiFi.
- Throttle background gfx redraws (we already only redraw on
  dirty flags, but a tighter audit might help).

Level B (the throttle from 1 Hz → 0.5 Hz during overlay) was kept
and committed. It's a real reduction of client-side socket churn
during the calibration window, but it's not the root cause of the
HMI's WebServer instability. The plan now has three unresolved
mitigations to try (LCD DMA priority, lower pixel clock, PSRAM
framebuffer) before the lockup will reliably go away.

## 8. Out of scope

- Cam-side firmware changes. The cam's `esp_http_server` is fine
  as-is; only the HMI changes.
- Replacing `HTTPClient` with the lower-level `esp_http_client_*`
  IDF API. `HTTPClient.setReuse(true)` gives us keep-alive without
  the rewrite.
- The live-stream MJPEG plan (`.plans/live-stream-preview.md`).
  These are complementary — live-stream is a feature add; this
  plan is a stability fix. Could land either order.
- The cam B intermittent-dropout issue logged on 2026-06-03. That
  appears to be a hardware/WiFi issue on the cam itself, separate
  from the HMI socket churn.
