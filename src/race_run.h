#pragma once

#include <TinyGPSPlus.h>
#include <stdint.h>

// Run state machine + pass detection (Step 4). Display-level crossing logic
// from Step 3 lives here now; Step 5 adds side judgments and fail calls.
//
// States per course, practice or race:
//   IDLE/PRE-START → RACING (start line crossed, or gun for point starts)
//   → marks advance by radius pass (gates: either buoy) → FINISHED.
// Practice countdown is local (R starts, LL cycles 1/3/5 min, NVS-backed);
// race guns come from the session. Splits are RAM-only (upload in Step 6).

void raceRunInit();   // load practice duration (NVS); call once at boot
void raceRunReset();  // course change / RR: clears run, keeps duration

void raceRunUpdate(TinyGPSPlus& gps); // call every pass with a fix

bool raceRunStarted();    // crossed and racing
bool raceRunFinished();
bool raceRunOcs();        // crossed early, awaiting proper re-cross
long raceRunStartEpoch(); // gun-cross moment, 0 when none
long raceRunEndEpoch();   // finish moment, 0 when unfinished
long raceGunEpoch();      // effective gun (race session or practice), 0 if none

uint8_t raceProgIdx();   // current target mark index
uint8_t racePassCount();
long raceSplit(uint8_t i); // seconds since start at pass i, -1 when unset

void racePracticeStart(long gunEpoch); // R in practice: gun = now + duration
void racePracticeCycleDur();           // LL in practice: 60 → 180 → 300
long racePracticeDur();                // seconds
