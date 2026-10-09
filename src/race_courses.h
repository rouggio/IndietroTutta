#pragma once

#include <TinyGPSPlus.h>
#include <stdint.h>
#include <stddef.h>

// Device-side course library for instant practice ("menu → Start
// practice → pick course → sail in 10s"). Courses arrive as wind-frame
// shapes (same JSON as the web builder) via the backend task; picking one
// instantiates a RAM-only local session (sessionId -1: nothing uploads,
// NVS untouched) with the start line 20m upwind of the boat and a
// now+10s gun. Committee pushes still override on arrival.

#define COURSE_MAX 12
#define COURSE_MARKS 10

struct CourseMark {
    float x = 0.0f, y = 0.0f; // wind-frame meters (+y = upwind)
    float r = 30.0f;
    char side = 'P';
    uint8_t type = 0; // RaceMarkType value (None/Start/Single/Gate/Finish)
    char gate[8] = {0};
};

struct CourseSeg {
    double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
    bool valid = false;
    bool sameAsStart = false; // finish only
};

struct Course {
    bool used = false;
    long id = 0;        // DB id (every course is a row, built-ins included)
    char name[33] = {0};
    uint8_t markCount = 0;
    CourseMark marks[COURSE_MARKS];
    CourseSeg startLine;
    CourseSeg finishLine;
};

// Fetch state (backend task context writes, UI thread reads).
void backendFetchCourses(); // ask the backend task to (re)fetch; returns immediately
void courseFetch();              // blocking fetch+parse (backend task only)
bool courseReady();              // fetch completed (possibly zero courses)
uint8_t courseCount();
const Course* courseGet(uint8_t i);

// Instantiate course i as the live local session. Needs a GPS fix (for
// the 20m-upwind placement) and wall time (for the +10s gun). Wind comes
// from the cached session, else 0 (N). Returns false with nothing changed
// when there is no fix/time.
bool courseStartSession(uint8_t i, double boatLat, double boatLon, long nowEpoch);

// Repeat the live course rigid-shifted so its start reference sits 20m
// upwind of the boat, with a fresh +10s gun. Local only, like Start.
bool courseRepeatSession(double boatLat, double boatLon, long nowEpoch);
