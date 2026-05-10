# Auto-Contrast Tune Plan

**User request:** "Create a button to auto-calibrate and find the highest contrast possible using black and white filter as it is shown in the attached image. Research and create a plan to achieve this objective."

**Reference image:** `/Users/mp/Downloads/IMG_6919.heic` — dashboard at 192.168.1.106 showing a B&W view with a bright dadinho on a dark surface.

**Decisions:**
- **Sweep scope:** full sweep — exposure (`aec`) + sensor contrast (`con`) + brightness (`bri`) + post-processing B&W threshold.
- **UI placement:** both Training dashboard and Match dashboard (Auto-Tune Home / Auto-Tune Away).

---

## 1. What "highest contrast" means

The dadinho (white dice) on a dark surface is a **bimodal scene**: pixels should cluster into a "dark background" group and a "bright foreground" group. The cleaner that separation, the more reliable downstream Sobel-edge calibration becomes.

Best metric for this: **Otsu's between-class variance** (`σ²_b`). For a given grayscale frame inside the ROI:
1. Build a 256-bin histogram.
2. For every threshold `t` in 0..255, compute the variance between the two classes (pixels `< t` and `>= t`).
3. The maximum value of `σ²_b` over all `t` is the contrast score for that frame.
4. The argmax `t` is the optimal binarization threshold.

A higher peak `σ²_b` means the camera+post settings produce a frame where dark and bright pixels are sharply separated — exactly what we want.

## 2. Parameter space

| Param | Range | Effect | Sweep values |
|---|---|---|---|
| `aec` (exposure) | 0..1200 | Most impactful. Too low → murky background, too high → blown-out dadinho. | `60, 100, 150, 220, 320, 480, 700` (7) |
| `con` (sensor contrast) | -2..2 | Stretches sensor's tonal curve. | `0, 1, 2` (3) |
| `bri` (sensor brightness) | -2..2 | Shifts midpoint. Coarse search is enough. | `-1, 0, 1` (3) |
| B&W threshold (post) | 0..255 | Pure software binarization step. Sweep on captured frame in memory — no recapture needed. | continuous, choose Otsu's optimum directly |

Sensor params we leave fixed at known-good defaults: `sharp=1`, `gain=8`, `gceil=1`, `gma=0`, `lenc=0`, `special_effect=2 (grayscale)`, `exposure_ctrl=0 (manual)`, `gain_ctrl=0 (manual)`.

## 3. Algorithm — staged search (not full Cartesian)

Full Cartesian would be 7×3×3 = 63 sensor combinations, ~7s. Staged search cuts it to ~13 captures (~1.5s) with comparable quality:

```
Stage 1 — Sweep aec (7 captures, others at defaults bri=-1,con=1)
  For each value: change sensor, settle (skip 2 frames), capture, score by Otsu.
  Keep best aec.

Stage 2 — Sweep con (3 captures, with best aec, bri=-1)
  Keep best con.

Stage 3 — Sweep bri (3 captures, with best aec+con)
  Keep best bri.

Stage 4 — On final frame, Otsu's argmax IS the optimal B&W threshold.
  No recapture. Software-only.

Total: 7 + 3 + 3 = 13 sensor changes ≈ 1.5–2s.
```

Each "settle + capture" cycle: change param → wait ~80ms → capture → discard → capture again → score. The OV3660 needs 1–2 frames to apply exposure changes.

## 4. State machine integration

Add `STATE_AUTOTUNE` to the existing `GameState` enum. Trigger via new HTTP endpoint `/autotune`. The main loop runs the staged sweep when in `STATE_AUTOTUNE`, then transitions back to `STATE_IDLE`.

Code goes in `src/main.cpp`:

```cpp
enum GameState { STATE_IDLE, STATE_CALIBRATING, STATE_AUTOTUNE, STATE_PLAYING, STATE_PAUSED };

// Auto-tune progress reporting (read by /status)
volatile int autotuneStage = 0;       // 0=idle, 1=exp, 2=con, 3=bri, 4=done
volatile int autotuneStep = 0;        // current step within stage
volatile int autotuneTotalSteps = 0;  // total for current stage
volatile int autotuneBestScore = 0;
volatile int autotuneBestAec = 150, autotuneBestCon = 1, autotuneBestBri = -1, autotuneBestThresh = 30;

void runAutotune();          // implemented in main.cpp
int otsuScore(uint8_t* px, int w, int h, int rx, int ry, int rw, int rh, int* bestThreshOut);
void requestAutotune();      // sets gameState = STATE_AUTOTUNE
```

Where:
- `otsuScore()` — compute histogram in ROI → loop 0..255 thresholds → return peak between-class variance and argmax threshold.
- `runAutotune()` — staged loop above, updates `calFeedback` after each stage so the dashboard can show progress.

## 5. HTTP endpoint

`src/web_stream.cpp`:

```cpp
extern void requestAutotune();
extern volatile int autotuneStage, autotuneStep, autotuneTotalSteps, autotuneBestScore;
extern volatile int autotuneBestAec, autotuneBestCon, autotuneBestBri, autotuneBestThresh;

static esp_err_t autotune_handler(httpd_req_t *req) {
    requestAutotune();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "{\"ok\":true}", 11);
}
```

Register URI `/autotune`. Bump `max_uri_handlers` from 20 → 21.

Extend `/status` JSON with: `"autoStage":N, "autoStep":N, "autoTotal":N, "autoScore":N, "autoAec":N, "autoCon":N, "autoBri":N, "autoThresh":N` so the dashboard can render live progress.

## 6. UI changes

### Training dashboard (`src/training_dashboard.h`)
Add inside `idle-controls`, next to `btn-cal`:
```html
<button class='btn btn-tune' id='btn-tune' onclick='autotune()' data-i18n='train.autotune'>&#9881; Auto-Tune</button>
```

Add JS:
```js
async function autotune(){
  $('btn-tune').disabled=true; $('btn-cal').disabled=true;
  $('cal-feedback').style.display='block';
  $('cal-feedback').textContent=t('train.autotuning');
  await fetch('/autotune');
  // poll /status until autoStage===4
}
```

When `autoStage===4`, sync the visual sliders (Bri/Con/Exp/B&W) to the new values returned by `/status`, then re-enable buttons. Show the resulting score and parameters in `cal-feedback`.

i18n entries: `train.autotune`, `train.autotuning`, `train.autotune_done`.

### Match dashboard (`src/match_dashboard.h`)
Inside each `cam-sliders` block (home and away), add a button next to the existing Calibrate Home / Calibrate Away buttons:
```html
<button class='btn btn-tune' onclick='autotune("home")'>&#9881; Auto-Tune</button>
```

JS variant: `async function autotune(side){ ... fetch('http://'+boards[side].ip+'/autotune') ... }`. Poll the corresponding board's `/status` and refresh sliders for that side.

## 7. Edge cases & safety

| Concern | Mitigation |
|---|---|
| Dadinho not in view | If best score is below threshold (e.g. < 500), report "no clear contrast — place dadinho in ROI". |
| User triggers autotune mid-game | `/autotune` rejects unless `gameState == STATE_IDLE`. |
| Sensor change during streaming | Already serialized — `loop()` is single-threaded; no race. |
| Sliders out of sync after autotune | Dashboard re-reads applied values from `/status` and updates `<input type='range'>` `value` attrs. |
| AGC/AEC sensor auto modes interfere | We keep them disabled (manual). Don't touch `set_exposure_ctrl` or `set_gain_ctrl` during autotune. |

## 8. Implementation steps (ordered)

1. **`src/main.cpp`:** Add `STATE_AUTOTUNE`, autotune state vars, `otsuScore()`, `runAutotune()`, `requestAutotune()`. Wire into `loop()`'s state dispatch.
2. **`src/web_stream.cpp`:** Add `/autotune` endpoint, externs, status JSON fields, register URI.
3. **`src/training_dashboard.h`:** Add Auto-Tune button, `autotune()` JS, status polling that syncs sliders, i18n.
4. **`src/match_dashboard.h`:** Add per-side Auto-Tune buttons + JS.
5. **Build & flash** to one board, place a dadinho in the ROI, tap Auto-Tune, verify:
   - Score climbs across stages (logged via Serial).
   - Final B&W view clearly shows white dice on dark background.
   - Sliders update to the chosen values.
6. **Test on second board** to confirm both work standalone.

## 9. Acceptance criteria

- New "Auto-Tune" button appears on Training dashboard idle controls and inside both home/away panels of Match dashboard.
- Tapping the button completes within 3 seconds and shows progress text during the sweep.
- After completion, the dashboard's Bri/Con/Exp/B&W sliders reflect the chosen values and the live MJPEG stream shows visibly higher contrast on the dadinho.
- Subsequent `Calibrate Dice` succeeds (Sobel peak above threshold) without any manual slider tweaking.
- If the dadinho is not in the ROI, autotune still completes but reports a low-contrast warning.

## 10. Open questions for review

- Should we lock `gain` low (e.g. 4) during autotune to reduce noise, then restore? Currently leaving it as user-set.
- Should the "best B&W threshold" picked by Otsu also become the new value of the existing B&W slider, or is it stored separately as an autotune-only override?
- After autotune, should we automatically chain into a calibrate-dice cycle? Or keep them as separate user actions? **Default: keep separate** — user explicitly hits Calibrate next.
