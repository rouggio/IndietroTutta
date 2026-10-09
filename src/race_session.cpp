#include "race_session.h"
#include "race_run.h"
#include "serial_buffer.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

RaceSession raceSession;

static long daysFromCivil(int y, int m, int d); // defined below

// millis()-based offset, calibrated from GPS time whenever a fix carries
// one. Lets countdowns tick through fix gaps instead of freezing.
static long wallOffsetSec = 0;
static bool wallHave = false;

long raceGpsEpoch(TinyGPSPlus& gps)
{
    if (!gps.date.isValid() || !gps.time.isValid()) {
        return -1;
    }
    const long days = daysFromCivil(gps.date.year(), gps.date.month(), gps.date.day());
    const long epoch = days * 86400L + (long)gps.time.hour() * 3600L + (long)gps.time.minute() * 60L + (long)gps.time.second();
    wallOffsetSec = epoch - (long)(millis() / 1000);
    wallHave = true;
    return epoch;
}

long raceWallEpoch()
{
    if (!wallHave) return -1;
    return (long)(millis() / 1000) + wallOffsetSec;
}

// Howard Hinnant's days_from_civil: days since 1970-01-01, proleptic Gregorian.
static long daysFromCivil(int y, int m, int d)
{
    y -= m <= 2 ? 1 : 0;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned mp = (unsigned)(m + (m > 2 ? -3 : 9));
    const unsigned doy = (153 * mp + 2) / 5 + (unsigned)(d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

long raceIsoEpoch(const char* iso)
{
    if (!iso || !iso[0]) {
        return -1;
    }
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, s = 0;
    // "2026-10-10T14:00:00.000Z" — trailing millis/Z tolerated (ignored)
    if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &mi, &s) < 6) {
        return -1;
    }
    if (Y < 2020 || Y > 2100 || M < 1 || M > 12 || D < 1 || D > 31 ||
        h > 23 || mi > 59 || s > 60) {
        return -1;
    }
    return daysFromCivil(Y, M, D) * 86400L + (long)h * 3600L + (long)mi * 60L + (long)s;
}

static RaceMarkType markTypeFrom(const char* t)
{
    if (!t) return RaceMarkNone;
    if (strcmp(t, "start") == 0) return RaceMarkStart;
    if (strcmp(t, "mark") == 0) return RaceMarkSingle;
    if (strcmp(t, "gate") == 0) return RaceMarkGate;
    if (strcmp(t, "finish") == 0) return RaceMarkFinish;
    return RaceMarkNone;
}

static void fillSeg(RaceSeg& seg, JsonObject o)
{
    if (o.isNull()) {
        seg.valid = false;
        return;
    }
    seg.latA = o["latA"] | 0.0;
    seg.lonA = o["lonA"] | 0.0;
    seg.latB = o["latB"] | 0.0;
    seg.lonB = o["lonB"] | 0.0;
    seg.valid = true;
}

// Fill `out` from a parsed session object. Returns false when unusable.
static bool sessionFromJson(JsonObject sess, RaceSession& out)
{
    if (sess.isNull()) {
        return false;
    }
    RaceSession tmp;
    tmp.sessionId = sess["id"] | 0L;
    if (tmp.sessionId <= 0) {
        return false;
    }
    const char* mode = sess["mode"] | "";
    strncpy(tmp.mode, mode, sizeof(tmp.mode) - 1);
    const char* status = sess["status"] | "";
    strncpy(tmp.status, status, sizeof(tmp.status) - 1);
    const char* st = sess["startTime"] | (const char*)nullptr;
    tmp.startTime = st ? raceIsoEpoch(st) : 0;
    tmp.startOffsetSec = sess["startOffsetSec"] | 0L;
    tmp.windDir = sess["windDir"] | 0;
    tmp.windSpeed = sess["windSpeed"] | 0;
    tmp.courseVersion = sess["courseVersion"] | 0;

    JsonArray marks = sess["marks"].as<JsonArray>();
    uint8_t n = 0;
    for (JsonObject m : marks) {
        if (n >= 10) break;
        RaceMark& dst = tmp.marks[n];
        dst.lat = m["lat"] | 0.0;
        dst.lon = m["lon"] | 0.0;
        dst.r = m["r"] | 30.0f;
        const char* side = m["side"] | "P";
        dst.side = side[0] ? side[0] : 'P';
        dst.type = markTypeFrom(m["type"] | "");
        const char* gate = m["gate"] | "";
        strncpy(dst.gate, gate, sizeof(dst.gate) - 1);
        n++;
    }
    tmp.markCount = n;

    fillSeg(tmp.startLine, sess["startLine"].as<JsonObject>());
    JsonObject fin = sess["finishLine"].as<JsonObject>();
    const char* sameAs = fin["sameAs"] | "";
    if (strcmp(sameAs, "start") == 0) {
        tmp.finishSameAsStart = true;
        tmp.finishLine = tmp.startLine; // resolved mirror
    } else {
        tmp.finishSameAsStart = false;
        fillSeg(tmp.finishLine, fin);
    }

    // Committee signals (already filtered to this device + fleet by the
    // backend). RAM only — re-pulled every health poll.
    tmp.sigCount = 0;
    JsonArray sigs = sess["signals"].as<JsonArray>();
    for (JsonObject s : sigs) {
        if (tmp.sigCount >= 8) break;
        RaceSignal& dst = tmp.signals[tmp.sigCount];
        dst.id = s["id"] | 0L;
        if (dst.id <= 0) continue;
        const char* kind = s["kind"] | "";
        strncpy(dst.kind, kind, sizeof(dst.kind) - 1);
        const char* detail = s["detail"] | "";
        strncpy(dst.detail, detail, sizeof(dst.detail) - 1);
        tmp.sigCount++;
    }

    tmp.valid = true;
    out = tmp;
    return true;
}

static bool sameGeometry(const RaceSession& a, const RaceSession& b)
{
    return a.valid && b.valid &&
           a.sessionId == b.sessionId &&
           a.courseVersion == b.courseVersion;
}

// Venue wind piggyback: health top-level "wind" object (dir = FROM
// degrees, speed = knots). RAM only, refreshed every poll; an absent key
// (offline body, old backend) keeps the last value.
static void applyEnvWind(JsonObject w)
{
    if (w.isNull()) {
        return;
    }
    const int d = w["dir"] | 0;
    const int s = w["speed"] | 0;
    if (d == raceSession.envWindDir && s == raceSession.envWindSpeed) {
        return;
    }
    raceSession.envWindDir = ((d % 360) + 360) % 360;
    raceSession.envWindSpeed = s;
    bufferedSerialPrintln(String("[RACE] env wind ") + raceSession.envWindDir + "° " + raceSession.envWindSpeed + " kn");
}

bool raceSessionParse(const char* healthBody)
{
    if (!healthBody || !healthBody[0]) {
        return false;
    }
    // Heap-backed: keeps the 12k backend-task stack untouched.
    // 6K: session geometry + up to 20 piggybacked committee signals.
    DynamicJsonDocument doc(6144);
    if (deserializeJson(doc, healthBody)) {
        return false;
    }
    applyEnvWind(doc["wind"]);
    JsonObject sess = doc["session"];
    if (sess.isNull()) {
        // Explicitly unassigned (backend reachable, body parsed): drop a
        // stale backend course so the display goes clean. Offline (no body
        // at all) keeps the cache; local practice (id -1) is never cleared.
        if (raceSession.valid && raceSession.sessionId > 0) {
            raceSession.valid = false;
            Preferences prefs;
            if (prefs.begin("race", false)) {
                prefs.remove("sess");
                prefs.end();
            }
            raceRunReset();
            bufferedSerialPrintln("[RACE] session unassigned, cache cleared");
            return true;
        }
        return false;
    }
    RaceSession next;
    if (!sessionFromJson(sess, next)) {
        return false;
    }
    const bool geometryChanged = !sameGeometry(raceSession, next);
    const bool volatileChanged = raceSession.valid &&
                                 (raceSession.startTime != next.startTime ||
                                  raceSession.startOffsetSec != next.startOffsetSec ||
                                  strcmp(raceSession.mode, next.mode) != 0 ||
                                  strcmp(raceSession.status, next.status) != 0);
    // Env wind lives in raceSession (applyEnvWind may have just refreshed
    // it) — sessionFromJson starts from a zeroed struct, so carry it over.
    next.envWindDir = raceSession.envWindDir;
    next.envWindSpeed = raceSession.envWindSpeed;
    raceSession = next;
    if (geometryChanged || volatileChanged || !raceSession.valid) {
        raceSessionSave();
        bufferedSerialPrintln("[RACE] session updated");
        return true;
    }
    return false;
}

bool raceSessionSave()
{
    if (!raceSession.valid) {
        return false;
    }
    // Re-serialize minimally (mirror of the backend shape).
    String j = String("{\"id\":") + raceSession.sessionId +
               String(",\"mode\":\"") + raceSession.mode + "\"" +
               String(",\"status\":\"") + raceSession.status + "\"" +
               String(",\"startTime\":") + raceSession.startTime +
               String(",\"startOffsetSec\":") + raceSession.startOffsetSec +
               String(",\"windDir\":") + raceSession.windDir +
               String(",\"windSpeed\":") + raceSession.windSpeed +
               String(",\"courseVersion\":") + raceSession.courseVersion +
               String(",\"marks\":[");
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        const RaceMark& m = raceSession.marks[i];
        if (i > 0) j += ",";
        j += String("{\"lat\":") + String(m.lat, 7) +
             String(",\"lon\":") + String(m.lon, 7) +
             String(",\"r\":") + String(m.r, 1) +
             String(",\"side\":\"") + m.side + "\"" +
             String(",\"type\":") + (int)m.type +
             String(",\"gate\":\"") + m.gate + "\"}";
    }
    const auto serSeg = [](String& j, const char* key, const RaceSeg& s) {
        j += String(",\"") + key + "\":";
        if (!s.valid) {
            j += "null";
            return;
        }
        j += String("{\"latA\":") + String(s.latA, 7) +
             String(",\"lonA\":") + String(s.lonA, 7) +
             String(",\"latB\":") + String(s.latB, 7) +
             String(",\"lonB\":") + String(s.lonB, 7) + "}";
    };
    j += "]";
    serSeg(j, "startLine", raceSession.startLine);
    if (raceSession.finishSameAsStart) {
        j += ",\"finishLine\":{\"sameAs\":\"start\"}";
    } else {
        serSeg(j, "finishLine", raceSession.finishLine);
    }
    j += "}";    // Lines are re-fetched from the backend on every poll while online;
    // the NVS copy only needs marks + timing to stay useful offline.
    // (Full line persistence arrives with the Step 5 engine.)

    Preferences prefs;
    if (!prefs.begin("race", false)) {
        return false;
    }
    const bool ok = prefs.putString("sess", j) > 0;
    prefs.end();
    return ok;
}

bool raceSessionLoad()
{
    Preferences prefs;
    if (!prefs.begin("race", true)) {
        return false;
    }
    const String j = prefs.getString("sess", "");
    prefs.end();
    if (j.isEmpty()) {
        return false;
    }
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, j)) {
        return false;
    }
    // NVS shape uses numeric types; adapt into the backend shape.
    JsonObject root = doc.as<JsonObject>();
    if (root.isNull() || !root.containsKey("id")) {
        return false;
    }
    RaceSession tmp;
    tmp.sessionId = root["id"] | 0L;
    if (tmp.sessionId <= 0) {
        return false;
    }
    const char* mode = root["mode"] | "";
    strncpy(tmp.mode, mode, sizeof(tmp.mode) - 1);
    const char* status = root["status"] | "";
    strncpy(tmp.status, status, sizeof(tmp.status) - 1);
    tmp.startTime = root["startTime"] | 0L;
    tmp.startOffsetSec = root["startOffsetSec"] | 0L;
    tmp.windDir = root["windDir"] | 0;
    tmp.windSpeed = root["windSpeed"] | 0;
    tmp.courseVersion = root["courseVersion"] | 0;
    JsonArray marks = root["marks"].as<JsonArray>();
    uint8_t n = 0;
    for (JsonObject m : marks) {
        if (n >= 10) break;
        RaceMark& dst = tmp.marks[n];
        dst.lat = m["lat"] | 0.0;
        dst.lon = m["lon"] | 0.0;
        dst.r = m["r"] | 30.0f;
        const char* side = m["side"] | "P";
        dst.side = side[0] ? side[0] : 'P';
        dst.type = (RaceMarkType)(m["type"] | 0);
        const char* gate = m["gate"] | "";
        strncpy(dst.gate, gate, sizeof(dst.gate) - 1);
        n++;
    }
    tmp.markCount = n;
    tmp.valid = true;
    fillSeg(tmp.startLine, root["startLine"].as<JsonObject>());
    JsonObject fin = root["finishLine"].as<JsonObject>();
    const char* sameAs = fin["sameAs"] | "";
    if (strcmp(sameAs, "start") == 0) {
        tmp.finishSameAsStart = true;
        tmp.finishLine = tmp.startLine;
    } else {
        tmp.finishSameAsStart = false;
        fillSeg(tmp.finishLine, fin);
    }
    raceSession = tmp;
    bufferedSerialPrintln("[RACE] session restored from NVS");
    return true;
}

bool raceSessionLive()
{
    return raceSession.valid && strcmp(raceSession.status, "live") == 0;
}
