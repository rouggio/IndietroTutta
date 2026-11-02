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
void backendAbandonSession(long sessionId);   // raises the ABANDON signal (retries)
// True ONCE when the ABANDON could not be delivered after its retries: the
// backend still has the session live, so the boat stays blocked on the web.
bool backendAbandonFailed();

// True while the last successful health poll is recent enough (<60s) to
// trust the network. Gates the race screen entry.
bool backendOnlineFresh();

// True ONCE after a newly assigned session arrived (todo 30). The UI loop
// asks and, if the sailor is on MAIN, opens the race screen for them.
bool backendTakeRacePageRequest();

void backendInit(TinyGPSPlus* gps);

void backendLoop(TinyGPSPlus &gps);

// Bytes still unused on the backend task's stack at the worst moment since
// boot (FreeRTOS high-water mark). -1 before the task exists. Reported by
// /status: a 2KB run-upload body and a ServerLink live on that stack, so this
// is the number that says whether a reboot was our own stack overflow.
int backendStackFree();

// Same, for the task that owns the health poll and the pending-run upload -
// the upload runs THERE (see backendInit). This is the one to watch at a race
// finish.
int healthStackFree();
