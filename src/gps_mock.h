#pragma once

#include <TinyGPSPlus.h>

// Mock GPS source for indoor testing: instead of the UART, fixes come from
// a backend scripted run (GET /sim/next) synthesized into NMEA sentences.
// The rest of the stack (screens, countdown, pass engine) can't tell.
//
// The feed is split in two on purpose:
//
//   gpsMockFeed()  backend task — refills the buffer. Blocking (HTTP),
//                  but only every ~20s.
//   gpsMockTick()  UI loop, every pass — releases ONE due fix from RAM.
//                  No I/O, so a fetch in flight can never stall the
//                  tick: the boat keeps moving on a truthful clock
//                  until the buffer is exhausted.
//
// They were one function and it did not work: the feed task was blocked
// by POST /gps for ~2s per loop, so the shared call ran ~3x less often
// than 1Hz. The queue head fell behind, the staleness guard dropped the
// backlog, and the mock track lost ~7 fixes every 20s. Ticking from the
// UI pass restores true 1Hz regardless of how slow the network is.
bool gpsMockActive();       // cached NVS flag (5s refresh)
void gpsMockSet(bool on);   // portal toggle, persists to NVS
void gpsMockFeed(TinyGPSPlus& gps); // backend-task side: refill buffer
void gpsMockTick(TinyGPSPlus& gps); // UI-loop side: release one due fix