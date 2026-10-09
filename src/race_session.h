#pragma once

#include <TinyGPSPlus.h>
#include <stdint.h>

// Live race-session cache: what the device pulled from the backend
// (GET /health → session) plus the NVS-backed copy that survives reboot.
// Step 3: receive + cache + display. Pass/fail engine arrives in Step 5.

enum RaceMarkType : uint8_t {
    RaceMarkNone = 0,
    RaceMarkStart,
    RaceMarkSingle,
    RaceMarkGate,
    RaceMarkFinish
};

struct RaceMark {
    double lat = 0.0;
    double lon = 0.0;
    float r = 30.0f;
    char side = 'P';
    RaceMarkType type = RaceMarkNone;
    char gate[8] = {0};
};

struct RaceSeg {
    double latA = 0.0;
    double lonA = 0.0;
    double latB = 0.0;
    double lonB = 0.0;
    bool valid = false;
};

struct RaceSignal {
    long id = 0;
    char kind[8] = {0};   // OCS|DSQ|DNF|RET|SCP|RECALL|ABANDON
    char detail[32] = {0}; // e.g. SCP seconds
};

struct RaceSession {
    bool valid = false;
    long sessionId = 0;
    char mode[9] = {0}; // "practice" | "race"
    char status[10] = {0}; // "scheduled" | "live"
    long startTime = 0;  // UTC epoch of gun (without pursuit offset), 0 = none
    long startOffsetSec = 0;
    int windDir = 0;
    int windSpeed = 0; // knots (0 = unknown)
    // Venue wind from the health top-level "wind" piggyback (backend
    // computes it at the boat's last stored position). RAM only — never
    // persisted; the UI and practice placement fall back to it when the
    // assigned session carries no wind of its own. 0 speed = unknown.
    int envWindDir = 0;
    int envWindSpeed = 0;
    int courseVersion = 0;
    uint8_t markCount = 0;
    RaceMark marks[10];
    RaceSeg startLine;
    RaceSeg finishLine; // mirror of startLine when finishSameAsStart
    bool finishSameAsStart = false;
    uint8_t sigCount = 0; // committee signals, oldest first (RAM only)
    RaceSignal signals[8];
};

// Live copy. Written by the backend task, read by the UI thread.
extern RaceSession raceSession;

// NVS ("race" namespace) load at boot / save on change.
bool raceSessionLoad();
bool raceSessionSave();

// Parse a GET /health body. Updates raceSession always (startTime can move
// without a version bump); writes NVS only when something changed.
// Explicit `"session":null` while online clears a stale *backend* course
// (unassigned ≠ unreachable); local practice (id -1) is never auto-cleared.
// Returns true when the live session changed.
bool raceSessionParse(const char* healthBody);

// UTC epoch from GPS date/time. -1 when the fix has no usable date/time.
// (TinyGPS++ getters are non-const, so this takes a mutable reference.)
// Also calibrates the wall clock (see raceWallEpoch).
long raceGpsEpoch(TinyGPSPlus& gps);

// Wall-clock UTC epoch: GPS-calibrated once any fix with time was seen,
// then ticks with millis() through fix gaps (mock end, tunnels, page sits
// on stale data). -1 when never calibrated — countdowns show NO TIME.
long raceWallEpoch();

// UTC epoch from "YYYY-MM-DDTHH:MM:SS[.mmm]Z". -1 on parse failure.
long raceIsoEpoch(const char* iso);

// True when the committee has this session live (fast health poll).
bool raceSessionLive();
