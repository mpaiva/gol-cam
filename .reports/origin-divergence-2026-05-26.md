---
date: 2026-05-26
prompt: "please check this repo agains origin and evaluate what needs to be updated."
status: evaluation only — no code changes made
---

# Origin Divergence Evaluation — 2026-05-26

## TL;DR

`master` and `origin/master` have **diverged** since `ab35f14` (PR #1 merge):

- **Local (13 commits, May 9–10):** UI redesign, scoreboard *integration & hardware debugging* — battery operation, brightness, WiFi TX-power tuning, VSYNC overflow fix, HTTPD stack-overflow fix, ROI auto-tune.
- **Remote (12 commits, May 9 + May 24):** Repo *consolidation* — subtree-merged the Placar Eletronico history, imported 3D models under `hardware/`, dual-environment `platformio.ini`, modular `placar_*.cpp` files, new `README.md` (EN + pt-BR), updated `CLAUDE.md`, `.gitignore`, scoreboard-side push from camera.

**Both sides have value.** Local has hardware-tested fixes; remote has cleaner architecture and missing docs/3D assets. Neither should be discarded.

## Divergence point

```
merge-base: ab35f14  Merge pull request #1 from mpaiva/edge-detection-grayscale
```

## Conflicting files (5)

| File | Local Δ | Remote Δ | Nature of conflict |
|---|---|---|---|
| `load_env.py` | +3 / −1 | +1 / −1 | Both add scoreboard env vars. Local set is a superset (adds `SCOREBOARD_SIDE`, `SCOREBOARD_STATIC_IP`, etc.). Remote adds `CAMERA_IP`. **Trivial reconcile.** |
| `platformio.ini` | Adds env `scoreboard` w/ single-file `src_scoreboard/scoreboard.cpp` | Adds env `placar` w/ modular `src/placar_*.cpp` + `-DBOARD_ROLE_*` macros | **Architectural conflict** — same goal, two layouts. |
| `src/main.cpp` | +418 / −89 (battery push, ROI auto-tune, side resolution, scoreboard.cpp glue) | +58 (own `goalPushTask` w/ POST JSON) | Two implementations of "push goal to scoreboard". Local uses `GET /goal?side=`; remote uses `POST /goal {"side":"A"}`. |
| `src/web_stream.cpp` | +61 / −29 (stack-overflow fix, memory stats in `/status`) | +5 / −2 (adds `"side"` field to `/status`) | Local is the bigger / more critical change; remote's `side` field can be re-applied on top. |
| `src/match_dashboard.h` | +317 / −240 (full UI redesign — hierarchy, expert toggle, client-side overlays) | +36 / −8 (adds Placar IP field + status polling) | Local rewrote large blocks; remote added a Placar-status row. Remote additions need to be re-grafted into the redesigned UI. |

## Non-conflicting additions on remote that we should pull in regardless

These don't touch local files and are pure additions:

- `README.md` + pt-BR translation
- `hardware/` (caixa-câmera STL/3MF, trave, acessórios, histórico) — **real assets, no local equivalent**
- `_import_placar/` (subtree-merged git history of the Placar Eletronico repo)
- `.gitignore` — adds `.claude/`
- `.plans/consolidation-plan.md`, `.plans/session-2026-05-24.md`
- `CLAUDE.md` edits (documents the dual-env layout)
- `.about/feature-requests.md` adds F6/F7 (Placar + Standalone)

## Non-conflicting local additions remote doesn't have

- `.plans/scoreboard-integration-2026-05-10.md`, `.plans/auto-contrast-tune.md`, `.plans/self-improvement-2026-05-09.md`
- `src_scoreboard/scoreboard.cpp` (421 LOC, battery-debugged firmware)
- Deletion of `include/gol_sound*.h` (legacy audio headers)
- `include/camera.h` tweaks

## Key risk: scoreboard firmware

Remote's `src/main_placar.cpp` + `placar_*.cpp` total **448 lines** of clean port from the Arduino sketch. Local's `src_scoreboard/scoreboard.cpp` is **421 lines** with hardware-debugged fixes the remote does *not* have:

- `WiFi.setSleep(WIFI_PS_MIN_MODEM)` — fixes battery operation
- `WiFi.setTxPower(...)` — softens current spikes
- `NORMAL_BRIGHTNESS = 8` chosen after testing brightness 13 caused power-bank cutoff
- Keep-alive pulse so phone power banks don't auto-shut-off
- Static-IP commit so power-cycles don't break gol-cam pushes
- Comments documenting the 1000 µF cap wiring

**Dropping local's scoreboard.cpp loses real debugging work.** Conversely, keeping it as-is means we ignore remote's modular split.

## Recommended path forward

**Option A (recommended) — Merge, favor local code + remote structure**

1. `git merge origin/master` (creates 5 conflicts).
2. Resolve:
   - `load_env.py`: union of both sets of env vars.
   - `platformio.ini`: keep remote's dual-env skeleton, rename env `placar` → also accept `scoreboard`, or just rename local's `src_scoreboard/` to fit remote's `src/placar_*.cpp` layout.
   - `src/main.cpp`: keep local entirely; drop remote's `goalPushTask` (local has its own, more refined version).
   - `src/web_stream.cpp`: keep local; re-apply remote's 3-line `"side"` field addition to the JSON.
   - `src/match_dashboard.h`: keep local UI; re-graft remote's Placar IP field + `pollPlacar()` into the redesigned layout.
3. Decide: port local's `scoreboard.cpp` hardware fixes into remote's modular `placar_*.cpp` files **OR** keep `src_scoreboard/scoreboard.cpp` and adjust `platformio.ini` env to point to it. The first is cleaner; the second is faster.

Estimated effort: **2–4 hours** including a clean build + smoke test on the camera board (scoreboard hardware needed for full validation).

**Option B — Rebase local onto origin/master**

Replay each of the 13 local commits on top. Cleaner history but the platformio/main/dashboard conflicts will fire multiple times. Not recommended unless we want a linear log.

**Option C — Push local, abandon remote (NOT recommended)**

Would discard the hardware/ 3D models, README, subtree-merged history, and consolidation work. Don't do this.

## Action items

- [ ] User picks A/B/C above.
- [ ] If A: create `.plans/origin-merge-2026-05-26.md` with the per-file resolution script.
- [ ] Run `pio run -e dfr1154` after merge to confirm camera still builds.
- [ ] Run `pio run -e placar` (or `-e scoreboard`) to confirm second env builds.
- [ ] Smoke-test camera on actual hardware before pushing.
