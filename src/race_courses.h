#pragma once

#include <TinyGPSPlus.h>
#include <stdint.h>
#include <stddef.h>

// Device-side course library for instant practice ("menu → Start
// practice → pick course → set options → sail"). Courses arrive as
// wind-frame shapes (same JSON as the web builder) via the backend task;
// picking one places the course relative to the boat and creates a REAL
// session on the backend (so it shows on the web and the run uploads).
//
// Placement: the start line sits `distM` metres UPWIND of the boat, so the
// boat starts downwind and beats up through the line into a course that
// runs upwind from it (the line is the leeward edge of every shape).

#define COURSE_MAX 12
#define COURSE_MARKS 10

// Session options, chosen in the menu and remembered across reboots. The
// first-boot fallbacks are the constants below; after the first race the
// device reuses whatever was picked last.
struct PracticePrefs {
    long courseId = -1; // -1 = never started a practice session
    int gunSec = 30;     // countdown to the gun
    int distM = 20;      // boat -> start line, metres
};

void practicePrefsLoad();               // NVS -> RAM (call once at boot)
const PracticePrefs& practicePrefs();
void practicePrefsSetGun(int sec);     // cycles in the options screen
void practicePrefsSetDist(int m);
void practicePrefsSave(long courseId);  // called on confirm only
bool practicePrefsValid();              // a course was chosen before
// Offered countdown lengths (seconds) and boat->start-line distances (m).
// Fixed sizes so callers can cycle them without an out-of-line helper.
#define PRACTICE_GUN_N 5
#define PRACTICE_DIST_N 3
extern const int PRACTICE_GUNS[PRACTICE_GUN_N];  // 10, 30, 60, 120, 300
extern const int PRACTICE_DISTS[PRACTICE_DIST_N]; // 10, 20, 30

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
    char key[16] = {0}; // builtinKey, empty for user courses
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
bool courseHaveId(long id);      // is this course in RAM right now?

// Row the picker opens on: the remembered course, else the Windward-Leeward
// built-in (key "wl"), else the first one. DB ids are per-install (the seed
// is auto-increment), so the default keys off builtinKey, never off an id.
uint8_t coursePreselectIndex();

// Instantiate course i as the live session: place it relative to the boat,
// arm the gun at now+prefs.gunSec and hand the creation to the backend
// (POST /sessions). Needs a GPS fix and wall time; returns false with
// nothing changed otherwise. `boatCourse` is the fallback wind source when
// no station reports (-1 = none).
bool courseStartSession(uint8_t i, double boatLat, double boatLon, long nowEpoch, int boatCourse);

// Repeat the last practice session: ABANDON the current one, then re-create
// it with the remembered course/gun/distance, re-oriented from the boat.
bool courseRepeatSession(double boatLat, double boatLon, long nowEpoch, int boatCourse);

// End the live session (if any): raise the ABANDON signal on the backend so
// the web agrees, then forget it locally.
void courseAbandonSession();

// Wind to place a course with: session wind, else the venue wind from the
// health piggyback, else the boat's own bearing (it lies in the no-go
// angle, so its heading points at the wind source).
int practiceWindDir(int boatCourse);
int practiceWindSpeed();
