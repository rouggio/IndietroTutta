#include "race_run.h"
#include "race_session.h"
#include "serial_buffer.h"

#include <Arduino.h>
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
// Pass recognition (SPEC-mark-passing.md, locked): two traverses per mark.
// Per-leg state: entry latch (cut the entry traverse while leaving A),
// leaving trends (EMA distance to A/B over consecutive fixes), gate thread
// flag. No angles, no bow, no ranges on marks.
static const double TRAV_HALF_M = 2000.0; // traverse half-length: unbounded for any course
static const uint8_t LEAVE_N = 3;         // consecutive EMA rises = leaving
static uint8_t legIdx = 255;      // target the leg state belongs to
static bool entryLatched = false; // cut the entry traverse leaving A
static bool exitLatched = false;  // cut the exit traverse toward C (side in exitSide)
static char exitSide = ' ';       // P/S read at the exact crossing point
static double emaDA = -1.0, emaDB = -1.0; // EMA distance to A (previous) and B (target)
static uint8_t growA = 0, growB = 0; // consecutive EMA rises
static bool legFresh = true;      // first evaluated fix: late join may sit past the entry
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

// --- Run integrity watchdog (2026-10-17) --------------------------------
// A run that has demonstrably gone wrong is STOPPED where it stands instead
// of being watched to the end. The evidence around the failure is worth more
// than the rest of the race: a broken run cannot be diagnosed by staring at
// a boat that is drifting, and the serial ring rolls in ~3 minutes anyway.
// Frozen means the engine consumes no further fix, nothing scores, nothing
// uploads, and the race screen shows a red BAD RUN tag — so this is visible
// on the water, not only in the log.
static bool badRun = false;
static char badRunWhy[32] = {0};
// Rejected fixes in a row (see the fix gate in raceRunUpdate). A stream that
// cannot be read is a dead end, not a course to sail.
static uint8_t badFixRun = 0;
// WRONG verdicts on the mark being rounded right now. One is a mistake; three
// on the same mark means the engine failed to re-arm and will not recover on
// its own, which is exactly the "bad run" the sailor asked to have stopped.
static uint8_t wrongRun = 0;
static uint8_t wrongRunMark = 255;

static void raceMarkBadRun(const char* why, unsigned prog, unsigned marks,
                           bool st, bool fin, bool oc, double lat, double lon,
                           unsigned long ageMs)
{
    if (badRun) return;
    badRun = true;
    strncpy(badRunWhy, why, sizeof(badRunWhy) - 1);
    badRunWhy[sizeof(badRunWhy) - 1] = '\0';
    char msg[176];
    snprintf(msg, sizeof(msg),
             "[RACE] BAD RUN %s | prog=%u/%u started=%d finished=%d ocs=%d "
             "f=(%.5f,%.5f) age=%lums wrongRun=%u@%u",
             why, prog, marks, st ? 1 : 0, fin ? 1 : 0, oc ? 1 : 0,
             lat, lon, ageMs, (unsigned)wrongRun, (unsigned)wrongRunMark);
    bufferedSerialPrintln(msg);
    bufferedSerialPrintln("[RACE] BAD RUN frozen: no further fixes scored, no upload");
}
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
    badRun = false;
    badRunWhy[0] = '\0';
    badFixRun = 0;
    wrongRun = 0;
    wrongRunMark = 255;
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
bool raceRunBadRun() { return badRun; }
const char* raceRunBadRunWhy() { return badRunWhy; }
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

// Bounded segment-segment crossing in ENU meters (a touch counts).
static bool segXingXYT(double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy, double& t);
static bool segXingXY(double ax, double ay, double bx, double by,
                      double cx, double cy, double dx, double dy)
{
    double t = 0.0;
    return segXingXYT(ax, ay, bx, by, cx, cy, dx, dy, t);
}

// Same, also returning t (0..1 along A→B) for the exact crossing point.
static bool segXingXYT(double ax, double ay, double bx, double by,
                       double cx, double cy, double dx, double dy, double& t)
{
    const double d1x = bx - ax, d1y = by - ay;
    const double d2x = dx - cx, d2y = dy - cy;
    const double denom = d1x * d2y - d1y * d2x;
    if (fabs(denom) < 1e-12) return false;
    t = ((cx - ax) * d2y - (cy - ay) * d2x) / denom;
    const double u = ((cx - ax) * d1y - (cy - ay) * d1x) / denom;
    return t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0;
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
        // Both are CENTRE coordinates. This used to read
        // `lon = (fin->latA + fin->latB) / 2.0`, i.e. a longitude built from
        // the line's latitudes: the last mark is the only one that takes C
        // from here, so it was the only mark judged against a nonsense exit
        // traverse (an axis pointing ~90 deg east instead of down the leg),
        // and it scored early — while the boat was still on the approach —
        // which is what made the auto zoom leave before the rounding.
        lat = (fin->latA + fin->latB) / 2.0;
        lon = (fin->lonA + fin->lonB) / 2.0;
        return true;
    }
    return false;
}

// Fresh leg state for target idx (re-approach / re-round / next leg).
static void legReset(uint8_t idx)
{
    legIdx = idx;
    entryLatched = false;
    exitLatched = false;
    exitSide = ' ';
    emaDA = -1.0;
    emaDB = -1.0;
    growA = 0;
    growB = 0;
    legFresh = true;
    gateThreaded = false;
    havePrev = false;
    // Point start with no line geometry (mark 0): there is no entry line to
    // cut, so the approach is latched from the start.
    if (idx == 0 && !raceSession.startLine.valid) entryLatched = true;
}

// Score the pass: split + event + forward advance + leg reset. The finish
// arm/finish paths are the same as ever (todo 16: line cross only).
// kind: "mark" (detail = cut side P/S), "gate", "pin" (start-pin skip).
static void scorePass(uint8_t doneIdx, long now, const char* kind, char detail)
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
        char msg[80];
        if (kind && strcmp(kind, "gate") == 0)
            snprintf(msg, sizeof(msg), "[RACE] passed mark %d (gate)", doneIdx + 1);
        else if (kind && strcmp(kind, "pin") == 0)
            snprintf(msg, sizeof(msg), "[RACE] passed mark %d (pin skip)", doneIdx + 1);
        else
            snprintf(msg, sizeof(msg), "[RACE] passed mark %d (cut %c)", doneIdx + 1, detail);
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

    // Frozen by the watchdog: consume nothing, score nothing, change nothing.
    // The last state IS the evidence, and it has to survive the read-out.
    if (badRun) return;

    // --- Fix sanity gate (diagnostic) -----------------------------------
    // A teleporting fix carries no information and corrupts everything
    // downstream (a 40 m jump across a traverse reads as a cut on the wrong
    // side). Two things were wrong with the first version, both fixed here:
    //
    // 1. THRESHOLD. It used 2x speed — ~29 m at 28 kn and 1 Hz — which sits
    //    inside the spread of the feed's OWN gaps (40-56 m), so it rejected
    //    ordinary fixes. 4x speed with a 60 m floor is far above a normal
    //    step and far below a real teleport (hundreds of metres).
    // 2. IT LATCHED. It returned BEFORE moving the anchor, so one dropped fix
    //    made the next gap larger, which was dropped too: in race 181 the
    //    engine stopped consuming fixes 11 s after the finish armed and never
    //    scored it, while the screen kept showing the old target as the boat
    //    sailed on. A rejected fix now RE-ANCHORS: the jump never reaches the
    //    pass logic, but the next fix is measured from where the boat is.
    {
        static double rejLat = 0.0, rejLon = 0.0;
        static unsigned long rejMs = 0;
        static unsigned long lastStepMs = 0;
        const unsigned long mnow = millis();
        // The same fix can reach the engine twice (the 200 ms screen pass
        // revisits it); the second visit would measure a zero-length step and
        // score the very fix the first visit rejected. Same coordinates
        // within 400 ms of a rejection = the same fix, still rejected.
        if (rejMs != 0 && (mnow - rejMs) < 400 &&
            fabs(lat - rejLat) < 1e-9 && fabs(lon - rejLon) < 1e-9) {
            rejMs = mnow;
            return;
        }
        if (havePrev && lastStepMs != 0) {
            double dt = (mnow - lastStepMs) / 1000.0;
            if (dt < 0.5) dt = 0.5;
            if (dt > 10.0) dt = 10.0;
            const double sog = gps.speed.isValid() ? gps.speed.knots() * 0.51444
                                                   : 6.0;
            double maxStep = 4.0 * sog * dt;
            if (maxStep < 60.0) maxStep = 60.0;
            const double step = TinyGPSPlus::distanceBetween(
                legPrevLat, legPrevLon, lat, lon);
            if (step > maxStep) {
                legPrevLat = lat;
                legPrevLon = lon;
                havePrev = true;
                rejLat = lat;
                rejLon = lon;
                rejMs = mnow;
                lastStepMs = mnow;
                if (badFixRun < 255) badFixRun++;
                char jmsg[96];
                snprintf(jmsg, sizeof(jmsg),
                         "[RACE] fix rejected d=%.0f over %.1fs (limit %.0f) run=%u",
                         step, dt, maxStep, (unsigned)badFixRun);
                bufferedSerialPrintln(jmsg);
                if (badFixRun >= 8) {
                    raceMarkBadRun("fix stream unusable", progIdx,
                                   raceSession.markCount, started, finished,
                                   ocs, lat, lon, gps.location.age());
                }
                return;
            }
            badFixRun = 0;
        }
        lastStepMs = mnow;
    }

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
            // Clean slate for the crossing SEGMENT too: legPrev still holds
            // the fix from the last rounding, hundreds of metres back, and a
            // segment drawn from there could score a cross the instant the
            // line arms. The crossing starts from where the boat is now.
            legPrevLat = lat;
            legPrevLon = lon;
            havePrev = true;
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
        // PRIMARY: the finish is a crossing of the line SEGMENT, scored on the
        // segment between the last two ACCEPTED fixes — the same exact rule
        // the marks use (segXing, not a proximity threshold). The band +
        // side-flip below needs a sample to land inside 150 m of the line; a
        // feed gap hops that band and the crossing is then invisible (race
        // 181: the boat crossed, the line was armed, nothing scored). A
        // segment that straddles the line cannot be hopped.
        if (havePrev && segXing(legPrevLat, legPrevLon, lat, lon,
                                fin->latA, fin->lonA, fin->latB, fin->lonB)) {
            finished = true;
            endEpoch = now;
            uploadPending = true;
            logEvent("FINISH", now, "seg");
            logPass("[RACE] finished (line cross, segment)", now);
        } else if (near && finLastSide != 0 && side != 0 && side != finLastSide) {
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
        // The finish leg consumes fixes too: keep the crossing segment live
        // for the next pass. This block returns before the mark block that
        // normally maintains legPrev, so without this the segment would never
        // advance past the arming fix.
        legPrevLat = lat;
        legPrevLon = lon;
        havePrev = true;
        if (side != 0) finLastSide = side;
        return;
    }

    // --- Pass recognition (SPEC-mark-passing.md, locked): two traverses. ---
    if (legIdx != progIdx) legReset(progIdx);
    {
        const RaceMark& cur = raceSession.marks[progIdx];
        // Start pins are not roundings: sail past them, never score them.
        if (cur.type == RaceMarkStart || cur.type == RaceMarkNone) {
            scorePass(progIdx, now, "pin", ' ');
            legPrevLat = lat;
            legPrevLon = lon;
            havePrev = true;
            return;
        }
        const bool isGate = (cur.type == RaceMarkGate && gateSibling(progIdx) >= 0);
        if (isGate) {
            // Cut the pair segment: that IS the gate pass.
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
            if (gateThreaded) {
                scorePass(progIdx, now, "gate", ' ');
                legPrevLat = lat;
                legPrevLon = lon;
                havePrev = true;
                return;
            }
        } else {
            // A = previous node (start-line centre for mark 0), B = this
            // mark, C = next node (next mark/centre or finish centre).
            // Work in ENU meters about B: axes, traverses, sides, trends.
            double alat, alon;
            if (progIdx == 0) {
                if (raceSession.startLine.valid) {
                    alat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
                    alon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
                } else {
                    alat = cur.lat;
                    alon = cur.lon; // degenerate A: entry pre-latched in legReset
                }
            } else {
                targetPoint(progIdx - 1, alat, alon);
            }
            double clat, clon;
            const bool haveNext = nextTargetPoint(progIdx, clat, clon);
            double Ax, Ay, Cx, Cy, P1x, P1y;
            toBoatFrame(cur.lat, cur.lon, alat, alon, Ax, Ay);
            if (haveNext) {
                toBoatFrame(cur.lat, cur.lon, clat, clon, Cx, Cy);
            } else {
                Cx = -Ax;
                Cy = -Ay; // no exit geometry: assume the reversal
            }
            toBoatFrame(cur.lat, cur.lon, lat, lon, P1x, P1y);
            double P0x = P1x, P0y = P1y;
            if (havePrev) toBoatFrame(cur.lat, cur.lon, legPrevLat, legPrevLon, P0x, P0y);
            const double dA = sqrt((P1x - Ax) * (P1x - Ax) + (P1y - Ay) * (P1y - Ay));
            const double dB = sqrt(P1x * P1x + P1y * P1y);
            if (emaDA < 0.0) {
                emaDA = dA;
                emaDB = dB;
            } else {
                if (dA > emaDA) {
                    if (growA < 255) growA++;
                } else if (dA < emaDA) {
                    growA = 0;
                }
                if (dB > emaDB) {
                    if (growB < 255) growB++;
                } else if (dB < emaDB) {
                    growB = 0;
                }
                emaDA = 0.5 * emaDA + 0.5 * dA;
                emaDB = 0.5 * emaDB + 0.5 * dB;
            }
            const bool leavingA = growA >= LEAVE_N;
            const bool leavingB = growB >= LEAVE_N;
            // Axes: entry A→B, exit B→C. Degenerate A (== B): no entry line.
            const double iL = sqrt(Ax * Ax + Ay * Ay);
            const double oL = sqrt(Cx * Cx + Cy * Cy);
            const double uix = iL > 1.0 ? -Ax / iL : 0.0;
            const double uiy = iL > 1.0 ? -Ay / iL : 0.0;
            const double uox = oL > 1.0 ? Cx / oL : (iL > 1.0 ? -uix : 0.0);
            const double uoy = oL > 1.0 ? Cy / oL : (iL > 1.0 ? -uiy : 1.0);
            const bool haveEntry = iL > 1.0;
            // Late join: the leg opens past the entry line already.
            if (legFresh && haveEntry && !entryLatched && (P1x * uix + P1y * uiy) > 0.0) {
                entryLatched = true;
                bufferedSerialPrintln("[RACE] entry assumed (late join)");
            }
            legFresh = false;
            // Entry: cut the entry traverse while leaving A.
            if (havePrev && haveEntry && !entryLatched && leavingA &&
                segXingXY(P0x, P0y, P1x, P1y, -uiy * TRAV_HALF_M, uix * TRAV_HALF_M,
                          uiy * TRAV_HALF_M, -uix * TRAV_HALF_M) &&
                (P1x - P0x) * uix + (P1y - P0y) * uiy > 0.0) {
                entryLatched = true;
                bufferedSerialPrintln("[RACE] entry latched");
            }
            // Back to A: re-crossing the entry traverse away from B (and not
            // out through the exit) cancels the approach; a new one
            // re-proves itself. Trend-based cancelling broke Windward-Leeward:
            // the whole downwind leg sails back toward A, which is the
            // correct course, not a retreat.
            const bool exitCross =
                segXingXY(P0x, P0y, P1x, P1y, -uoy * TRAV_HALF_M, uox * TRAV_HALF_M,
                          uoy * TRAV_HALF_M, -uox * TRAV_HALF_M) &&
                (P1x - P0x) * uox + (P1y - P0y) * uoy > 0.0;
            if (havePrev && entryLatched && !exitCross &&
                segXingXY(P0x, P0y, P1x, P1y, -uiy * TRAV_HALF_M, uix * TRAV_HALF_M,
                          uiy * TRAV_HALF_M, -uix * TRAV_HALF_M) &&
                (P1x - P0x) * uix + (P1y - P0y) * uiy < 0.0) {
                entryLatched = false;
                bufferedSerialPrintln("[RACE] latch cleared (back to A)");
            }
            // Exit: cut the exit traverse toward C. The crossing latches at
            // once (exact point, exact side); the score fires when leaving B
            // confirms it. Same-fix gating raced the trend: the straddle is
            // gone by the time three rises accumulate, and the pass died.
            // The latch always takes the LATEST toward-C cut: on a reversal
            // the approach ends ON the shared line, and jitter straddles it
            // northward before the rounding — that stale side must not win
            // over the true exit cut.
            if (exitCross) {
                double xt = 0.0;
                double qx = P1x, qy = P1y;
                if (segXingXYT(P0x, P0y, P1x, P1y, -uoy * TRAV_HALF_M, uox * TRAV_HALF_M,
                               uoy * TRAV_HALF_M, -uox * TRAV_HALF_M, xt)) {
                    qx = P0x + (P1x - P0x) * xt;
                    qy = P0y + (P1y - P0y) * xt;
                }
                // Side of the cut: right of the axis facing C = P (CCW).
                const char cut = (qx * uoy + qy * -uox) > 0.0 ? 'P' : 'S';
                if (!exitLatched || cut != exitSide) {
                    exitLatched = true;
                    exitSide = cut;
                    char emsg[48];
                    snprintf(emsg, sizeof(emsg), "[RACE] exit latched cut %c", exitSide);
                    bufferedSerialPrintln(emsg);
                }
            }
            // Turned back through the exit before leaving: cancel the cut.
            if (havePrev && exitLatched &&
                segXingXY(P0x, P0y, P1x, P1y, -uoy * TRAV_HALF_M, uox * TRAV_HALF_M,
                          uoy * TRAV_HALF_M, -uox * TRAV_HALF_M) &&
                (P1x - P0x) * uox + (P1y - P0y) * uoy < 0.0) {
                exitLatched = false;
                exitSide = ' ';
                bufferedSerialPrintln("[RACE] exit cancelled (back to B)");
            }
            if (entryLatched && exitLatched && leavingB) {
                const char sailed = exitSide;
                if ((cur.side == 'P' || cur.side == 'S') && sailed != cur.side) {
                    char wv[16];
                    snprintf(wv, sizeof(wv), "%c!%c", cur.side, sailed);
                    logEvent("WRONG", now, wv);
                    wrongFlag = true;
                    char wmsg[64];
                    snprintf(wmsg, sizeof(wmsg),
                             "[RACE] wrong side %c!%c (cut %c)",
                             cur.side, sailed, sailed);
                    bufferedSerialPrintln(wmsg);
                    // Three wrongs on the SAME mark is not a mistake, it is
                    // the engine failing to re-arm — stop the run right here
                    // rather than watch it fail for another three minutes.
                    if (wrongRunMark == progIdx) {
                        if (wrongRun < 255) wrongRun++;
                    } else {
                        wrongRunMark = progIdx;
                        wrongRun = 1;
                    }
                    if (wrongRun >= 3) {
                        raceMarkBadRun("3 wrongs on one mark", progIdx,
                                       raceSession.markCount, started, finished,
                                       ocs, lat, lon, gps.location.age());
                        return;
                    }
                    legReset(progIdx);
                    entryLatched = true; // arrival proven: the re-round needs only a clean exit
                    legPrevLat = lat;
                    legPrevLon = lon;
                    havePrev = true;
                    return;
                }
                scorePass(progIdx, now, "mark", sailed);
                legPrevLat = lat;
                legPrevLon = lon;
                havePrev = true;
                return;
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
