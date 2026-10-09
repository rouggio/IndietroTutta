#pragma once

#include <TinyGPSPlus.h>

bool backendOnline();

// Ask the backend task to run the health poll immediately instead of
// waiting for the 30s tick (race screen manual resync).
void backendPollHealthNow();

// Ask the backend task to (re)fetch the template library (instant
// practice setup). Returns immediately; poll tplReady().
void backendFetchTemplates();

void backendInit(TinyGPSPlus* gps);

void backendLoop(TinyGPSPlus &gps);
