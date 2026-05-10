# Self-Improvement Plan — 2026-05-09

User asked: "create a plan to self-improve this continuously and only stop when
the program is working. Feel free to refactor the entire project and interface."

## Diagnosis: why we keep firefighting

Each recent change added load to the same single-threaded ESP camera+stream
pipeline. The auto-tune sweep monopolises the loop task for 5-10 s, and the
RGB565 colored-overlay encoder (added for the colored ROI) doubled the
per-frame CPU cost. The two together starve the MJPEG stream so the browser
just sees a static frame for the duration of the sweep — what the user calls
"freezing".

Fixing it incrementally hasn't worked because the **architecture is wrong**:
visual overlays (yellow ROI, green dadinho box) belong on the **client**, not
on the firmware. The firmware should serve the simplest, fastest possible
camera frame, and the dashboard should draw on top of it with HTML/SVG.

## Acceptance criteria — "working"

1. Live MJPEG stream stays smooth (>= 8 fps) at all times, including during
   auto-tune.
2. ROI rectangle is **always** visible (yellow) regardless of B&W threshold.
3. When the dadinho is detected during play, a **limegreen** bounding box
   appears around it in real time.
4. Auto-tune runs in <= 3 s and visibly improves contrast.
5. After auto-tune, sliders show the chosen values; live stream resumes
   immediately.
6. Goal sound (whistle) plays without freezing the device.
7. Both Training and Match dashboards work end-to-end.

## Refactor plan

### Phase 1 — Move overlays to the client (the big one)

**Why:** the firmware is doing pixel-level rendering it shouldn't. SVG over
the `<img>` gives us crisp colored overlays for free with zero firmware CPU.

**Changes:**

- `main.cpp`: drop `encodeFrameWithOverlays`, `grayToRgb565BE`,
  `drawROIColor`, `drawBboxColor`. Restore `drawROI` to a simple white
  rectangle on the grayscale buffer (only used for the calibration snapshot
  fallback). Stream uses plain `frame2jpg(fb, 70, …)` again — cheap, ~10 ms.
- `main.cpp`: track `lastDiceX/Y/W/H` (most recent detected dadinho bbox) and
  `lastDiceDetected`. Expose in `/status`.
- `web_stream.cpp`: add the new bbox fields to the JSON.
- `training_dashboard.h` / `match_dashboard.h`: wrap the `<img>` in a
  positioned container, add an absolutely-positioned SVG over it. JS reads
  `roiX/roiY/roiW/roiH` and `diceBboxX/Y/W/H` from `/status` and updates SVG
  rectangles every poll. Yellow stroke for ROI, limegreen for dadinho.
- Remove the 150 KB RGB565 buffer; we don't need it anymore.

**Result:** firmware encoder is back to grayscale-only fast path. No fmt2jpg.
Stream throughput restored.

### Phase 2 — Keep stream alive during auto-tune

**Why:** even with cheap encoding, `runAutotune` blocks `loop()` for several
seconds. During that block, no frames flow. We need to push a frame
periodically so the browser doesn't perceive a freeze.

**Changes:**

- In `runAutotune`'s `sweepParam` helper, push the *current* frame to
  `frameStore` once per parameter sweep (so ~16 frames spread across the
  whole sweep, ~one every 300-500 ms). This uses the frame already captured
  for scoring — no extra cost.
- Display an "Auto-tuning…" badge in the dashboard during the sweep so the
  user knows progress is intentional.

### Phase 3 — Trim auto-tune

**Why:** 8 parameters × 2 passes is overengineered and slow. Most of the
contrast comes from gain + exposure + brightness.

**Changes:**

- Single pass.
- Sweep gain (4 values), gceil (3), aec (5), bri (3) — drop con/sharp/gma/lenc.
  Total: 15 captures, ~2 s.
- Keep `applyCamSettings(*best)` re-application at end of each sweep so the
  sensor and `cur*` reflect the running best.

### Phase 4 — B&W threshold sanity

**Why:** the hard-cut binarisation produces all-white frames when Otsu picks
a low threshold, which hides everything including the dadinho. With overlays
moving to the client, this matters less, but still affects detection visuals.

**Changes:**

- Keep hard-cut semantics (it's what the user wants for "max contrast").
- Clamp Otsu's chosen threshold so it's not below 30 nor above 220 (avoid
  fully-saturated frames).
- Show the chosen value in the dashboard label: "B&W: 80 (auto)".

### Phase 5 — Cleanup

- Remove the heartbeat log spam now that we're confident loop() runs.
- Remove the warmup loop after auto-tune (no longer needed).
- Remove the 400 ms re-sync setTimeout (no longer needed once stream
  doesn't stall).
- Update `.about/feature-requests.md` to mark these features done.

### Phase 6 — Verify

- Compile, no warnings.
- Smoke-test by reading current state at the end.
- User uploads and confirms each acceptance criterion.

## Execution order

Phase 1 first (highest leverage). Build, ask user to upload + test. If freeze
is gone, continue Phase 2-5. If not, debug specifically before moving on.

Commit after each phase. Push at the end.
