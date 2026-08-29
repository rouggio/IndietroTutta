#include "race_store.h"
#include <TinyGPSPlus.h>
#include <ArduinoJson.h>
#include <time.h>

static String activeRaceId = "";
static String activeRaceName = "";
static String activeRaceStatus = "";
static unsigned long activeStartMs = 0;
static String activeCourseName = "";
static RaceMark courseMarks[10];
static int courseMarkCount = 0;
static int currentMarkIdx = 0;

static unsigned long serverTimeAtSyncMs = 0;
static unsigned long millisAtSync = 0;

static unsigned long parseIsoToMs(const String& iso) {
    if (iso.length() < 19) return 0;
    int y, mo, d, h, mi, s;
    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    struct tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = 0;
    time_t t = mktime(&tm);
    if (t == -1) return 0;
    return (unsigned long)t * 1000UL;
}

void raceClear() {
    activeRaceId = "";
    activeRaceName = "";
    activeRaceStatus = "";
    activeStartMs = 0;
    activeCourseName = "";
    courseMarkCount = 0;
    currentMarkIdx = 0;
    for (int i=0;i<10;i++) courseMarks[i] = {0,0,30,"P"};
}

bool raceHasActive() {
    return activeRaceId.length() > 0 && activeStartMs != 0;
}

String raceGetName() { return activeRaceName; }
String raceGetStatus() { return activeRaceStatus; }
unsigned long raceGetStartTimeMs() { return activeStartMs; }
String raceGetCourseName() { return activeCourseName; }
int raceGetMarkCount() { return courseMarkCount; }
int raceGetCurrentMarkIndex() { return currentMarkIdx; }

bool raceGetNextMark(RaceMark &out) {
    if (courseMarkCount == 0 || currentMarkIdx >= courseMarkCount) return false;
    out = courseMarks[currentMarkIdx];
    return true;
}

void raceAdvanceMark() {
    if (currentMarkIdx < courseMarkCount) currentMarkIdx++;
}

bool raceCheckPass(TinyGPSPlus &gps) {
    if (!gps.location.isValid()) return false;
    if (courseMarkCount == 0 || currentMarkIdx >= courseMarkCount) return false;
    RaceMark &m = courseMarks[currentMarkIdx];
    double dist = TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(), m.lat, m.lon);
    if (dist <= m.radius) {
        // For side G (gate either) we accept any; for P/S we could check relative bearing but for now just distance
        raceAdvanceMark();
        return true;
    }
    return false;
}

void raceUpdateFromHealth(const String& id, const String& name, const String& status, const String& startTimeIso, const String& courseName, const String& courseMarksJson, const String& serverTimeIso) {
    if (id.length() == 0) {
        raceClear();
        return;
    }
    bool isNewRace = (id != activeRaceId);
    activeRaceId = id;
    activeRaceName = name;
    activeRaceStatus = status;
    activeCourseName = courseName;
    activeStartMs = parseIsoToMs(startTimeIso);

    // Parse marks JSON array if provided
    if (courseMarksJson.length() > 0) {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, courseMarksJson);
        if (!err && doc.is<JsonArray>()) {
            JsonArray arr = doc.as<JsonArray>();
            int idx = 0;
            for (JsonObject obj : arr) {
                if (idx >= 10) break;
                double lat = obj["lat"] | obj["latOffset"] | 0.0;
                double lon = obj["lon"] | obj["lonOffset"] | 0.0;
                // If latOffset/lonOffset, they are offsets from course center - for device we treat as absolute if lat/lon missing
                // Marks from builder already have absolute lat/lon
                if (lat == 0 && lon == 0) continue;
                courseMarks[idx].lat = lat;
                courseMarks[idx].lon = lon;
                courseMarks[idx].radius = obj["radius"] | 30;
                const char* side = obj["side"] | "P";
                courseMarks[idx].side = String(side);
                idx++;
            }
            if (isNewRace || idx != courseMarkCount) {
                courseMarkCount = idx;
                currentMarkIdx = 0;
            }
        }
    }

    unsigned long serverMs = parseIsoToMs(serverTimeIso);
    if (serverMs != 0) {
        serverTimeAtSyncMs = serverMs;
        millisAtSync = millis();
    }
}

unsigned long getSyncedTimeMs(TinyGPSPlus &gps) {
    if (gps.date.isValid() && gps.time.isValid()) {
        struct tm tm = {};
        tm.tm_year = gps.date.year() - 1900;
        tm.tm_mon = gps.date.month() - 1;
        tm.tm_mday = gps.date.day();
        tm.tm_hour = gps.time.hour();
        tm.tm_min = gps.time.minute();
        tm.tm_sec = gps.time.second();
        tm.tm_isdst = 0;
        time_t t = mktime(&tm);
        if (t != -1) return (unsigned long)t * 1000UL + gps.time.centisecond() * 10;
    }
    if (serverTimeAtSyncMs != 0) {
        return serverTimeAtSyncMs + (millis() - millisAtSync);
    }
    return 0;
}

String formatCountdown(unsigned long remainingMs) {
    if (remainingMs == 0) return "GO";
    unsigned long s = remainingMs / 1000;
    unsigned long m = s / 60;
    s %= 60;
    char buf[16];
    if (m >= 60) {
        unsigned long h = m / 60;
        m %= 60;
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%lu:%02lu", m, s);
    }
    return String(buf);
}
