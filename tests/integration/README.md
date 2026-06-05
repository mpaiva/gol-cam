# gol-cam integration tests

Stdlib-only Python test suite that exercises the **live** REST contracts
between the cameras, the placar, and any future HMI surface. Run it any
time you change firmware that touches `/calibrate`, `/goal`, `/deduct`,
`/test-fire`, `/api/reset`, `/goal-undo`, or `/status`.

## Usage

```bash
# Full suite (~30s, requires all three boards online)
python3 tests/integration/test_system.py

# Quick subset (skips calibration trigger tests)
python3 tests/integration/test_system.py --quick

# Add the auto-tune sweep tests (opt-in — adds ~10-15 s per cam
# because the sweep walks 15 captures × ~400 ms each)
python3 tests/integration/test_system.py --with-autotune

# Single group
python3 tests/integration/test_system.py --only placar
python3 tests/integration/test_system.py --only cameras
python3 tests/integration/test_system.py --only cross

# Different IPs (e.g. you're on a different network)
python3 tests/integration/test_system.py \
    --placar 192.168.1.50 --cam-a 192.168.1.51 --cam-b 192.168.1.52
```

Exits 0 if all expected tests pass, non-zero on any failure.

## What's covered

- **placar** — `/status` shape, `/api/reset`, `/goal?side=a|b`,
  `/goal?side=x` (400 path), `/goal-undo` decrement + clamp-at-zero.
- **cameras (autotune, opt-in)** — `/autotune` is accepted, the
  sweep starts (`autoStage` goes positive), `autoDone` flips 0→1
  within 20 s, `state` returns to IDLE, `calMsg` starts with
  "Auto-tune", `autoScore > 0`, `autoGain` lands on a swept value,
  and a follow-up `/calibrate` is accepted in the freshly-tuned
  state (the path the HMI's CAL button will take once we wire
  autotune-before-calibrate).
- **cameras** — `/status` shape (including all the new motion/colour
  fields), `side` assignment matches `BOARD_ROLE`, `/reset` clears
  goalCount, `/deduct` clamps at 0, `/calibrate` transitions state.
- **cross-board** — `/test-goal` push lands on placar, `/test-fire`
  bumps cam goalCount + placar + sets `hasGoalSnap`, `/deduct` mirrors
  to placar via `/goal-undo` (the VAR-annul-sync feature).

## When a test goes red

The output names the specific check, the expected value, and what
actually came back. Common causes:

- **A board is offline** — reachability gate prints `DOWN`; affected
  suites get skipped.
- **A camera is running pre-`1bc6df8` firmware** — VAR-annul-sync
  fails (cam goalCount drops but placar stays bumped). Reflash the
  camera.
- **WiFi jitter / cam→placar push delay** — cross-board tests use a
  polling `assert_eventually` pattern with a 3 s window so transient
  delays don't false-fail. If you're still seeing flakes, increase the
  `timeout=` arg.
