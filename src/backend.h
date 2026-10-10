#pragma once

#include <TinyGPSPlus.h>

bool backendOnline();

// Ask the backend task to run the health poll immediately instead of
// waiting for the 30s tick (race screen manual resync).
void backendPollHealthNow();

// Ask the backend task to (re)fetch the course library (practice setup).
// Returns immediately; poll courseReady().
void backendFetchCourses();

// --- Device-started practice sessions ------------------------------------
// The UI thread hands over the placement; the task POSTs it and stores the
// new id. Poll backendSessionCreated() (true once per finished attempt).
void backendCreateSession(long courseId, long startEpoch, double originLat,
                          double originLon, int windDir, int windSpeed);
bool backendSessionCreated(long* sessionId); // false while in flight
void backendAbandonSession(long sessionId);   // raises the ABANDON signal

// True while the last successful health poll is recent enough (<60s) to
// trust the network. Gates the race screen entry.
bool backendOnlineFresh();

void backendInit(TinyGPSPlus* gps);

void backendLoop(TinyGPSPlus &gps);
