#pragma once

#include <TinyGPSPlus.h>
#include <stdint.h>
#include <stddef.h>

// Run state machine + pass detection + strict rules (Step 5) + committee
// signals (Step 7). Display-level crossing logic from Step 3 lives in the
// .cpp; Step 6 (results UI) reads the upload the device POSTs at finish.
//
// States per course, practice or race:
//   IDLE/PRE-START → RACING (start line crossed, or gun for point starts)
//   → marks advance by radius pass with required-side check (gates: either
//   buoy, no side check) → FINISHED (line cross or last-mark pass).
// Practice countdown is local (LL arms now+duration, R cycles 1/3/5 min,
// NVS-backed) unless a pushed session gun exists — then the session gun
// counts (coach-driven practice) until LL overrides locally;
// race guns come from the session. 360/720 are sailor-declared (LL/R while
// racing) and verified by heading-rotation integration. The run (splits +
// event log) uploads once at finish; committee signals apply idempotently.

void raceRunInit();   // load practice duration + signal cursor (NVS); call once at boot
void raceRunReset();  // course change / reset: clears run, keeps duration

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

void racePracticeStart(long gunEpoch); // LL in practice: gun = now + duration
void racePracticeCycleDur();           // R in practice: 60 → 180 → 300
long racePracticeDur();                // seconds

// --- Step 5: turns, events, upload -------------------------------------
void raceTurnDeclare(long now); // sailor logs a 360/720; verified by rotation
int raceTurnPoll();             // 0 none, 360/720 just verified (consumed)
bool raceTurnPending();
bool raceWrongPoll();           // true once per wrong-side call (consumed)

void raceLogEvent(const char* code, long t, const char* v); // e.g. SIG
uint8_t raceEventCount();
bool raceEventGet(uint8_t i, long* t, char* e, size_t esz, char* v, size_t vsz);

bool raceUploadPending(); // finished run (or result signal) awaiting POST
void raceUploadBody(const char* deviceId, char* buf, size_t n); // JSON payload
void raceUploadAck();       // backend accepted/rejected: stop retrying

// --- Step 7: committee signals ------------------------------------------
void raceApplySignal(const char* kind, const char* detail, long now);
bool raceSignalIsNew(long id); // idempotency cursor (per session, NVS-backed)
void raceSignalMark(long id);
