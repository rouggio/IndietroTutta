#include "race_run.h"
#include "race_session.h"
#include "serial_buffer.h"

#include <Preferences.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include <string.h>

static const float CROSS_RANGE_M = 150.0f; // line flips count inside this band
// Distance at which a pre-gun position counts as a real "approach" for OCS
// purposes. Comfortably inside CROSS_RANGE_M so a boat that sailed out and
// came back is still judged, but far enough out that jitter on the line
// cannot manufacture a side.
static const float OCS_SETTLE_M = 60.0f;

static uint8_t progIdx = 0;
static bool started = false;
static bool finished = false;
// Pass recognition (spec mark-passing.md §3): angles + bow side only, no
// ranges. Per-leg state: excursion of the true mark bearing since the leg
// began (rounding proof), bow-side arming (wrong-way loops), triangle
// switch (clear route), gate threading, and an anti-stuck leg reset.
static const double PASS_EXC_DEG = 150.0; // excursion proving the rounding
static const double SIDE_ARM_DEG = 135.0; // side engages just below it
static const double TRI_SWITCH_DEG = 90.0; // clear-route triangle at the boat
static const double DEAD_CONE_DEG = 20.0;  // bow/stern exemption for side
static uint8_t legIdx = 255;      // target the leg state belongs to
static bool legExcInit = false;
static double legExcMin = 0.0, legExcMax = 0.0; // unwrapped smoothed extremes
static bool haveSmoothBrg = false;
static double smoothBrg = 0.0;    // EMA of the unwrapped mark bearing
static double smoothDist = -1.0;  // EMA of the mark distance
static double legMinDist = -1.0;  // smallest smoothed distance this leg
static bool sideArmed = false;
static bool triHeld = false;      // triangle>90 on the previous pass
static bool gateThreaded = false;
static double legPrevLat = 0.0, legPrevLon = 0.0;
static bool havePrev = false;

// Forward: defined with the pass helpers below, used by raceRunReset.
static void legReset(uint8_t idx);
static void logEvent(const char* code, long now, const char* v);
static void logPass(const char* what, long now);
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
static RaceEvent evRing[64];
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
// Side held while the boat was genuinely clear of the start line, used to
// judge OCS. Distinct from gunSide, which tracks the side every pass and
// is meaningless while the boat is parked on the line.
static int approachSide = 0;
static bool sawApproach = false;
static int gunSide = 0;
static bool hasGunSide = false;
// Finish-line side tracking.
static int finLastSide = 0;
static double finMinDist = 1e9;
static long finishPendingUntil = 0; // hold a grace window after the cross
// The boat has been inside the finish-line band at least once since the
// line was armed. Gates the "crossed but flip missed" escape so finishing
// never happens for a boat that has not demonstrably been at the line.
static bool finSawBand = false;
// When the finish line was armed (first pass with every mark behind us), for
// the serial diagnostic. Diagnostic only — nothing scores from it.
static long finArmedAt = 0;
// (leg rounding state lives in the block above: excursion/side/triangle.)

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
    const bool wasStarted = started;   // read BEFORE the wipe below
    const bool wasFinished = finished;
    progIdx = 0;
    started = false;
    finished = false;
    ocs = false;
    startEpoch = 0;
    endEpoch = 0;
    lastSide = 0;
    approachSide = 0;
    sawApproach = false;
    gunSide = 0;
    hasGunSide = false;
    finLastSide = 0;
    finSawBand = false;
    finArmedAt = 0;
    legReset(progIdx);
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
    // Breadcrumb. A reset wipes hasGunSide/cueSecLeft — the state the start
    // decision is built on — so a reset landing between the gun and the
    // crossing silently demotes a clean line-cross start to a late join.
    // The serial buffer rolls in ~3 minutes, so without this line there is
    // no way to tell afterwards WHICH pass killed the start state.
    bufferedSerialPrintln(String("[RACE] run reset (session ") +
                          raceSession.sessionId + " cv" + raceSession.courseVersion +
                          " was started=" + (wasStarted ? 1 : 0) +
                          " finished=" + (wasFinished ? 1 : 0) + ")");
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
    // One gun for the whole session: pursuit/handicap starts (per-boat
    // startOffsetSec) were removed, so this is just the session startTime.
    return raceSession.startTime;
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

// 0..360 normalization for bearings.
static double norm360(double d)
{
    while (d < 0.0) d += 360.0;
    while (d >= 360.0) d -= 360.0;
    return d;
}

// Nearest equivalent of ang to ref (unwrapped continuity for the EMA).
static double unwrapNear(double ref, double ang)
{
    double d = ang - ref;
    while (d < -180.0) d += 360.0;
    while (d > 180.0) d -= 360.0;
    return ref + d;
}

// Angle at the boat between points A and B, folded to 0..180.
static double triAngle(double lat, double lon,
                       double aLat, double aLon, double bLat, double bLon)
{
    const double b1 = TinyGPSPlus::courseTo(lat, lon, aLat, aLon);
    const double b2 = TinyGPSPlus::courseTo(lat, lon, bLat, bLon);
    double d = fabs(b1 - b2);
    while (d >= 360.0) d -= 360.0;
    if (d > 180.0) d = 360.0 - d;
    return d;
}

// Bounded segment-segment crossing (local equirectangular meters — exact
// crossing math, not a proximity threshold).
static bool segXing(double aLat, double aLon, double bLat, double bLon,
                    double cLat, double cLon, double dLat, double dLon)
{
    const double lat0 = (aLat + bLat + cLat + dLat) * 0.25;
    const double k = cos(lat0 * M_PI / 180.0);
    const double d1x = (bLon - aLon) * k, d1y = bLat - aLat;
    const double d2x = (dLon - cLon) * k, d2y = dLat - cLat;
    const double denom = d1x * d2y - d1y * d2x;
    if (fabs(denom) < 1e-12) return false;
    const double t = ((cLon - aLon) * k * d2y - (cLat - aLat) * d2x) / denom;
    const double u = ((cLon - aLon) * k * d1y - (cLat - aLat) * d1x) / denom;
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0;
}

// Sibling buoy of a gate pair (same gate id), or -1 when lone.
static int gateSibling(uint8_t i)
{
    if (i >= raceSession.markCount) return -1;
    const RaceMark& m = raceSession.marks[i];
    if (m.type != RaceMarkGate || !m.gate[0]) return -1;
    for (uint8_t k = 0; k < raceSession.markCount; k++) {
        if (k != i && raceSession.marks[k].type == RaceMarkGate &&
            strcmp(raceSession.marks[k].gate, m.gate) == 0) {
            return (int)k;
        }
    }
    return -1;
}

// Reference point of a target: the mark, or the pair center for gates.
static void targetPoint(uint8_t i, double& lat, double& lon)
{
    const RaceMark& m = raceSession.marks[i];
    const int sib = gateSibling(i);
    if (sib >= 0) {
        lat = (m.lat + raceSession.marks[sib].lat) / 2.0;
        lon = (m.lon + raceSession.marks[sib].lon) / 2.0;
    } else {
        lat = m.lat;
        lon = m.lon;
    }
}

// Next-target point after target i (triangle far arm): the following mark
// (gate pairs resolve to their center), else the finish center. False when
// there is nothing (caller then treats the triangle as satisfied).
static bool nextTargetPoint(uint8_t i, double& lat, double& lon)
{
    uint8_t ni = gatePairEnd(i) + 1;
    if (ni < raceSession.markCount) {
        targetPoint(ni, lat, lon);
        return true;
    }
    const RaceSeg* fin = nullptr;
    if (finishIsLine(fin)) {
        lat = (fin->latA + fin->latB) / 2.0;
        lon = (fin->latA + fin->latB) / 2.0;
        return true;
    }
    return false;
}

// Fresh leg state for target idx (re-approach / re-round / next leg).
static void legReset(uint8_t idx)
{
    legIdx = idx;
    legExcInit = false;
    legExcMin = 0.0;
    legExcMax = 0.0;
    haveSmoothBrg = false;
    smoothBrg = 0.0;
    smoothDist = -1.0;
    legMinDist = -1.0;
    sideArmed = false;
    triHeld = false;
    gateThreaded = false;
    havePrev = false;
}

// Score the pass: split + event + forward advance + leg reset. The finish
// arm/finish paths are the same as ever (todo 16: line cross only).
static void scorePass(uint8_t doneIdx, long now)
{
    splits[doneIdx] = now - startEpoch;
    splitSet[doneIdx] = true;
    char v[16];
    snprintf(v, sizeof(v), "%d", doneIdx + 1);
    logEvent("PASS", now, v);
    progIdx = gatePairEnd(doneIdx) + 1;
    if (progIdx >= raceSession.markCount) {
        progIdx = raceSession.markCount;
        const RaceSeg* finChk = nullptr;
        if (finishIsLine(finChk)) {
            logEvent("ARMED", now, "line");
            bufferedSerialPrintln("[RACE] last mark cleared, finish line armed");
        } else {
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
    legReset(progIdx);
}

static void logEvent(const char* code, long now, const char* v)
{
    if (evCount >= 64) {
        // Full: shift left, append at the end (64 entries, rare path).
        for (uint8_t i = 0; i < 63; i++) evRing[i] = evRing[i + 1];
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
    // FX clock: only discard a truly stale fix. Mock fixes legitimately lag
    // several seconds because of the 5-sample queue depth, so the threshold
    // here must stay well above 1 s or genuine passes are silently dropped.
    if (gps.location.age() > 5000) return;
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

    // (Passes score in the mark section below — single event, no deferral.)


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
        // Diagnostic: log the start decision only when it CHANGES — the
        // side, or whether the boat is inside the crossing band. Once a
        // second it buried the race in ~66 lines and pushed "passed mark"
        // and "finished" out of the 200-line serial ring, which is exactly
        // what this was added to diagnose. On transitions it costs ~4 lines
        // per race and still shows the whole approach: enter the band, flip
        // the side, cross.
        {
            static int dbgLastSide = -9;
            static bool dbgLastNear = false;
            static bool dbgFirst = true;
            const long dGun = now - gun;
            if (dGun >= -60 && dGun <= 30 && !started &&
                (dbgFirst || side != dbgLastSide || near != dbgLastNear)) {
                dbgFirst = false;
                dbgLastSide = side;
                dbgLastNear = near;
                bufferedSerialPrintln(String("[RACE] gun") +
                                      (dGun > 0 ? String("+") : String("")) + dGun +
                                      "s dist=" + (int)dist + "m side=" + side +
                                      " near=" + (near ? 1 : 0) +
                                      " gunSide=" + gunSide +
                                      " hasGunSide=" + (hasGunSide ? 1 : 0) +
                                      " ocs=" + (ocs ? 1 : 0));
            }
        }
        if (now < gun) {
            gunSide = side;
            hasGunSide = (side != 0);
            // OCS needs an APPROACH, not a side flip on the line. A session
            // anchored at the boat's own position puts the start line
            // exactly under it, so the boat can sit on the line for the
            // whole wait; taking that as "the side" made a metre of jitter
            // read as a crossing. Remember the side held while genuinely
            // outside OCS_SETTLE_M, and only raise OCS against THAT.
            if (dist >= OCS_SETTLE_M) {
                approachSide = side;
                sawApproach = true;
            } else if (near && sawApproach && side != 0 && side != approachSide) {
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
            } else if (now - gun > 60 && hasGunSide && !ocs && near &&
                       side != 0 && side != gunSide) {
                // Crossed without a clean flip record (GPS gap at the line).
                //
                // Two guards, both learned the hard way: this used to have
                // no `near` test, so 60s after the gun it started ANY boat
                // sitting on the early side — hundreds of metres away from
                // the line — and it cleared `ocs` unconditionally, so a boat
                // that genuinely was OCS was silently forgiven and started
                // as if it had never crossed early. It must now be AT the
                // line, and an OCS boat is excluded entirely: that has to
                // re-cross properly through the path above.
                started = true;
                startEpoch = gun;
                logEvent("START", gun, "late");
                logPass("[RACE] started (late cross)", now);
            }
        }
        if (side != 0) lastSide = side;
        noteGunEdge();
        return;
    }

    if (finished) return;
    // Grace window: after the finish cross is scored, keep the race alive
    // briefly so the boat's trailing fixes (already on the way, but not yet
    // polled) reach both the panel and the store.
    if (finishPendingUntil > 0 && now < finishPendingUntil) return;
    if (finishPendingUntil > 0 && now >= finishPendingUntil) {
        finished = true;
        endEpoch = now;
        uploadPending = true;
        logEvent("FINISH", now, "line");
        logPass("[RACE] finished (line cross, held grace)", now);
        finishPendingUntil = 0;
        return;
    }

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
        // The moment every mark is behind us, the finish starts from a clean
        // slate. Anything accumulated earlier — a band entry during the
        // upwind/downwind that passed the START/finish geometry — is stale by
        // then (the boat was round other marks, not finished) and only hands
        // the escape hatch a reason to fire without an actual crossing, which
        // is what the W/L-with-Gate course kept hitting.
        if (finArmedAt == 0) {
            finArmedAt = now;
            finSawBand = false;
            finLastSide = 0;
            finMinDist = 1e9;
        }
        // Bounded side flip across the finish line.
        double ax, ay, bx, by;
        toBoatFrame(lat, lon, fin->latA, fin->lonA, ax, ay);
        toBoatFrame(lat, lon, fin->latB, fin->lonB, bx, by);
        const int side = segSide(ax, ay, bx, by);
        const double dist = segDistM(lat, lon, *fin);
        const bool near = dist >= 0.0 && dist < CROSS_RANGE_M;
        if (near) finSawBand = true;
        if (near && dist < finMinDist) finMinDist = dist;
        // Diagnostic: same treatment as the start window — log only on a
        // CHANGE of side/near, so it costs a few lines per race instead of
        // evicting "passed mark" and "finished" from the serial ring. The
        // offline route checker says this crossing is sustained and the
        // instrument disagrees, so the disagreement has to be read off the
        // device rather than argued about.
        {
            static int dbgFinSide = -9;
            static bool dbgFinNear = false;
            static bool dbgFinFirst = true;
            if (dbgFinFirst || side != dbgFinSide || near != dbgFinNear) {
                dbgFinFirst = false;
                dbgFinSide = side;
                dbgFinNear = near;
                bufferedSerialPrintln(String("[RACE] fin dist=") + (int)dist +
                                      "m side=" + side + " near=" + (near ? 1 : 0) +
                                      " finLastSide=" + finLastSide +
                                      " band=" + (finSawBand ? 1 : 0) +
                                      " armedFor=" + (now - finArmedAt) + "s");
            }
        }
        if (near && finLastSide != 0 && side != 0 && side != finLastSide) {
            // Score the finish on the crossing edge so the session terminates
            // the moment the boat crosses — waiting for "trailing fixes" only
            // delayed status/upload (user sees the boat sail past the line
            // with nothing happening) and let the run get abandoned mid-grace.
            finished = true;
            endEpoch = now;
            uploadPending = true;
            logEvent("FINISH", now, "line");
            logPass("[RACE] finished (line cross)", now);
        } else if (finSawBand && dist >= CROSS_RANGE_M && (finMinDist < 25.0)) {
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

    // --- Pass recognition (spec §3-5): angles + bow side, no ranges. ---
    if (legIdx != progIdx) legReset(progIdx);
    {
        const RaceMark& cur = raceSession.marks[progIdx];
        // Start pins are not roundings: sail past them, never score them.
        if (cur.type == RaceMarkStart || cur.type == RaceMarkNone) {
            scorePass(progIdx, now);
            legPrevLat = lat;
            legPrevLon = lon;
            havePrev = true;
            return;
        }
        const bool isGate = (cur.type == RaceMarkGate && gateSibling(progIdx) >= 0);
        double tx, ty; // this target's reference point (mark or pair center)
        if (isGate) targetPoint(progIdx, tx, ty);
        else { tx = cur.lat; ty = cur.lon; }
        double nx, ny; // next-target point (triangle far arm)
        const bool haveNext = nextTargetPoint(progIdx, nx, ny);
        const double tri = haveNext ? triAngle(lat, lon, tx, ty, nx, ny) : 180.0;

        if (isGate) {
            // Thread the pair, then leave on a bearing that clears it.
            double gax, gay, gbx, gby;
            const int sib = gateSibling(progIdx);
            gax = cur.lat; gay = cur.lon;
            gbx = raceSession.marks[sib].lat; gby = raceSession.marks[sib].lon;
            if (havePrev && !gateThreaded &&
                segXing(legPrevLat, legPrevLon, lat, lon, gax, gay, gbx, gby)) {
                gateThreaded = true;
                char evv[16];
                snprintf(evv, sizeof(evv), "%u", (unsigned)(progIdx + 1));
                logEvent("ENTER", now, evv);
                bufferedSerialPrintln("[RACE] gate threaded");
            }
            const bool clears = !segXing(lat, lon, nx, ny, gax, gay, gbx, gby);
            if (tri > TRI_SWITCH_DEG) triHeld = true; else triHeld = false;
            if (gateThreaded && clears && triHeld && haveNext) {
                scorePass(progIdx, now);
                legPrevLat = lat;
                legPrevLon = lon;
                havePrev = true;
                return;
            }
        } else {
            // Excursion of the smoothed true bearing: the rounding proof.
            const double raw = norm360(TinyGPSPlus::courseTo(lat, lon, tx, ty));
            if (!haveSmoothBrg) {
                smoothBrg = raw;
                legExcMin = legExcMax = raw;
                haveSmoothBrg = true;
            } else {
                const double u = unwrapNear(smoothBrg, raw);
                smoothBrg = 0.6 * smoothBrg + 0.4 * u;
                if (smoothBrg < legExcMin) legExcMin = smoothBrg;
                if (smoothBrg > legExcMax) legExcMax = smoothBrg;
            }
            const double exc = legExcMax - legExcMin;
            const double rawD = TinyGPSPlus::distanceBetween(lat, lon, tx, ty);
            if (smoothDist < 0) smoothDist = rawD;
            else smoothDist = 0.5 * smoothDist + 0.5 * rawD;
            if (legMinDist < 0 || smoothDist < legMinDist) legMinDist = smoothDist;
            // Side engages only once nearly proven round: the approach
            // (tacks included) stays free, wrong-way loops still fault.
            if (!sideArmed && exc >= SIDE_ARM_DEG) {
                sideArmed = true;
                bufferedSerialPrintln("[RACE] side armed");
            }
            const bool judgeSide = sideArmed && cur.type == RaceMarkSingle &&
                (cur.side == 'P' || cur.side == 'S') &&
                gps.course.isValid();
            if (judgeSide) {
                const double cog = gps.course.deg();
                double rel = norm360(raw - cog);
                const bool deadZone = rel < DEAD_CONE_DEG || rel > 360.0 - DEAD_CONE_DEG ||
                    (rel > 160.0 && rel < 200.0);
                if (!deadZone) {
                    const char sailed = rel > 180.0 ? 'P' : 'S';
                    if (sailed != cur.side) {
                        char wv[16];
                        snprintf(wv, sizeof(wv), "%c!%c", cur.side, sailed);
                        logEvent("WRONG", now, wv);
                        wrongFlag = true;
                        bufferedSerialPrintln("[RACE] wrong side, re-round");
                        legReset(progIdx); // the re-round re-proves itself
                        legPrevLat = lat;
                        legPrevLon = lon;
                        havePrev = true;
                        return;
                    }
                }
            }
            // Two consecutive triangle reads: one noisy fix near the mark
            // must not flip the target.
            if (tri > TRI_SWITCH_DEG) {
                if (triHeld) {
                    if (exc >= PASS_EXC_DEG) {
                        scorePass(progIdx, now);
                        legPrevLat = lat;
                        legPrevLon = lon;
                        havePrev = true;
                        return;
                    }
                } else {
                    triHeld = true;
                }
            } else {
                triHeld = false;
                // Anti-stuck: left the area facing elsewhere — the next
                // visit is a new approach, not a continuation.
                if (legMinDist > 0 && smoothDist > 2.0 * legMinDist && tri < 60.0) {
                    bufferedSerialPrintln("[RACE] re-approach, leg reset");
                    legReset(progIdx);
                    legPrevLat = lat;
                    legPrevLon = lon;
                    havePrev = true;
                    return;
                }
            }
        }
        legPrevLat = lat;
        legPrevLon = lon;
        havePrev = true;
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
