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

struct RaceSession {
    bool valid = false;
    long sessionId = 0;
    char mode[9] = {0}; // "practice" | "race"
    long startTime = 0;  // UTC epoch of gun (without pursuit offset), 0 = none
    long startOffsetSec = 0;
    int windDir = 0;
    int courseVersion = 0;
    uint8_t markCount = 0;
    RaceMark marks[10];
    RaceSeg startLine;
    RaceSeg finishLine; // mirror of startLine when finishSameAsStart
    bool finishSameAsStart = false;
};

// Live copy. Written by the backend task, read by the UI thread.
extern RaceSession raceSession;

// NVS ("race" namespace) load at boot / save on change.
bool raceSessionLoad();
bool raceSessionSave();

// Parse a GET /health body. Updates raceSession always (startTime can move
// without a version bump); writes NVS only when something changed.
// Returns true when the live session changed.
bool raceSessionParse(const char* healthBody);

// UTC epoch from GPS date/time. -1 when the fix has no usable date/time.
// (TinyGPS++ getters are non-const, so this takes a mutable reference.)
long raceGpsEpoch(TinyGPSPlus& gps);

// UTC epoch from "YYYY-MM-DDTHH:MM:SS[.mmm]Z". -1 on parse failure.
long raceIsoEpoch(const char* iso);
