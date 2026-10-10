#ifndef ADBLOCK_DASHBOARD_H
#define ADBLOCK_DASHBOARD_H

#include <Arduino.h>

// ── E-paper dashboard ────────────────────────────────────────
// Renders the block statistics onto the 768x552 four-colour panel.
//
// The picture is never materialised as a whole image: epd_driver
// streams the panel one band at a time and calls back here for each
// band, so both screens below redraw themselves once per band. See
// epd_driver.h for the band contract and display.h for the canvas
// helpers that clip drawing to the active band.
//
// Colour semantics (fixed by design):
//   black  — data / labels
//   red    — today's blocking headline figure, alarms
//   yellow — secondary highlights, ranking accents
//   white  — background

// Full dashboard: headline counters, top-blocked list, hourly bars and
// a footer with uptime and last-refresh time. Flushes the panel.
void dashboardDraw();

// Simple message screen, used for boot / errors / setup. Lines are
// copied, so they need not outlive the call. Flushes the panel.
void dashboardMessage(const char* line1, const char* line2, const char* line3);

#endif // ADBLOCK_DASHBOARD_H
