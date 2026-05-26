#pragma once

#include <Arduino.h>

// =============================================================
// PlacarWeb — HTTP server for the scoreboard board.
// Exposes:
//   GET  /         HTML page (auto-refresh 2s, mobile-first)
//   GET  /a+ /b+   increment score (redirects to /)
//   GET  /az /bz   reset side (redirects to /)
//   GET  /reset    reset both (redirects to /)
//   GET  /status   JSON {"role":"scoreboard","a":N,"b":M}
//   POST /sync     accept {"a":N,"b":M} — set absolute scores (idempotent)
//   POST /goal     accept {"side":"A"|"B"} — increment one side (push from camera)
// =============================================================

void placarWebBegin();
void placarWebHandle();   // call from loop()
