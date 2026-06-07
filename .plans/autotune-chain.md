# Plan — Chain `/autotune` → `/calibrate` on CAL A/B tap

**Date:** 2026-06-07
**User prompt:** *"scope the autotune-chain plan"*

The static-light dadinho calibration repeatedly failed during a real
partida even though the dadinho-vs-blue contrast looked fine to the
eye. Root cause: the cam's exposure was tuned for whatever ambient
light it last saw, not the actual play surface. The cam already has
a `/autotune` sweep that walks gain / gceil / AEC / brightness to
find the best exposure for the current scene — it just isn't
chained to the HMI's CAL button. This plan wires the chain.

---

## 1. What already works

| Piece | Status | Where |
|---|---|---|
| Cam `/autotune` endpoint + sweep | Working — 4 params × ~15 captures ≈ 6 s wall time | `src/main.cpp` `requestAutotune()` ~line 347, `runAutotune()` ~line 945 |
| `/status` exposes sweep progress | Yes — `autoStage`, `autoStep`, `autoTotal`, `autoDone`, plus best-* fields | `src/web_stream.cpp` status_handler ~line 98 |
| Integration tests for autotune | Shipped 2026-06-07 (commit `05161cc`), opt-in via `--with-autotune` | `tests/integration/test_system.py` `test_camera_autotune` |
| HMI cal overlay (RUNNING / RESULT_OK / RESULT_FAIL phases) | Working from commits `1b36d56` … `0182851` | `src_hmi/main.cpp` `CalOverlay`, `updateCalOverlay()`, `drawCalOverlay()` |
| HMI snapshot preview | Working — single fetch on CAL tap + refresh on state-transition | Commit `ae41369` |

So the cam side needs **no changes**. Only the HMI orchestrates.

---

## 2. Proposed flow

Single CAL A (or CAL B) tap now produces:

```
   user taps CAL A
   ───────────────
   t=0       overlay raises in new TUNING sub-phase
             title = "Calibrando câmera A — auto-ajuste"
             progress bar driven by autoStep / autoTotal
             snapshot preview pulled on raise (previous frame)
             HMI fires /autotune to cam A
   t≈6 s     pollCam sees autoDone=1, HMI advances sub-phase to CAL
             title = "Calibrando Lado A"
             HMI fires /calibrate to cam A
             snapshot preview refreshes
   t≈7 s     OK!/FAILED early detection fires (existing path)
             phase flips to RESULT_OK or RESULT_FAIL
             snapshot refreshes one last time
   t≈13 s    RESULT phase auto-dismisses, placar repaints
```

Net wait per CAL: ~7-13 s (was ~1-3 s without autotune). The user
got blind calibrations that failed repeatedly with no autotune;
chaining it trades ~5 s of overlay time for a much higher success
rate per CAL.

---

## 3. Scope levels (pick one)

### Level A — always chain *(recommended baseline)*
- Every CAL tap fires `/autotune` first, then `/calibrate`.
- No new buttons, no settings to remember.
- Cancelar during TUNING fires `/stop` to abort the sweep, same
  as today's CAL Cancelar.
- **Cost:** ~120 LOC in `src_hmi/main.cpp`.
- **Risk:** low. Cam side already proven via integration tests.

### Level B — skip autotune if cam was tuned recently
- Track the last successful autotune timestamp per cam. If <60 s
  since last tune, skip directly to `/calibrate`.
- Useful when the operator does multiple cal-retries with the
  dadinho in slightly different positions (lighting hasn't changed).
- **Cost:** +20 LOC on top of Level A.
- **Risk:** very low.

### Level C — separate "Tune" button vs "CAL" button
- CAL stays fast (cal only). New on-screen "Tune" button runs
  autotune. Operator decides when to tune.
- More UX overhead (operator has to remember to tune), but
  faster everyday flow.
- **Cost:** ~150 LOC (overlay needs new phase + button hit area).
- **Risk:** medium. Operator forgetting to tune defeats the
  purpose — same failure mode we have today.

**Recommendation:** ship Level A first. Most direct fix for the
"calibration keeps failing" pain. Add Level B (60 s skip) in the
same six-day cycle as polish — it's small and removes the wait
for the common re-cal case.

---

## 4. Implementation breakdown (Level A + B)

All work in `src_hmi/main.cpp` unless noted.

### 4.1 Extend `CalOverlay` with a sub-phase + tracked autotune state
- Add `int8_t subPhase` to `CalOverlay`:
  - `0 = TUNING` (new)
  - `1 = CAL` (existing RUNNING behavior)
- Add `uint32_t lastAutotuneMs[2]` for Level B's 60 s skip.
- Add `int8_t lastAutoStep[2]` so we detect autoDone 0→1 edge.

### 4.2 Add `autoStep`, `autoTotal`, `autoDone` to `CamState`
- Mirror the pattern from existing autoStage extraction in `pollCam`.
- New fields: `int autoStep`, `int autoTotal`, `bool autoDone`.

### 4.3 Rewrite the CAL A/B action handler (~`src_hmi/main.cpp:705`)
```cpp
case ACT_CAL_A: {
    bool recentlyTuned = (millis() - calOverlay.lastAutotuneMs[0]) < 60000;
    if (recentlyTuned) {
        httpKick(String("http://") + camA.ip + "/calibrate");
        calOverlay.subPhase = 1;  // skip TUNING
    } else {
        httpKick(String("http://") + camA.ip + "/stop");   // ensure IDLE
        httpKick(String("http://") + camA.ip + "/autotune");
        calOverlay.subPhase = 0;  // TUNING
    }
    calOverlay.side = 0;
    calOverlay.phase = 0;
    // … rest as today …
    requestCalSnapshot(camA.ip);
    break;
}
```

### 4.4 Update `updateCalOverlay` to handle the TUNING → CAL transition
- When `subPhase == 0` (TUNING) AND `c.autoDone == 1`:
  - Record `lastAutotuneMs[side] = millis()`.
  - Fire `/calibrate` via `httpKick`.
  - Set `subPhase = 1` (CAL).
  - Refresh snapshot (cam's autotune leaves a snapshot too).
- Existing OK!/FAILED detection runs in `subPhase == 1` only.
- New watchdog: if `subPhase == 0` AND `(millis() - startedMs) > 20000`,
  flip to RESULT_FAIL with `calMsg = "Auto-tune travou"` — covers the
  case where the cam never reports `autoDone`.

### 4.5 Update `drawCalOverlay` rendering
- When `phase == 0` AND `subPhase == 0`:
  - Title: "Calibrando câmera A/B — auto-ajuste"
  - Subtitle: "Detectando melhor exposição"
  - Progress bar driven by `c.autoStep / c.autoTotal` (vs the
    coarse 3-stop bar we use during CAL)
  - Step counter: "passo 7/15"
- When `phase == 0` AND `subPhase == 1`: existing CAL rendering.
- Cancelar handler unchanged — `/stop` works during both phases.

### 4.6 Tests
- `tests/integration/test_system.py` already covers the chained-cal
  smoke (test_camera_autotune ends with a `/calibrate` after the
  sweep). No new assertions needed for the cam-side contract.
- Add a manual-test note in this plan's pickup section for the
  HMI-side behavior (tap CAL → see TUNING progress → CAL → result).

### 4.7 Optional: Cancelar during TUNING should also reset autoStage
- The cam's `requestAutotune()` checks `gameState == STATE_IDLE`;
  if Cancelar fires `/stop` mid-sweep we should make sure the
  next CAL tap can start fresh. Test by tapping Cancelar then CAL
  again quickly.

---

## 5. Acceptance criteria

| # | Criterion | How to verify |
|---|---|---|
| AC1 | Tap CAL A → overlay shows "auto-ajuste" with a progress bar that climbs from passo 1/15 to passo 15/15 over ~6 s. | Manual |
| AC2 | After autotune completes, title flips to "Calibrando Lado A" and the existing OK!/FAILED detection fires within ~1 s. | Manual |
| AC3 | RESULT phase shows learned thresholds (existing UX). | Manual |
| AC4 | Second CAL A tap within 60 s skips autotune and goes straight to CAL (Level B). | Tap CAL A, wait result, tap CAL A again immediately — should be fast. |
| AC5 | Second CAL A tap after 90 s does autotune again. | Tap CAL A, wait result, wait 90 s, tap CAL A — should re-tune. |
| AC6 | Cancelar mid-autotune cleanly resets cam to IDLE; next CAL A tap can start fresh. | Tap CAL A, hit Cancelar at passo 5/15, tap CAL A again → should start fresh autotune. |
| AC7 | All 70 baseline integration tests + the 16 `--with-autotune` tests still pass. | `python3 tests/integration/test_system.py --with-autotune` |
| AC8 | HMI's `/status` stays responsive during a partida (no regression from the 06-07 stability fix). | 90 s × 1 Hz triple-board soak still shows HMI ≥ 80/90. |

---

## 6. Risks / open questions

- **What if autotune diverges (low light, no contrast)?**
  `autoBestScore < 50000` triggers a special calMsg ("low edge
  content") in the cam. The HMI should still flip phase to
  RESULT_FAIL on that — currently the OK!/FAILED detection only
  triggers on `calMsg` prefixes, and that prefix is something like
  "Auto-tune done — low edge content (score N)". Either:
  - Add a third prefix detection for "Auto-tune done — low".
  - Or have the HMI inspect `calContrast` < threshold at end of
    cal and force RESULT_FAIL.
- **Race between `/stop` and `/autotune` http kicks.** The cam's
  worker queue is serial so they execute in order, but the queue
  is shared across sides. If `httpKick` order can't be guaranteed,
  consider chaining via state polling instead.
- **Cam B intermittent dropouts (logged 2026-06-03).** If cam B
  drops mid-autotune, the HMI overlay watchdog (4.4) will flip to
  RESULT_FAIL after 20 s. UX-wise that's fine but the operator
  loses 20 s vs the 3 s today. Worth noting.
- **Should Level B's 60 s window be configurable?** Default 60 s
  matches typical re-cal cadence (operator nudges dadinho, retries
  within a minute). A future setting screen could expose it but
  isn't blocking.

---

## 7. Pickup steps

1. Confirm cam-side state by running
   `python3 tests/integration/test_system.py --with-autotune` →
   all 16 autotune checks should still pass green.
2. Add `autoStep` / `autoTotal` / `autoDone` to `CamState` and
   extend `pollCam` to populate them.
3. Add `subPhase` + `lastAutotuneMs` to `CalOverlay`.
4. Rewrite ACT_CAL_A / ACT_CAL_B branches in `runAction()`.
5. Wire the TUNING → CAL transition in `updateCalOverlay()`.
6. Update `drawCalOverlay()` to render the new sub-phase.
7. Build + flash + manually verify all 8 ACs.
8. Run the integration suite (+ `--with-autotune`) → confirm no
   regression.
9. Commit + push.

---

## 8. Not in scope

- Cam-side firmware changes. `/autotune` already does the right
  thing; chaining is pure HMI orchestration.
- Persisting tuned values across reboots. Each cam tunes per power
  cycle. If the operator restarts mid-partida, the cam re-tunes on
  the next CAL.
- Showing the live MJPEG during the autotune sweep. That's
  `.plans/live-stream-preview.md`; it'd be the natural polish on
  top of this plan (the autotune progress bar plus a live feed of
  the cam picking its exposure is a great UX).
- The Cam B intermittent-dropout investigation (open since
  2026-06-03).
- The residual flicker in the snap area between fetches (also
  punted from earlier sessions).
