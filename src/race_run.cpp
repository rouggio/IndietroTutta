#include "race_run.h"
#include "race_session.h"
#include "serial_buffer.h"

#include <Preferences.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include <string.h>

static const float PASS_HYST_M = 5.0f;   // exit radius = r + hyst
static const float CROSS_RANGE_M = 150.0f; // line flips count inside this band

static uint8_t progIdx = 0;
static bool started = false;
static bool finished = false;
static bool ocs = false;
static long startEpoch = 0;
static long endEpoch = 0;
static long splits[10] = {0};
static bool splitSet[10] = {false};
static char runResult[9] = "FINISHED"; // FINISHED|DSQ|DNF|RET (committee)
static long scpSec = 0;                // SCP time add (seconds)

// Event log: ring of 32 {epoch, code, value}. Uploaded with the run.
struct RaceEvent {
    long t = 0;
    char e[8] = {0};
    char v[16] = {0};
};
static RaceEvent evRing[32];
static uint8_t evCount = 0; // saturates at 32 (oldest dropped)

// Wrong-side flag (consumed by the screen for the banner).
static bool wrongFlag = false;

// Gun-edge flag (consumed by the screen for the GO/OCS banner, once per
// start — edge, not state, so reconnects and mode toggles never re-fire).
static bool gunEdge = false;
static bool gunEdgeOcs = false;

// Pre-gun countdown cues (feat #8): the one-minute warning and the 10..1
// per-second ticks. Edges off whole seconds remaining, same rationale as
// gunEdge — a 200ms repaint or a reconnect must not replay a cue, and a
// slow pass (the feed logs 2-5s ones) skips a tick instead of stalling.
static bool gunMinuteCue = false;
static int gunTickDue = -1;
static long cueSecLeft = -1;
static bool minuteDone = false;

// Run upload handshake with the backend task.
static bool uploadPending = false;

// Committee-signal idempotency cursor (per session, NVS-backed).
static long sigLastId = 0;
static long sigLastSes = 0;

// Start-line side tracking.
static int lastSide = 0;
static int gunSide = 0;
static bool hasGunSide = false;
// Finish-line side tracking.
static int finLastSide = 0;
// The boat has been inside the finish-line band at least once since the
// line was armed. Gates the "crossed but flip missed" escape so finishing
// never happens for a boat that has not demonstrably been at the line.
static bool finSawBand = false;
// Pass tracking for the current target.
static bool wasInside = false;
// Circle-entry point + COG at entry. Side is judged at arrival (mark vs
// heading on the way in — the rounding's side is established there;
// departure with the mark astern is free). This replaces the old
// entry->exit chord, which inverted on lapping passes. Dead-ahead/astern
// arrivals (within 20 deg) are unjudged.
static double entryLat = 0.0, entryLon = 0.0;
static double entryCog = -1.0;
static bool entryValid = false;

void raceRunInit()
{
    Preferences prefs;
    if (prefs.begin("race", true)) {
        sigLastId = prefs.getInt("siglast", 0);
        sigLastSes = prefs.getInt("sigses", 0);
        prefs.end();
    }
}

static void saveSigCursor()
{
    Preferences prefs;
    if (prefs.begin("race", false)) {
        prefs.putInt("siglast", (int)sigLastId);
        prefs.putInt("sigses", (int)sigLastSes);
        prefs.end();
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
    finSawBand = false;
    wasInside = false;
    entryValid = false;
    entryCog = -1.0;
    strncpy(runResult, "FINISHED", sizeof(runResult) - 1);
    scpSec = 0;
    evCount = 0;
    wrongFlag = false;
    gunEdge = false;
    gunEdgeOcs = false;
    gunMinuteCue = false;
    gunTickDue = -1;
    cueSecLeft = -1;
    minuteDone = false;
    uploadPending = false;
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

long raceGunEpoch()
{
    if (!raceSession.valid) return 0;
    if (raceSession.startTime <= 0) return 0;
    return raceSession.startTime + raceSession.startOffsetSec;
}

// Signed side of directed segment A->B for point P (ENU meters, boat frame).
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

static void logEvent(const char* code, long now, const char* v)
{
    if (evCount >= 32) {
        // Full: shift left, append at the end (32 entries, rare path).
        for (uint8_t i = 0; i < 31; i++) evRing[i] = evRing[i + 1];
    } else {
        evCount++;
    }
    RaceEvent& dst = evRing[evCount - 1];
    dst.t = now;
    strncpy(dst.e, code ? code : "?", sizeof(dst.e) - 1);
    dst.e[sizeof(dst.e) - 1] = '\0';
    if (v) {
        strncpy(dst.v, v, sizeof(dst.v) - 1);
        dst.v[sizeof(dst.v) - 1] = '\0';
    } else {
        dst.v[0] = '\0';
    }
}

static void logPass(const char* what, long now)
{
    bufferedSerialPrintln(what);
    (void)now;
}

void raceLogEvent(const char* code, long t, const char* v)
{
    logEvent(code, t, v);
}

uint8_t raceEventCount() { return evCount; }

bool raceEventGet(uint8_t i, long* t, char* e, size_t esz, char* v, size_t vsz)
{
    if (i >= evCount) return false;
    if (t) *t = evRing[i].t;
    if (e && esz) {
        strncpy(e, evRing[i].e, esz - 1);
        e[esz - 1] = '\0';
    }
    if (v && vsz) {
        strncpy(v, evRing[i].v, vsz - 1);
        v[vsz - 1] = '\0';
    }
    return true;
}

void raceRunUpdate(TinyGPSPlus& gps)
{
    if (!raceSession.valid || !gps.location.isValid()) return;
    // Wall clock FIRST, GPS epoch only as a cold fallback. Both the banner
    // and the sound cues are shown/h heard against wall time, so the engine
    // must read the same clock they do. Reading the raw fix epoch instead
    // put the cues ~5s out of step with the banner in mock mode, because
    // the batched feed's released fix legitimately lags by the buffer
    // depth (see raceSetWallEpochFromServer).
    long now = raceWallEpoch();
    if (now <= 0) now = raceGpsEpoch(gps);
    if (now <= 0) return;
    const double lat = gps.location.lat(), lon = gps.location.lng();
    const long gun = raceGunEpoch();

    if (gun <= 0) return; // practice not started / race gun unknown

    // Pre-gun cues, evaluated BEFORE the start logic so they still fire on
    // the pass where the boat crosses (now >= gun). Ceil of the seconds
    // remaining: 60 the moment the gun is a minute away, 1 for the last
    // second, 0 once it is gone.
    if (!started) {
        // Both terms are epoch SECONDS, so the difference is already whole
        // seconds — no ceil scaling here. (An earlier version applied the
        // millisecond ceil trick (d+999)/1000 to this second-based delta
        // and reported 51 seconds as 1, which fired a single bogus tick
        // at handoff and silenced the whole countdown.)
        const long secLeft = gun - now;
        if (secLeft != cueSecLeft) {
            // Sticky 1:00 warning: the feed pass is 200ms, but /health and
            // /gps passes run 2-5s, so a plain `== 60` can be stepped over
            // entirely and the warning lost. Ticks only fire for the newest
            // second — a cue queued for a second already gone would land
            // after the gun.
            if (secLeft >= 0 && secLeft <= 60 && !minuteDone) {
                minuteDone = true;
                gunMinuteCue = true;
            }
            if (secLeft >= 1 && secLeft <= 10) gunTickDue = (int)secLeft;
            cueSecLeft = secLeft;
        }
    }

    const bool wasStarted = started;
    auto noteGunEdge = [&]() {
        if (started && !wasStarted) {
            gunEdge = true;
            gunEdgeOcs = ocs;
        }
    };

    if (!started) {
        if (!raceSession.startLine.valid) {
            // Point start: the gun starts the clock.
            if (now >= gun) {
                started = true;
                startEpoch = gun;
                logEvent("START", gun, "point");
                logPass("[RACE] started (point gun)", now);
            }
            noteGunEdge();
            return;
        }
        const int side = startSegSide(lat, lon);
        const double dist = segDistM(lat, lon, raceSession.startLine);
        const bool near = dist >= 0.0 && dist < CROSS_RANGE_M;
        if (now < gun) {
            gunSide = side;
            hasGunSide = (side != 0);
            if (near && lastSide != 0 && side != 0 && side != lastSide) {
                if (!ocs) {
                    ocs = true;
                    logEvent("OCS", now, nullptr);
                }
            }
        } else {
            if (near && hasGunSide && lastSide != 0 && side != 0 &&
                side != lastSide && side != gunSide) {
                started = true; // proper re-cross after the gun
                startEpoch = now;
                if (ocs) {
                    ocs = false;
                    logEvent("RECROSS", now, "line");
                } else {
                    logEvent("START", now, "line");
                }
                logPass("[RACE] started (line cross)", now);
            } else if (now - gun > 60 && !hasGunSide) {
                // Joined late, start unseen: assume a proper gun start.
                started = true;
                startEpoch = gun;
                logEvent("START", gun, "late");
                logPass("[RACE] started (late join)", now);
            } else if (now - gun > 60 && hasGunSide && side != 0 && side != gunSide) {
                // Crossed without a clean flip record (GPS gap at the line).
                started = true;
                startEpoch = gun;
                ocs = false;
                logEvent("START", gun, "late");
                logPass("[RACE] started (late cross)", now);
            }
        }
        if (side != 0) lastSide = side;
        noteGunEdge();
        return;
    }

    if (finished) return;

    // ALL marks cleared → the finish is evaluated HERE, and only here.
    //
    // This used to be checked when progIdx merely EQUALLED the last mark,
    // i.e. while that mark was still to be rounded. On Windward-Leeward
    // (marks 1, 2, 1 + a finish line sameAs the start) that armed the line
    // a whole rounding early: the boat drifted inside CROSS_RANGE_M of the
    // start/finish line and flipped sides, so it finished without ever
    // rounding mark 3. The mark and the line were both claiming to be the
    // finish, and the line won. Finish is a line-segment cross, evaluated
    // only once every mark is behind us.
    if (progIdx >= raceSession.markCount) {
        const RaceSeg* fin = nullptr;
        if (!finishIsLine(fin)) {
            // Course with no line armed at all: nothing left to do here (the
            // last rounding finished it when it was cleared).
            return;
        }
        // Bounded side flip across the finish line.
        double ax, ay, bx, by;
        toBoatFrame(lat, lon, fin->latA, fin->lonA, ax, ay);
        toBoatFrame(lat, lon, fin->latB, fin->lonB, bx, by);
        const int side = segSide(ax, ay, bx, by);
        const double dist = segDistM(lat, lon, *fin);
        const bool near = dist >= 0.0 && dist < CROSS_RANGE_M;
        if (near) finSawBand = true;
        if (near && finLastSide != 0 && side != 0 && side != finLastSide) {
            finished = true;
            endEpoch = now;
            uploadPending = true;
            logEvent("FINISH", now, "line");
            logPass("[RACE] finished (line cross)", now);
        } else if (finSawBand && dist >= CROSS_RANGE_M) {
            // Escape hatch, NOT a radius finish: the boat demonstrably
            // entered the line band and has now left it on the far side,
            // so it crossed without the side flip being recorded (GPS gap
            // at the line, or a very slow pass). Holding here forever would
            // leave the session open with no result; finishing from outside
            // the band on a boat that never entered it would be wrong, which
            // is why finSawBand gates this.
            finished = true;
            endEpoch = now;
            uploadPending = true;
            logEvent("FINISH", now, "pass");
            logPass("[RACE] finished (past line, no clean cross)", now);
        }
        if (side != 0) finLastSide = side;
        return;
    }

    float r = 30.0f;
    if (!wasInside && targetInside(lat, lon, progIdx, r)) {
        wasInside = true;
        entryLat = lat;
        entryLon = lon;
        entryCog = gps.course.isValid() ? gps.course.deg() : -1.0;
        entryValid = true;
    } else if (wasInside && targetOutside(lat, lon, progIdx, r)) {
        wasInside = false;
        // Strict side check on single marks (gates stay lenient: either
        // buoy, no side judgment). Judged at arrival: mark bearing vs
        // heading at circle entry. Dead-ahead/astern (within 20 deg) and
        // missing COG pass unjudged.
        const RaceMark& cur = raceSession.marks[progIdx];
        if (cur.type == RaceMarkSingle && (cur.side == 'P' || cur.side == 'S') && entryValid) {
            entryValid = false;
            if (entryCog >= 0.0) {
                double brg = TinyGPSPlus::courseTo(entryLat, entryLon, cur.lat, cur.lon);
                if (brg < 0) brg += 360.0;
                double rel = brg - entryCog;
                while (rel < 0.0) rel += 360.0;
                while (rel >= 360.0) rel -= 360.0;
                const bool deadZone = rel < 20.0 || rel > 340.0 ||
                                      (rel > 160.0 && rel < 200.0);
                if (!deadZone) {
                    const char sailed = rel > 180.0 ? 'P' : 'S';
                    if (sailed != cur.side) {
                        char v[16];
                        snprintf(v, sizeof(v), "%c!%c", cur.side, sailed);
                        logEvent("WRONG", now, v);
                        wrongFlag = true;
                        bufferedSerialPrintln("[RACE] wrong side, re-round");
                        return; // no advance: re-enter and round again
                    }
                }
            }
        } else {
            entryValid = false;
        }
        splits[progIdx] = now - startEpoch;
        splitSet[progIdx] = true;
        {
            const uint8_t doneIdx = progIdx;
            char v[16];
            snprintf(v, sizeof(v), "%d", doneIdx + 1);
            logEvent("PASS", now, v);
            progIdx = gatePairEnd(progIdx) + 1;

            if (progIdx >= raceSession.markCount) {
                progIdx = raceSession.markCount;
                const RaceSeg* finChk = nullptr;
                if (finishIsLine(finChk)) {
                    // Every mark is behind us and a line is armed: the
                    // cross is evaluated on the following passes. Radius-
                    // passing the line circle must NEVER finish the race.
                    logEvent("ARMED", now, "line");
                    bufferedSerialPrintln("[RACE] last mark cleared, finish line armed");
                } else {
                    // No finish line on this course: the last rounding IS
                    // the finish (explicit fallback, not an accident).
                    finished = true;
                    endEpoch = now;
                    uploadPending = true;
                    logEvent("FINISH", now, "mark");
                    logPass("[RACE] finished (last mark)", now);
                }
            } else {
                char msg[48];
                snprintf(msg, sizeof(msg), "[RACE] passed mark %d", doneIdx + 1);
                bufferedSerialPrintln(msg);
            }
        }
    }
}

// --- Step 5: events (turn declarations removed) ---------------------------

bool raceWrongPoll()
{
    const bool w = wrongFlag;
    wrongFlag = false;
    return w;
}

// Gun-edge poll for the GO/OCS banner: true once per start, with whether
// the boat was OCS at the edge. Edge, not state — consuming it here means
// reconnects and mode toggles never re-fire the banner.
bool raceGunEdge(bool* ocsOut)
{
    if (!gunEdge) return false;
    gunEdge = false;
    if (ocsOut) *ocsOut = gunEdgeOcs;
    return true;
}

bool raceGunMinuteCue()
{
    if (!gunMinuteCue) return false;
    gunMinuteCue = false;
    return true;
}

int raceGunTickCue()
{
    const int v = gunTickDue;
    gunTickDue = -1;
    return v; // seconds remaining (10..1), or -1 when nothing is due
}

// --- Step 5: run upload ---------------------------------------------------

bool raceUploadPending() { return uploadPending && startEpoch > 0; }

void raceUploadAck() { uploadPending = false; }

void raceUploadBody(const char* deviceId, char* buf, size_t n)
{
    if (!buf || !n) return;
    const long finish = (finished && endEpoch > 0) ? endEpoch + scpSec : 0;
    size_t o = snprintf(buf, n,
        "{\"deviceId\":\"%s\",\"startEpoch\":%ld,\"finishEpoch\":%ld,"
        "\"splits\":[",
        deviceId ? deviceId : "", startEpoch, finish);
    for (uint8_t i = 0; i < 10 && o < n; i++) {
        if (!splitSet[i]) continue;
        o += snprintf(buf + o, n - o, "%s%ld", o > 0 && buf[o - 1] != '[' ? "," : "", splits[i]);
    }
    o += snprintf(buf + o, n - o, "],\"events\":[");
    for (uint8_t i = 0; i < evCount && o < n; i++) {
        o += snprintf(buf + o, n - o, "%s{\"t\":%ld,\"e\":\"%s\"%s%s%s}",
            i ? "," : "", evRing[i].t, evRing[i].e,
            evRing[i].v[0] ? ",\"v\":\"" : "", evRing[i].v[0] ? evRing[i].v : "",
            evRing[i].v[0] ? "\"" : "");
    }
    snprintf(buf + o, n - o, "],\"result\":\"%s\"}", runResult);
}

// --- Step 7: committee signals ----------------------------------------------

bool raceSignalIsNew(long id)
{
    if (id <= 0) return false;
    if (!raceSession.valid) return false;
    if (raceSession.sessionId != sigLastSes) {
        // New session: cursor restarts (old signals arrive fresh, applied once).
        sigLastSes = raceSession.sessionId;
        sigLastId = 0;
        saveSigCursor();
    }
    return id > sigLastId;
}

void raceSignalMark(long id)
{
    if (id <= 0) return;
    sigLastId = id;
    if (raceSession.valid) sigLastSes = raceSession.sessionId;
    saveSigCursor();
}

void raceApplySignal(const char* kind, const char* detail, long now)
{
    if (!kind) return;
    if (strcmp(kind, "OCS") == 0) {
        ocs = true;
        logEvent("SIG", now, "OCS");
        bufferedSerialPrintln("[RACE] signal: OCS confirmed");
    } else if (strcmp(kind, "DSQ") == 0 || strcmp(kind, "DNF") == 0 ||
               strcmp(kind, "RET") == 0) {
        strncpy(runResult, kind, sizeof(runResult) - 1);
        if (started && !finished) {
            finished = true;
            endEpoch = now > 0 ? now : startEpoch;
            uploadPending = true;
        }
        logEvent("SIG", now, kind);
        bufferedSerialPrintln("[RACE] signal: run invalid");
    } else if (strcmp(kind, "SCP") == 0) {
        const long add = detail ? atol(detail) : 0;
        if (add > 0) {
            scpSec += add;
            if (finished) uploadPending = true; // re-upload with new total
            char v[16];
            snprintf(v, sizeof(v), "+%ld", add);
            logEvent("SIG", now, v);
        }
        bufferedSerialPrintln("[RACE] signal: SCP time added");
    } else if (strcmp(kind, "RECALL") == 0) {
        logEvent("SIG", now, "RECALL");
        raceRunReset(); // back to pre-start; committee re-sets the gun
        bufferedSerialPrintln("[RACE] signal: general recall");
    } else if (strcmp(kind, "ABANDON") == 0) {
        logEvent("SIG", now, "ABANDON");
        raceRunReset(); // run void; session cache stays for re-sail
        bufferedSerialPrintln("[RACE] signal: abandoned");
    }
}
