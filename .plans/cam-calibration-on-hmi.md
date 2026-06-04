# Plan — Cam Calibration on the HMI Display

**Date:** 2026-06-04
**User prompt:** *"create a plan to add the cam calibration to the display"*

The CrowPanel HMI already has `CAL A` and `CAL B` buttons that fire
`GET /calibrate` to each camera. Today, all visual feedback for the
calibration sequence lives only in the **camera's** web dashboard —
the HMI screen gives no indication that calibration is running,
what's happening, or whether it succeeded. The goal is to make
calibration legible directly on the HMI so the operator never has to
look at a laptop during a match setup.

---

## 1. What already works

| Piece | Where | Behaviour |
|---|---|---|
| `CAL A` / `CAL B` buttons | `src_hmi/main.cpp:346-349` | Tap → `runAction(ACT_CAL_A/B)` → `httpKick("http://<cam>/calibrate")` (fire-and-forget on the http worker) |
| Per-cam state struct | `src_hmi/main.cpp:273-282` | `CamState` tracks `online`, `state` (0=IDLE, 1=CAL, 2=PLAY, 3=PAUSE), `calibrated`, `goals` |
| Polling | `src_hmi/main.cpp:864-906` | `pollCam(camA/B)` every 1 s; pulls `state`, `calibrated`, `goals` from cam `/status` |
| Side status pill | `src_hmi/main.cpp:724-750` | Renders one of `OFFLINE`/`CAL...`/`PLAY`/`PAUSE`/`READY`/`IDLE` under HOME / AWAY |

So the infrastructure to *know* about calibration is in place. What's
missing is **dedicated visible feedback** on the screen while a
calibration is happening, plus the **richer field data** the camera
exposes in `/status` that the HMI never bothered to parse:

- `calMsg` — short human-readable progress string from the camera
  (e.g. *"ROI quieto, calibrando..."* / *"Sem objeto detectado"*).
- `motionTh` / `colorTh` / `calContrast` — the three thresholds
  learned during calibration.
- `calMotion` / `calU` / `calV` — the per-pixel motion floor and the
  UV chroma centre of the calibrated ball.
- `hasCalSnap` — flips `true` the moment the cam captures its
  calibration reference frame.
- `calW` / `calH` / `calPx` — bounding box + pixel count of the
  detected ball within the ROI.

---

## 2. UX options (pick one)

### Option A — minimal: enhanced side-status pill *(recommended baseline)*
- Make the existing per-side pill richer. While `state == 1` (CAL),
  show the cam's live `calMsg` underneath the pill ("ROI quieto…",
  "Sem objeto…", etc.) instead of just `"CAL..."`.
- On `state → 0` (back to IDLE), inspect `calibrated`:
  - `true` → green tick + "Calibrado" + two-line stats
    (`mot=<motionTh> col=<colorTh>`)
  - `false` → red "Falhou" + retry hint.
- Auto-clear back to normal `READY` / `IDLE` pill after 5 s of
  showing the result.
- **Cost:** small. Touches only `drawSideStatus()` + `pollCam()` +
  one new `extractStringField()` helper for `calMsg`. ~60 LOC.
- **Risk:** very low. Doesn't change layout or block any other UI.

### Option B — calibration overlay *(stretch)*
- When `CAL A` or `CAL B` is tapped, an overlay covers the score
  area showing a large centred title ("Calibrando Lado A — coloque
  o dado no gol"), the live `calMsg`, and a small progress bar
  driven by `(hasCalSnap, calContrast)`.
- Stays visible until cam state returns to IDLE; then shows
  success/failure for 3 s and dismisses itself.
- Operator can tap "Cancelar" to abort (sends `/reset` to the cam).
- **Cost:** medium. Needs an overlay rendering layer, a small state
  machine (`HMI_CAL_IDLE → HMI_CAL_RUNNING → HMI_CAL_RESULT →
  HMI_CAL_IDLE`), redraw of the underlying score after dismiss.
  ~180 LOC.
- **Risk:** low–medium. Overlapping rects need careful damage-
  tracking so dismissing the overlay doesn't leave score-area
  artifacts. Touch hit-test under the overlay must be disabled.

### Option C — full calibration snapshot *(later)*
- Same as B, but additionally pull `/cal-snapshot` JPEG from the
  camera and render it on the panel (~640 × 360 area). Operator can
  literally see the dadinho as the camera sees it.
- **Cost:** large. Requires a JPEG decoder (`esp_jpeg`, ~30 KB
  static), a chunked HTTP body reader (camera serves the JPEG
  directly, no pagination), and a temporary RGB565 framebuffer
  (~460 KB — fits in PSRAM but needs careful allocation).
- **Risk:** medium. JPEG decode timing during a live HMI session
  could glitch the touch loop if not on a separate task. Heap
  fragmentation is the main concern.
- Defer until A and B prove the workflow.

**Recommendation:** ship A first (small, immediately useful), then
B in the same six-day cycle if there's time. C is a "next sprint"
idea.

---

## 3. Implementation breakdown (Option A first)

All changes in `src_hmi/main.cpp` unless noted.

1. **Add `calMsg` to `CamState`** (`src_hmi/main.cpp:273`)
   - New field: `char calMsg[40] = {0};` (small char buffer, no
     heap involvement)
   - Mirror in `pollCam()`: add `extractStringField(body, "calMsg",
     c.calMsg, sizeof(c.calMsg))` after the existing extractors.
   - Reuse the substring-parser pattern already in `extractIntField`
     /`extractBoolField` — there's no JSON parser; we hand-grep
     fields. The new helper just copies the value between the
     closing `"` characters.

2. **Add `motionTh`, `colorTh`, `calContrast`, `hasCalSnap` to
   `CamState`** (`src_hmi/main.cpp:273`)
   - Four new ints + one bool. Pull them in `pollCam()` via the
     existing `extractIntField`/`extractBoolField`.
   - Skip the fields we don't render (e.g. `calMotion`, `calU/V`)
     for now — they're available later if we need richer feedback.

3. **Track a calibration result-display timer**
   (`src_hmi/main.cpp` near top of CamState):
   - `uint32_t calResultUntilMs = 0;` — populated by `pollCam()` on
     the state-transitions `1 → 0`, set to `millis() + 5000`.
   - `bool calLastOk = false;` — captured at the same moment.

4. **Extend `drawSideStatus()`** (`src_hmi/main.cpp:724-750`)
   - When `c.state == 1`: render the existing orange `CAL...` pill,
     AND a small line of text below it with `c.calMsg` (truncated
     to ~24 chars).
   - When `millis() < c.calResultUntilMs`: render a green
     `Calibrado` pill (if `calLastOk`) or red `Falhou` pill, plus
     a two-line stat: `mot=<motionTh>  col=<colorTh>`.
   - Otherwise render the existing READY/IDLE pill.

5. **Mark dirty on every `pollCam()` state change**
   (`src_hmi/main.cpp:864-906`)
   - Already does `dirtyStatus = true` on `changed`. Extend the
     `changed` check to also fire when `calMsg` differs or when the
     result timer expires — otherwise the textual status under the
     pill doesn't refresh.

6. **String-field extractor** (~10 LOC near other extractors)
   - Mirror `extractIntField`: find `"key":"`, copy until next `"`.
     No quote-escape handling required (the cam's calMsg avoids
     literal quotes).

7. **Tests**
   - The integration suite (`tests/integration/test_system.py`)
     already validates the camera's `/status` exposes `calMsg`
     during calibration (camera-side coverage).
   - Add a manual test note in `.plans/` for the HMI-side: "tap
     CAL A → confirm side pill turns orange + shows calMsg →
     within ~6 s shows green Calibrado pill with thresholds".

8. **Commit + push** (per CLAUDE.md global instruction "always
   commit after each task").

---

## 4. Implementation breakdown (Option B — to do *after* A lands)

1. Define a new struct `CalOverlay { int side; uint32_t startedMs;
   uint32_t finishMs; bool ok; char msg[48]; bool active; }` and a
   single global `calOverlay`.
2. New `drawCalOverlay()` function: clears a 760×340 rect over the
   score area, renders the title, current `calMsg`, and progress
   bar.
3. Wire `ACT_CAL_A` / `ACT_CAL_B` in `runAction()` to also set
   `calOverlay.active = true` so the next `loop()` iteration
   renders it.
4. In `loop()`, when `calOverlay.active`, suppress touch hit-test
   against the placar buttons EXCEPT a new on-overlay "Cancelar"
   button.
5. On cam state `1 → 0`, set `calOverlay.finishMs = millis() +
   3000`. After that point, redraw the full placar view
   (`renderFull()` is overkill; selectively redraw the area the
   overlay covered).
6. Polling-task dirty flag: `dirtyCalOverlay` so main loop knows
   to redraw it when `calMsg` changes.

---

## 5. Acceptance criteria

| # | Criterion | How to verify |
|---|---|---|
| AC1 | Tapping `CAL A` shows visible feedback on the screen within 1 s. | Manual: tap and watch the HOME pill — should turn orange. |
| AC2 | The HMI shows the camera's live `calMsg` text while calibration is running. | Manual: place dadinho in goal during calibration, watch text update from "Sem objeto" → "Calibrando..." → final state. |
| AC3 | On success, the HMI shows a green confirmation + the learned `motionTh` and `colorTh` values for 5 s. | Manual + curl `/status` to confirm thresholds match what's displayed. |
| AC4 | On failure (camera state returns to IDLE with `calibrated:false`), the HMI shows a red "Falhou" indicator. | Manual: trigger calibration with no dadinho present → cam returns IDLE without setting calibrated; HMI should show red. |
| AC5 | Calibration UI never blocks score updates or other touch input. | Manual: tap `A+` while CAL is shown; score must still increment. |
| AC6 | All 70 integration tests still pass. | `python3 tests/integration/test_system.py` |

---

## 6. Risks / unknowns

- **`calMsg` length variance.** The camera might send strings longer
  than expected. Truncate at the HMI side (40-char buffer with
  ellipsis on overflow). No memory risk because everything is
  stack-allocated.
- **Camera's `calMsg` is Portuguese.** Already matches the HMI's
  Portuguese-only UI. No i18n work needed.
- **Refresh rate.** `pollCam()` is 1 Hz. During the ~6 s
  calibration window, the operator will see 5–6 calMsg updates. If
  finer feedback is wanted, bump the camera-poll rate to 500 ms
  *only when* `state == 1` — gated to keep the 1 Hz baseline.
- **WebServer-already-fixed regression.** The HMI's REST surface
  is now `esp_http_server` (commit `77ec6ec`). Don't introduce any
  new code paths that hold an `HTTPClient` connection across
  multiple HTTP calls — the worker queue pattern already in place
  is the right one.

---

## 7. Open questions for the user (resolve before starting Option B)

- Should the overlay block manual score adjustments while
  calibration is in progress, or stay non-modal? **Default in
  this plan: non-modal — calibration overlay does not capture
  taps outside its own buttons.**
- On calibration failure, should the operator be prompted to
  re-tap CAL, or auto-retry once? **Default: manual re-tap.**
- Show the camera's calibration snapshot (Option C) eventually,
  or never? **Default: defer to a separate sprint.**

---

## 8. Pickup steps

When ready to implement:
1. Open `src_hmi/main.cpp:273` and add the new `CamState` fields.
2. Open `src_hmi/main.cpp:864` and extend `pollCam()` to populate
   them.
3. Open `src_hmi/main.cpp:724` and extend `drawSideStatus()` to
   render the new info.
4. Build with `pio run -e crowpanel_hmi`.
5. Flash and run the manual AC verification list above.
6. Run `python3 tests/integration/test_system.py` to confirm
   AC6 (no regression).
7. Commit + push (per CLAUDE.md global).

If Option B is approved later, repeat steps 1–7 on the overlay
struct + renderer.
