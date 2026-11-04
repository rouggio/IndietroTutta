#include "race_courses.h"
#include "race_session.h"
#include "race_run.h"
#include "backend.h"
#include "config.h"
#include "serial_buffer.h"
#include "server_link.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

static Course coursePool[COURSE_MAX];
static bool courseHave = false;

// Fetch state the UI has to know about (written on the backend task).
//
// The picker draws straight out of coursePool while that pool is being
// rebuilt, so two things matter and used to be wrong:
//  - `courseBusy` is set for the (sub-millisecond) clear+fill window, and the
//    page shows "asking backend" instead of rows read mid-rebuild. Before this
//    the clear happened BEFORE the HTTP round-trip, so for 1-2 seconds the UI
//    drawn on a mid-fetch keypress saw zero or half-filled rows: the empty /
//    corrupted course list.
//  - `courseGen` changes only when a fetch actually landed. courseReady() is
//    true across a refresh, so the old `ready changed?` test never fired after
//    a re-fetch and a bad frame stayed on screen until the page was left.
static volatile bool courseBusy = false;
static volatile uint32_t courseGen = 0;

bool courseReady() { return courseHave; }

bool courseFetching() { return courseBusy; }

uint32_t courseGeneration() { return courseGen; }

uint8_t courseCount()
{
    if (!courseHave) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < COURSE_MAX; i++) if (coursePool[i].used) n++;
    return n;
}

const Course* courseGet(uint8_t i)
{
    uint8_t n = 0;
    for (uint8_t k = 0; k < COURSE_MAX; k++) {
        if (!coursePool[k].used) continue;
        if (n == i) return &coursePool[k];
        n++;
    }
    return nullptr;
}

bool courseHaveId(long id)
{
    for (uint8_t k = 0; k < COURSE_MAX; k++) {
        if (coursePool[k].used && coursePool[k].id == id) return true;
    }
    return false;
}

// Remembered course wins; first boot falls back to Windward-Leeward.
uint8_t coursePreselectIndex()
{
    if (practicePrefsValid()) {
        for (uint8_t i = 0; i < COURSE_MAX; i++) {
            const Course* c = courseGet(i);
            if (c && c->id == practicePrefs().courseId) return i;
        }
    }
    for (uint8_t i = 0; i < COURSE_MAX; i++) {
        const Course* c = courseGet(i);
        if (c && strcmp(c->key, "wl") == 0) return i;
    }
    return 0;
}

static uint8_t markTypeFromCourse(const char* t)
{
    // Mirrors race_session markTypeFrom: None/Start/Single/Gate/Finish.
    if (!t) return 0;
    if (strcmp(t, "start") == 0) return 1;
    if (strcmp(t, "mark") == 0) return 2;
    if (strcmp(t, "gate") == 0) return 3;
    if (strcmp(t, "finish") == 0) return 4;
    return 0;
}

static void courseClear()
{
    for (uint8_t i = 0; i < COURSE_MAX; i++) coursePool[i].used = false;
}

// Fill one pool slot from a parsed course row.
// Returns false when the slot list is full.
static bool courseFill(JsonObject o, long id)
{
    Course* dst = nullptr;
    for (uint8_t i = 0; i < COURSE_MAX; i++) {
        if (!coursePool[i].used) {
            dst = &coursePool[i];
            break;
        }
    }
    if (!dst) return false;
    dst->id = id;
    const char* name = o["name"] | "Course";
    strncpy(dst->name, name, sizeof(dst->name) - 1);
    const char* key = o["builtinKey"] | "";
    strncpy(dst->key, key, sizeof(dst->key) - 1);
    dst->markCount = 0;
    JsonArray marks = o["marks"].as<JsonArray>();
    for (JsonObject m : marks) {
        if (dst->markCount >= COURSE_MARKS) break;
        CourseMark& d = dst->marks[dst->markCount];
        d.x = m["x"] | 0.0f;
        d.y = m["y"] | 0.0f;
        d.r = m["r"] | 30.0f;
        const char* side = m["side"] | "P";
        d.side = side[0] ? side[0] : 'P';
        d.type = markTypeFromCourse(m["type"] | "");
        const char* gate = m["gate"] | "";
        strncpy(d.gate, gate, sizeof(d.gate) - 1);
        dst->markCount++;
    }
    auto fillSeg = [](CourseSeg& s, JsonObject so) {
        if (so.isNull()) {
            s.valid = false;
            return;
        }
        if (strcmp(so["sameAs"] | "", "start") == 0) {
            s.valid = false;
            s.sameAsStart = true;
            return;
        }
        s.ax = so["ax"] | 0.0;
        s.ay = so["ay"] | 0.0;
        s.bx = so["bx"] | 0.0;
        s.by = so["by"] | 0.0;
        s.valid = true;
    };
    fillSeg(dst->startLine, o["startLine"].as<JsonObject>());
    fillSeg(dst->finishLine, o["finishLine"].as<JsonObject>());
    dst->used = dst->markCount > 0;
    return dst->used;
}

// Backend-task context: pull the course library (heap JSON docs,
// ~2KB each -> safe off the task stack). One fetch: built-in and user
// courses are all rows of GET /courses, id-keyed (builtinKey marks the
// read-only built-ins, which the device just shows like any other).
// Returns true only when the payload ARRIVED and PARSED: the caller keeps the
// previous library on false, so an offline or cold-backend refresh no longer
// takes the practice setup down with it.
static bool courseFetchOnce(const String& url)
{
    if (WiFi.status() != WL_CONNECTED) return false;
    ServerLink link;
    if (!link.begin(url)) return false;
    link.http.addHeader("DeviceId", String(WiFi.macAddress()));
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        link.http.end();
        return false;
    }
    String body = link.http.getString();
    link.http.end();
    if (body.length() == 0 || body.length() > 16384) return false;
    DynamicJsonDocument doc(12288);
    if (deserializeJson(doc, body)) return false;
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) return false;
    // The whole payload is in RAM and parsed: only NOW is the live pool
    // replaced. `used` goes false first and each slot's is set again on
    // courseFill()'s last line, so a reader sees whole rows or none - never a
    // half-written name - and the busy flag covers even that window.
    courseBusy = true;
    courseClear();
    for (JsonObject o : arr) {
        const long id = o["id"] | 0L;
        if (id <= 0) continue;
        courseFill(o, id);
    }
    courseBusy = false;
    return true;
}

void courseFetch()
{
    const bool ok = courseFetchOnce(serverBaseUrl() + "/courses");
    if (ok) {
        courseHave = true;
        courseGen++;
        bufferedSerialPrintln("[CRS] library ready");
    } else {
        // Keep the pool: the library you already have is better than an empty
        // list, and courseReady() must not claim a fetch that never happened.
        bufferedSerialPrintln("[CRS] fetch failed, keeping RAM library");
    }
}

// Wind-frame resolve (mirror of the backend/js math, scale 1 -> the builder
// bakes scale into the model). Lines always square to the wind.
static void courseResolvePt(double originLat, double originLon, int windDir,
                         double x, double y, double& lat, double& lon)
{
    const double t = windDir * M_PI / 180.0;
    const double cosLat = cos(originLat * M_PI / 180.0);
    const double E = x * cos(t) + y * sin(t);
    const double N = -x * sin(t) + y * cos(t);
    lat = originLat + N / 111320.0;
    lon = originLon + E / (111320.0 * cosLat);
}

static void courseResolveSeg(double originLat, double originLon, int windDir,
                          const CourseSeg& s, double& latA, double& lonA,
                          double& latB, double& lonB)
{
    const double cx = (s.ax + s.bx) / 2.0, cy = (s.ay + s.by) / 2.0;
    const double len = hypot(s.bx - s.ax, s.by - s.ay);
    const double bdeg = fmod(windDir + 90.0 + 360.0, 360.0);
    const double brad = bdeg * M_PI / 180.0;
    double clat, clon;
    courseResolvePt(originLat, originLon, windDir, cx, cy, clat, clon);
    const double cosLat = cos(originLat * M_PI / 180.0);
    const double half = len / 2.0;
    const double dLa = (half * cos(brad)) / 111320.0;
    const double dLo = (half * sin(brad)) / (111320.0 * cosLat);
    latA = clat - dLa;
    lonA = clon - dLo;
    latB = clat + dLa;
    lonB = clon + dLo;
}

// ---- Practice options (remembered across reboots) ------------------------
const int PRACTICE_GUNS[PRACTICE_GUN_N] = {10, 30, 60, 120, 300};
const int PRACTICE_DISTS[PRACTICE_DIST_N] = {10, 20, 30};

static PracticePrefs sPrefs;   // defaults: course -1, gun 30s, distance 20m

void practicePrefsLoad()
{
    Preferences prefs;
    if (!prefs.begin("race", true)) return;
    sPrefs.courseId = (long)prefs.getInt("prefCourse", -1);
    sPrefs.gunSec = prefs.getInt("prefGun", 30);
    sPrefs.distM = prefs.getInt("prefDist", 20);
    sPrefs.placeAhead = prefs.getInt("prefAhead", 0) != 0;
    prefs.end();
    // Snap a stale/hand-edited value back to the offered set.
    unsigned gi = 0, di = 0;
    while (gi + 1 < sizeof(PRACTICE_GUNS) / sizeof(PRACTICE_GUNS[0]) && PRACTICE_GUNS[gi] != sPrefs.gunSec) gi++;
    unsigned dj = 0;
    while (dj + 1 < sizeof(PRACTICE_DISTS) / sizeof(PRACTICE_DISTS[0]) && PRACTICE_DISTS[dj] != sPrefs.distM) dj++;
    sPrefs.gunSec = PRACTICE_GUNS[gi];
    sPrefs.distM = PRACTICE_DISTS[dj];
}

const PracticePrefs& practicePrefs() { return sPrefs; }

void practicePrefsSave(long courseId)
{
    sPrefs.courseId = courseId;
    Preferences prefs;
    if (prefs.begin("race", false)) {
        prefs.putInt("prefCourse", (int)sPrefs.courseId);
        prefs.putInt("prefGun", sPrefs.gunSec);
        prefs.putInt("prefDist", sPrefs.distM);
        prefs.putInt("prefAhead", sPrefs.placeAhead ? 1 : 0);
        prefs.end();
    }
}

bool practicePrefsValid() { return sPrefs.courseId > 0; }

// Cycling the options does not persist: the values become the new default
// only when a session is actually started (practicePrefsSave).
void practicePrefsSetGun(int sec) { sPrefs.gunSec = sec; }
void practicePrefsSetDist(int m) { sPrefs.distM = m; }
void practicePrefsSetAhead(bool ahead) { sPrefs.placeAhead = ahead; }

// Wind for placement: session wind wins, then the venue wind carried by the
// health piggyback, then the boat's own bearing (no wind known => assume the
// boat lies in the no-go angle, so its heading points at the wind source).
int practiceWindDir(int boatCourse)
{
    if (raceSession.valid && raceSession.windSpeed > 0) return raceSession.windDir;
    if (raceSession.envWindSpeed > 0) return raceSession.envWindDir;
    return boatCourse >= 0 ? boatCourse : 0;
}

int practiceWindSpeed()
{
    if (raceSession.valid && raceSession.windSpeed > 0) return raceSession.windSpeed;
    return raceSession.envWindSpeed > 0 ? raceSession.envWindSpeed : 0;
}

bool courseStartSession(uint8_t i, double boatLat, double boatLon, long nowEpoch, int boatCourse)
{
    const Course* t = courseGet(i);
    if (!t || nowEpoch <= 0) return false;
    const PracticePrefs& p = practicePrefs();
    const int wind = practiceWindDir(boatCourse);
    // PLACEMENT (the options row): the shape is laid against this one angle and
    // the start line goes p.distM metres from the boat along it.
    //   Upwind -> the wind (session wind, then venue wind, then the boat's own
    //             bearing - the fallback practiceWindDir() already applies);
    //   Ahead  -> the boat's own heading: put the course where you point.
    //             No COG yet (drifting, no fix) degrades to upwind.
    // The session's windDir becomes the placement angle on purpose: the backend
    // re-resolves the shape from courseId + origin + windDir and the device
    // adopts those marks, so geometry and payload must share one frame.
    const int place = (p.placeAhead && boatCourse >= 0) ? boatCourse : wind;
    // Reference: start-line center in course-frame (the course origin sits at
    // the leeward edge), else the origin itself.
    double refX = 0.0, refY = 0.0;
    if (t->startLine.valid) {
        refX = (t->startLine.ax + t->startLine.bx) / 2.0;
        refY = (t->startLine.ay + t->startLine.by) / 2.0;
    }
    // Desired start center: p.distM metres from the boat along `place`.
    const double wb = place * M_PI / 180.0;
    const double scLat = boatLat + ((double)p.distM * cos(wb)) / 111320.0;
    const double scLon = boatLon + ((double)p.distM * sin(wb)) / (111320.0 * cos(boatLat * M_PI / 180.0));
    // Origin = start center minus the rotated reference offset.
    const double wt = place * M_PI / 180.0;
    const double Eoff = refX * cos(wt) + refY * sin(wt);
    const double Noff = -refX * sin(wt) + refY * cos(wt);
    const double originLat = scLat - Noff / 111320.0;
    const double originLon = scLon - Eoff / (111320.0 * cos(scLat * M_PI / 180.0));

    RaceSession next;
    next.valid = true;
    next.sessionId = -1; // until the backend hands back a real id
    strncpy(next.mode, "practice", sizeof(next.mode) - 1);
    strncpy(next.status, "scheduled", sizeof(next.status) - 1);
    next.startTime = nowEpoch + p.gunSec; // the gun the countdown runs to
    next.windDir = place; // the frame the shape was just resolved in (see above)
    next.windSpeed = practiceWindSpeed();
    next.courseVersion = raceSession.valid ? raceSession.courseVersion + 1 : 1;
    next.markCount = 0;
    for (uint8_t k = 0; k < t->markCount && k < 10; k++) {
        const CourseMark& s = t->marks[k];
        RaceMark& d = next.marks[k];
        courseResolvePt(originLat, originLon, place, s.x, s.y, d.lat, d.lon);
        d.r = s.r;
        d.side = s.side;
        d.type = (RaceMarkType)s.type;
        strncpy(d.gate, s.gate, sizeof(d.gate) - 1);
        next.markCount++;
    }
    if (t->startLine.valid) {
        courseResolveSeg(originLat, originLon, place, t->startLine,
                      next.startLine.latA, next.startLine.lonA,
                      next.startLine.latB, next.startLine.lonB);
        next.startLine.valid = true;
    }
    if (t->finishLine.sameAsStart) {
        next.finishSameAsStart = true;
        next.finishLine = next.startLine;
    } else if (t->finishLine.valid) {
        next.finishSameAsStart = false;
        courseResolveSeg(originLat, originLon, place, t->finishLine,
                      next.finishLine.latA, next.finishLine.lonA,
                      next.finishLine.latB, next.finishLine.lonB);
        next.finishLine.valid = true;
    }
    next.envWindDir = raceSession.envWindDir;   // struct starts zeroed here
    next.envWindSpeed = raceSession.envWindSpeed;
    raceSession = next;
    raceRunReset();
    // Real session on the backend: the id comes back asynchronously and is
    // adopted by the UI thread (courseCreatedPoll). Until then the device
    // still runs this local copy, so a failed POST never costs the sailor
    // the start.
    backendCreateSession(t->id, nowEpoch + p.gunSec, originLat, originLon, place, next.windSpeed);
    practicePrefsSave(t->id);
    bufferedSerialPrintln("[CRS] session started");
    return true;
}

// Abandon the live session (if any): ask the backend to mark it abandoned so
// the web agrees with the device, then forget it locally.
void courseAbandonSession()
{
    if (raceSession.valid && raceSession.sessionId > 0) {
        backendAbandonSession(raceSession.sessionId);
        // Do not let the next few heartbeats hand the same session back: the
        // abandon is asynchronous, and re-adopting it made the page flicker
        // through the race screen before it finally went empty.
        raceSessionSuppressPush(raceSession.sessionId);
    }
    raceRunReset();
    raceSession.valid = false;
    raceSession.sessionId = -1;
    raceSession.markCount = 0;
    bufferedSerialPrintln("[CRS] session abandoned");
}

// Repeat = abandon whatever is running, then re-create the remembered course
// with the remembered gun/distance, re-oriented from where the boat is now.
bool courseRepeatSession(double boatLat, double boatLon, long nowEpoch, int boatCourse)
{
    if (nowEpoch <= 0) return false;
    if (!practicePrefsValid()) return false;
    const Course* t = nullptr;
    for (uint8_t i = 0; i < COURSE_MAX && !t; i++) {
        const Course* c = courseGet(i);
        if (c && c->id == practicePrefs().courseId) t = c;
    }
    if (!t) return false;
    courseAbandonSession();
    // courseStartSession takes an index, not a pointer: find it again.
    uint8_t idx = 0;
    for (uint8_t i = 0; i < COURSE_MAX; i++) {
        const Course* c = courseGet(i);
        if (c && c->id == practicePrefs().courseId) { idx = i; break; }
    }
    return courseStartSession(idx, boatLat, boatLon, nowEpoch, boatCourse);
}
