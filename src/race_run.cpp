#include "race_run.h"
#include "race_session.h"
#include "serial_buffer.h"

#include <Preferences.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include <string.h>

static const float PASS_HYST_M = 5.0f;   // exit radius = r + hyst
static const float CROSS_RANGE_M = 150.0f; // line flips count inside this band
static const long PRACTICE_DURS[] = {60, 180, 300};

static uint8_t progIdx = 0;
static bool started = false;
static bool finished = false;
static bool ocs = false;
static long startEpoch = 0;
static long endEpoch = 0;
static long splits[10] = {0};
static bool splitSet[10] = {false};
static long practiceGun = 0;
static long practiceDurSec = 300;

// Start-line side tracking.
static int lastSide = 0;
static int gunSide = 0;
static bool hasGunSide = false;
// Finish-line side tracking.
static int finLastSide = 0;
// Pass tracking for the current target.
static bool wasInside = false;

void raceRunInit()
{
    Preferences prefs;
    if (prefs.begin("race", true)) {
        const long d = (long)prefs.getInt("pdur", 300);
        prefs.end();
        for (unsigned i = 0; i < sizeof(PRACTICE_DURS) / sizeof(PRACTICE_DURS[0]); i++) {
            if (d == PRACTICE_DURS[i]) {
                practiceDurSec = d;
                break;
            }
        }
    }
}

void raceRunReset()
{
    progIdx = 0;
    started = false;
    finished = false;
    ocs = false;
    startEpoch = 0;
    endEpoch = 0;
    lastSide = 0;
    gunSide = 0;
    hasGunSide = false;
    finLastSide = 0;
    wasInside = false;
    practiceGun = 0;
    for (uint8_t i = 0; i < 10; i++) {
        splits[i] = 0;
        splitSet[i] = false;
    }
}

bool raceRunStarted() { return started; }
bool raceRunFinished() { return finished; }
bool raceRunOcs() { return ocs; }
long raceRunStartEpoch() { return startEpoch; }
long raceRunEndEpoch() { return endEpoch; }
uint8_t raceProgIdx() { return progIdx; }
uint8_t racePassCount()
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < 10; i++) if (splitSet[i]) n++;
    return n;
}
long raceSplit(uint8_t i)
{
    if (i >= 10 || !splitSet[i]) return -1;
    return splits[i];
}

static bool isPractice()
{
    return !(raceSession.valid && strcmp(raceSession.mode, "race") == 0);
}

long raceGunEpoch()
{
    if (!raceSession.valid) return 0;
    if (!isPractice()) {
        if (raceSession.startTime <= 0) return 0;
        return raceSession.startTime + raceSession.startOffsetSec;
    }
    return practiceGun;
}

void racePracticeStart(long gunEpoch)
{
    if (!isPractice()) return;
    practiceGun = gunEpoch;
}

void racePracticeCycleDur()
{
    if (!isPractice()) return;
    unsigned n = sizeof(PRACTICE_DURS) / sizeof(PRACTICE_DURS[0]);
    unsigned i = 0;
    for (; i < n; i++) {
        if (practiceDurSec == PRACTICE_DURS[i]) break;
    }
    practiceDurSec = PRACTICE_DURS[(i + 1) % n];
    Preferences prefs;
    if (prefs.begin("race", false)) {
        prefs.putInt("pdur", (int)practiceDurSec);
        prefs.end();
    }
}

long racePracticeDur() { return practiceDurSec; }

// Signed side of directed segment A→B for point P (ENU meters, boat frame).
static int segSide(double ax, double ay, double bx, double by)
{
    // P is the origin here (boat-relative coords).
    const double dx = bx - ax, dy = by - ay;
    const double cross = dx * (-ay) - dy * (-ax);
    if (cross == 0.0) return 0;
    return cross > 0.0 ? 1 : -1;
}

static void toBoatFrame(double lat, double lon, double plat, double plon,
                        double& ex, double& ey)
{
    const double cosLat = cos(lat * M_PI / 180.0);
    ex = (plon - lon) * 111320.0 * cosLat;
    ey = (plat - lat) * 111320.0;
}

// Meters from the boat to a segment. -1 when the segment is invalid.
static double segDistM(double lat, double lon, const RaceSeg& s)
{
    if (!s.valid) return -1.0;
    double ax, ay, bx, by;
    toBoatFrame(lat, lon, s.latA, s.lonA, ax, ay);
    toBoatFrame(lat, lon, s.latB, s.lonB, bx, by);
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 1.0) {
        t = -(ax * dx + ay * dy) / len2;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    const double nx = ax + t * dx, ny = ay + t * dy;
    return sqrt(nx * nx + ny * ny);
}

static int startSegSide(double lat, double lon)
{
    if (!raceSession.startLine.valid) return 0;
    const RaceSeg& s = raceSession.startLine;
    double ax, ay, bx, by;
    toBoatFrame(lat, lon, s.latA, s.lonA, ax, ay);
    toBoatFrame(lat, lon, s.latB, s.lonB, bx, by);
    return segSide(ax, ay, bx, by);
}

static bool finishIsLine(const RaceSeg*& seg)
{
    if (raceSession.finishSameAsStart) {
        if (!raceSession.startLine.valid) return false;
        seg = &raceSession.startLine;
        return true;
    }
    if (!raceSession.finishLine.valid) return false;
    seg = &raceSession.finishLine;
    return true;
}

// Index after the gate pair containing i (pairs advance together).
static uint8_t gatePairEnd(uint8_t i)
{
    if (i >= raceSession.markCount) return i;
    const RaceMark& m = raceSession.marks[i];
    if (m.type != RaceMarkGate || !m.gate[0]) return i;
    uint8_t end = i;
    for (uint8_t k = 0; k < raceSession.markCount; k++) {
        if (raceSession.marks[k].type == RaceMarkGate && m.gate[0] &&
            strcmp(raceSession.marks[k].gate, m.gate) == 0 && k > end) {
            end = k;
        }
    }
    return end;
}

// Inside any required circle of the current target (either buoy for gates)?
static bool targetInside(double lat, double lon, uint8_t idx, float& rOut)
{
    if (idx >= raceSession.markCount) return false;
    const RaceMark& m = raceSession.marks[idx];
    if (m.type == RaceMarkGate && m.gate[0]) {
        for (uint8_t k = 0; k < raceSession.markCount; k++) {
            const RaceMark& g = raceSession.marks[k];
            if (g.type == RaceMarkGate && strcmp(g.gate, m.gate) == 0) {
                const double d = TinyGPSPlus::distanceBetween(lat, lon, g.lat, g.lon);
                if (d < g.r) {
                    rOut = g.r;
                    return true;
                }
            }
        }
        rOut = m.r;
        return false;
    }
    const double d = TinyGPSPlus::distanceBetween(lat, lon, m.lat, m.lon);
    rOut = m.r;
    return d < m.r;
}

static bool targetOutside(double lat, double lon, uint8_t idx, float r)
{
    // Outside = beyond r + hysteresis on EVERY circle of the target.
    if (idx >= raceSession.markCount) return true;
    const RaceMark& m = raceSession.marks[idx];
    if (m.type == RaceMarkGate && m.gate[0]) {
        for (uint8_t k = 0; k < raceSession.markCount; k++) {
            const RaceMark& g = raceSession.marks[k];
            if (g.type == RaceMarkGate && strcmp(g.gate, m.gate) == 0) {
                if (TinyGPSPlus::distanceBetween(lat, lon, g.lat, g.lon) < g.r + PASS_HYST_M) {
                    return false;
                }
            }
        }
        return true;
    }
    return TinyGPSPlus::distanceBetween(lat, lon, m.lat, m.lon) >= m.r + PASS_HYST_M;
}

static void logPass(const char* what, long now)
{
    bufferedSerialPrintln(what);
    (void)now;
}

void raceRunUpdate(TinyGPSPlus& gps)
{
    if (!raceSession.valid || !gps.location.isValid()) return;
    const long now = raceGpsEpoch(gps);
    if (now <= 0) return;
    const double lat = gps.location.lat(), lon = gps.location.lng();
    const long gun = raceGunEpoch();

    if (gun <= 0) return; // practice not started / race gun unknown

    if (!started) {
        if (!raceSession.startLine.valid) {
            // Point start: the gun starts the clock.
            if (now >= gun) {
                started = true;
                startEpoch = gun;
                logPass("[RACE] started (point gun)", now);
            }
            return;
        }
        const int side = startSegSide(lat, lon);
        const double dist = segDistM(lat, lon, raceSession.startLine);
        const bool near = dist >= 0.0 && dist < CROSS_RANGE_M;
        if (now < gun) {
            gunSide = side;
            hasGunSide = (side != 0);
            if (near && lastSide != 0 && side != 0 && side != lastSide) {
                ocs = true;
            }
        } else {
            if (near && hasGunSide && lastSide != 0 && side != 0 &&
                side != lastSide && side != gunSide) {
                started = true; // proper re-cross after the gun
                startEpoch = now;
                ocs = false;
                logPass("[RACE] started (line cross)", now);
            } else if (now - gun > 60 && !hasGunSide) {
                // Joined late, start unseen: assume a proper gun start.
                started = true;
                startEpoch = gun;
                logPass("[RACE] started (late join)", now);
            } else if (now - gun > 60 && hasGunSide && side != 0 && side != gunSide) {
                // Crossed without a clean flip record (GPS gap at the line).
                started = true;
                startEpoch = gun;
                ocs = false;
                logPass("[RACE] started (late cross)", now);
            }
        }
        if (side != 0) lastSide = side;
        return;
    }

    if (finished) return;

    // Racing: advance through marks by radius pass; finish at the line.
    const uint8_t lastIdx = raceSession.markCount > 0 ? raceSession.markCount - 1 : 0;
    if (progIdx >= raceSession.markCount) {
        finished = true;
        endEpoch = now;
        return;
    }
    const bool isLast = (progIdx == lastIdx);
    const RaceSeg* fin = nullptr;
    const bool lineFinish = isLast && finishIsLine(fin);

    if (lineFinish && fin) {
        // Finish-line cross: bounded side flip.
        double ax, ay, bx, by;
        toBoatFrame(lat, lon, fin->latA, fin->lonA, ax, ay);
        toBoatFrame(lat, lon, fin->latB, fin->lonB, bx, by);
        const int side = segSide(ax, ay, bx, by);
        const double dist = segDistM(lat, lon, *fin);
        const bool near = dist >= 0.0 && dist < CROSS_RANGE_M;
        if (near && finLastSide != 0 && side != 0 && side != finLastSide) {
            finished = true;
            endEpoch = now;
            splits[progIdx] = now - startEpoch;
            splitSet[progIdx] = true;
            logPass("[RACE] finished (line cross)", now);
        }
        if (side != 0) finLastSide = side;
        // Still allow radius pass on the last mark as a fallback below.
        if (finished) return;
    }

    float r = 30.0f;
    if (!wasInside && targetInside(lat, lon, progIdx, r)) {
        wasInside = true;
    } else if (wasInside && targetOutside(lat, lon, progIdx, r)) {
        wasInside = false;
        splits[progIdx] = now - startEpoch;
        splitSet[progIdx] = true;
        if (isLast) {
            finished = true;
            endEpoch = now;
            logPass("[RACE] finished (mark)", now);
        } else {
            const uint8_t doneIdx = progIdx;
            progIdx = gatePairEnd(progIdx) + 1;
            if (progIdx >= raceSession.markCount) {
                finished = true;
                endEpoch = now;
                logPass("[RACE] finished (last mark)", now);
            } else {
                char msg[48];
                snprintf(msg, sizeof(msg), "[RACE] passed mark %d", doneIdx + 1);
                bufferedSerialPrintln(msg);
            }
        }
    }
}
