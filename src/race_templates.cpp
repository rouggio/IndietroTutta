#include "race_templates.h"
#include "race_session.h"
#include "race_run.h"
#include "config.h"
#include "serial_buffer.h"
#include "server_link.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

static Tpl tplPool[TPL_MAX];
static bool tplHave = false;

bool tplReady() { return tplHave; }

uint8_t tplCount()
{
    if (!tplHave) return 0;
    uint8_t n = 0;
    for (uint8_t i = 0; i < TPL_MAX; i++) if (tplPool[i].used) n++;
    return n;
}

const Tpl* tplGet(uint8_t i)
{
    uint8_t n = 0;
    for (uint8_t k = 0; k < TPL_MAX; k++) {
        if (!tplPool[k].used) continue;
        if (n == i) return &tplPool[k];
        n++;
    }
    return nullptr;
}

static uint8_t markTypeFromTpl(const char* t)
{
    // Mirrors race_session markTypeFrom: None/Start/Single/Gate/Finish.
    if (!t) return 0;
    if (strcmp(t, "start") == 0) return 1;
    if (strcmp(t, "mark") == 0) return 2;
    if (strcmp(t, "gate") == 0) return 3;
    if (strcmp(t, "finish") == 0) return 4;
    return 0;
}

static void tplClear()
{
    for (uint8_t i = 0; i < TPL_MAX; i++) tplPool[i].used = false;
}

// Fill one pool slot from a parsed template object (preset or DB row).
// Returns false when the slot list is full.
static bool tplFill(JsonObject o, long id, const char* key)
{
    Tpl* dst = nullptr;
    for (uint8_t i = 0; i < TPL_MAX; i++) {
        if (!tplPool[i].used) {
            dst = &tplPool[i];
            break;
        }
    }
    if (!dst) return false;
    dst->id = id;
    strncpy(dst->key, key ? key : "", sizeof(dst->key) - 1);
    const char* name = o["name"] | "Course";
    strncpy(dst->name, name, sizeof(dst->name) - 1);
    dst->markCount = 0;
    JsonArray marks = o["marks"].as<JsonArray>();
    for (JsonObject m : marks) {
        if (dst->markCount >= TPL_MARKS) break;
        TplMark& d = dst->marks[dst->markCount];
        d.x = m["x"] | 0.0f;
        d.y = m["y"] | 0.0f;
        d.r = m["r"] | 30.0f;
        const char* side = m["side"] | "P";
        d.side = side[0] ? side[0] : 'P';
        d.type = markTypeFromTpl(m["type"] | "");
        const char* gate = m["gate"] | "";
        strncpy(d.gate, gate, sizeof(d.gate) - 1);
        dst->markCount++;
    }
    auto fillSeg = [](TplSeg& s, JsonObject so) {
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

// Backend-task context: pull presets + user templates (heap JSON docs,
// ~2KB each — safe off the task stack).
static void tplFetchOnce(const String& url, bool preset)
{
    if (WiFi.status() != WL_CONNECTED) return;
    ServerLink link;
    if (!link.begin(url)) return;
    link.http.addHeader("DeviceId", String(WiFi.macAddress()));
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        link.http.end();
        return;
    }
    String body = link.http.getString();
    link.http.end();
    if (body.length() == 0 || body.length() > 16384) return;
    DynamicJsonDocument doc(12288);
    if (deserializeJson(doc, body)) return;
    if (preset) {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject o : arr) {
            const char* key = o["key"] | "";
            tplFill(o, 0, key);
        }
    } else {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject o : arr) {
            const long id = o["id"] | 0L;
            if (id <= 0) continue;
            tplFill(o, id, nullptr);
        }
    }
}

void tplFetch()
{
    tplClear();
    tplFetchOnce(serverBaseUrl() + "/templates/presets", true);
    tplFetchOnce(serverBaseUrl() + "/templates", false);
    tplHave = true;
    bufferedSerialPrintln("[TPL] library ready");
}

// Wind-frame resolve (mirror of the backend/js math, scale 1 — builder
// bakes scale into the model). Lines always square to the wind.
static void tplResolvePt(double originLat, double originLon, int windDir,
                         double x, double y, double& lat, double& lon)
{
    const double t = windDir * M_PI / 180.0;
    const double cosLat = cos(originLat * M_PI / 180.0);
    const double E = x * cos(t) + y * sin(t);
    const double N = -x * sin(t) + y * cos(t);
    lat = originLat + N / 111320.0;
    lon = originLon + E / (111320.0 * cosLat);
}

static void tplResolveSeg(double originLat, double originLon, int windDir,
                          const TplSeg& s, double& latA, double& lonA,
                          double& latB, double& lonB)
{
    const double cx = (s.ax + s.bx) / 2.0, cy = (s.ay + s.by) / 2.0;
    const double len = hypot(s.bx - s.ax, s.by - s.ay);
    const double bdeg = fmod(windDir + 90.0 + 360.0, 360.0);
    const double brad = bdeg * M_PI / 180.0;
    double clat, clon;
    tplResolvePt(originLat, originLon, windDir, cx, cy, clat, clon);
    const double cosLat = cos(originLat * M_PI / 180.0);
    const double half = len / 2.0;
    const double dLa = (half * cos(brad)) / 111320.0;
    const double dLo = (half * sin(brad)) / (111320.0 * cosLat);
    latA = clat - dLa;
    lonA = clon - dLo;
    latB = clat + dLa;
    lonB = clon + dLo;
}

bool tplStartSession(uint8_t i, double boatLat, double boatLon, long nowEpoch)
{
    const Tpl* t = tplGet(i);
    if (!t || nowEpoch <= 0) return false;
    const int wind = raceSession.valid ? raceSession.windDir
                    : (raceSession.envWindSpeed > 0 ? raceSession.envWindDir : 0);
    // Reference: start-line center in wind-frame (origin for OOTB lines),
    // else the origin itself.
    double refX = 0.0, refY = 0.0;
    if (t->startLine.valid) {
        refX = (t->startLine.ax + t->startLine.bx) / 2.0;
        refY = (t->startLine.ay + t->startLine.by) / 2.0;
    }
    // Desired start center: 20m upwind of the boat (toward windDir).
    const double wb = wind * M_PI / 180.0;
    const double scLat = boatLat + (20.0 * cos(wb)) / 111320.0;
    const double scLon = boatLon + (20.0 * sin(wb)) / (111320.0 * cos(boatLat * M_PI / 180.0));
    // Origin = start center minus the rotated reference offset.
    const double wt = wind * M_PI / 180.0;
    const double Eoff = refX * cos(wt) + refY * sin(wt);
    const double Noff = -refX * sin(wt) + refY * cos(wt);
    const double originLat = scLat - Noff / 111320.0;
    const double originLon = scLon - Eoff / (111320.0 * cos(scLat * M_PI / 180.0));

    RaceSession next;
    next.valid = true;
    next.sessionId = -1; // local only: uploads ack-drop, NVS untouched
    strncpy(next.mode, "practice", sizeof(next.mode) - 1);
    next.startTime = 0;
    next.startOffsetSec = 0;
    next.windDir = wind;
    next.courseVersion = raceSession.valid ? raceSession.courseVersion + 1 : 1;
    next.markCount = 0;
    for (uint8_t k = 0; k < t->markCount && k < 10; k++) {
        const TplMark& s = t->marks[k];
        RaceMark& d = next.marks[k];
        tplResolvePt(originLat, originLon, wind, s.x, s.y, d.lat, d.lon);
        d.r = s.r;
        d.side = s.side;
        d.type = (RaceMarkType)s.type;
        strncpy(d.gate, s.gate, sizeof(d.gate) - 1);
        next.markCount++;
    }
    if (t->startLine.valid) {
        tplResolveSeg(originLat, originLon, wind, t->startLine,
                      next.startLine.latA, next.startLine.lonA,
                      next.startLine.latB, next.startLine.lonB);
        next.startLine.valid = true;
    }
    if (t->finishLine.sameAsStart) {
        next.finishSameAsStart = true;
        next.finishLine = next.startLine;
    } else if (t->finishLine.valid) {
        next.finishSameAsStart = false;
        tplResolveSeg(originLat, originLon, wind, t->finishLine,
                      next.finishLine.latA, next.finishLine.lonA,
                      next.finishLine.latB, next.finishLine.lonB);
        next.finishLine.valid = true;
    }
    next.envWindDir = raceSession.envWindDir;   // struct starts zeroed here
    next.envWindSpeed = raceSession.envWindSpeed;
    raceSession = next;
    raceRunReset();
    racePracticeStart(nowEpoch + 10); // 10-second start, immediately
    bufferedSerialPrintln("[TPL] local session started");
    return true;
}

bool tplRepeatSession(double boatLat, double boatLon, long nowEpoch)
{
    // Repeat = same absolute course rigid-shifted so the start reference
    // (line center, else mark #1) sits 20m upwind of the boat, fresh +10s
    // gun. No wind-frame needed: translation preserves the shape.
    if (!raceSession.valid || raceSession.markCount == 0 || nowEpoch <= 0) return false;
    const int wind = raceSession.windDir != 0 ? raceSession.windDir
                     : (raceSession.envWindSpeed > 0 ? raceSession.envWindDir : 0);
    const double wb = wind * M_PI / 180.0;
    const double scLat = boatLat + (20.0 * cos(wb)) / 111320.0;
    const double scLon = boatLon + (20.0 * sin(wb)) / (111320.0 * cos(boatLat * M_PI / 180.0));
    double refLat, refLon;
    if (raceSession.startLine.valid) {
        refLat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
        refLon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
    } else {
        refLat = raceSession.marks[0].lat;
        refLon = raceSession.marks[0].lon;
    }
    const double dLat = scLat - refLat, dLon = scLon - refLon;
    RaceSession next = raceSession; // struct copy, then shift geometry
    for (uint8_t k = 0; k < next.markCount; k++) {
        next.marks[k].lat += dLat;
        next.marks[k].lon += dLon;
    }
    if (next.startLine.valid) {
        next.startLine.latA += dLat;
        next.startLine.lonA += dLon;
        next.startLine.latB += dLat;
        next.startLine.lonB += dLon;
    }
    if (next.finishLine.valid && !next.finishSameAsStart) {
        next.finishLine.latA += dLat;
        next.finishLine.lonA += dLon;
        next.finishLine.latB += dLat;
        next.finishLine.lonB += dLon;
    }
    if (next.finishSameAsStart) next.finishLine = next.startLine;
    next.sessionId = -1; // repeat is local (uploads ack-drop)
    strncpy(next.mode, "practice", sizeof(next.mode) - 1);
    next.startTime = 0;
    next.startOffsetSec = 0;
    next.courseVersion = raceSession.courseVersion + 1;
    raceSession = next;
    raceRunReset();
    racePracticeStart(nowEpoch + 10);
    bufferedSerialPrintln("[TPL] session repeated at boat");
    return true;
}
