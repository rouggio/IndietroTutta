#pragma once

#include <TinyGPSPlus.h>

// Mock GPS source for indoor testing: instead of the UART, fixes come from
// a backend scripted run (GET /sim/next) synthesized into NMEA sentences.
// The rest of the stack (screens, countdown, pass engine) can't tell.
bool gpsMockActive();       // cached NVS flag (5s refresh)
void gpsMockSet(bool on);   // portal toggle, persists to NVS
void gpsMockPoll(TinyGPSPlus& gps); // backend-task side, ~3s cadence
