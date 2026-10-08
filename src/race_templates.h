#pragma once

#include <TinyGPSPlus.h>
#include <stdint.h>
#include <stddef.h>

// Device-side template library for instant practice ("menu → Start
// practice → pick template → sail in 10s"). Templates arrive as wind-frame
// shapes (same JSON as the web builder) via the backend task; picking one
// instantiates a RAM-only local session (sessionId -1: nothing uploads,
// NVS untouched) with the start line 20m upwind of the boat and a
// now+10s gun. Committee pushes still override on arrival.

#define TPL_MAX 12
#define TPL_MARKS 10

struct TplMark {
    float x = 0.0f, y = 0.0f; // wind-frame meters (+y = upwind)
    float r = 30.0f;
    char side = 'P';
    uint8_t type = 0; // RaceMarkType value (None/Start/Single/Gate/Finish)
    char gate[8] = {0};
};

struct TplSeg {
    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    bool valid = false;
    bool sameAsStart = false; // finish only
};

struct Tpl {
    bool used = false;
    long id = 0;        // DB id, 0 = OOTB preset
    char key[16] = {0}; // preset key (OOTB only)
    char name[33] = {0};
    uint8_t markCount = 0;
    TplMark marks[TPL_MARKS];
    TplSeg startLine;
    TplSeg finishLine;
};

// Fetch state (backend task context writes, UI thread reads).
void backendFetchTemplates(); // ask the backend task to (re)fetch; returns immediately
void tplFetch();              // blocking fetch+parse (backend task only)
bool tplReady();              // fetch completed (possibly zero templates)
uint8_t tplCount();
const Tpl* tplGet(uint8_t i);

// Instantiate template i as the live local session. Needs a GPS fix (for
// the 20m-upwind placement) and wall time (for the +10s gun). Wind comes
// from the cached session, else 0 (N). Returns false with nothing changed
// when there is no fix/time.
bool tplStartSession(uint8_t i, double boatLat, double boatLon, long nowEpoch);

// Repeat the live course rigid-shifted so its start reference sits 20m
// upwind of the boat, with a fresh +10s gun. Local only, like Start.
bool tplRepeatSession(double boatLat, double boatLon, long nowEpoch);
