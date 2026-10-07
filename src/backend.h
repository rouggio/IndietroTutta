#pragma once

#include <TinyGPSPlus.h>

bool backendOnline();

// Ask the backend task to run the health poll immediately instead of
// waiting for the 30s tick (race screen manual resync).
void backendPollHealthNow();

void backendInit();

void backendLoop(TinyGPSPlus &gps);

bool backendSendFlaggedPosition(TinyGPSPlus &gps, const char* uid);
bool backendEnqueueDeleteWaypoint(const char* uid);
