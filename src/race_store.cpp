#include "race_store.h"
#include <TinyGPSPlus.h>
#include <time.h>

static String activeRaceId = "";
static String activeRaceName = "";
static String activeRaceStatus = "";
static unsigned long activeStartMs = 0;
static String activeCourseName = "";

static unsigned long serverTimeAtSyncMs = 0;
static unsigned long millisAtSync = 0;

static unsigned long parseIsoToMs(const String& iso) {
    if (iso.length() < 19) return 0;
    // Expect YYYY-MM-DDTHH:MM:SS[.sss][Z]
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
    // ESP32 defaults to UTC, so mktime is effectively UTC
    time_t t = mktime(&tm);
    if (t == -1) return 0;
    return (unsigned long)t * 1000UL;
}

void raceUpdate(const String& raceJson, const String& courseJson, const String& serverTimeIso) {
    // raceJson is a JSON string for race object or empty; courseJson similar
    // For simplicity, caller passes already-extracted fields via ArduinoJson in backend.cpp
    // This overload is not used; actual update is via detailed fields
}

void raceUpdateDetailed(const String& id, const String& name, const String& status, const String& startTimeIso, const String& courseName, const String& serverTimeIso) {
    activeRaceId = id;
    activeRaceName = name;
    activeRaceStatus = status;
    activeCourseName = courseName;
    activeStartMs = parseIsoToMs(startTimeIso);

    unsigned long serverMs = parseIsoToMs(serverTimeIso);
    if (serverMs != 0) {
        serverTimeAtSyncMs = serverMs;
        millisAtSync = millis();
    }
}

void raceClear() {
    activeRaceId = "";
    activeRaceName = "";
    activeRaceStatus = "";
    activeStartMs = 0;
    activeCourseName = "";
}

bool raceHasActive() {
    return activeRaceId.length() > 0 && activeStartMs != 0;
}

String raceGetName() { return activeRaceName; }
String raceGetStatus() { return activeRaceStatus; }
unsigned long raceGetStartTimeMs() { return activeStartMs; }
String raceGetCourseName() { return activeCourseName; }

// Exposed for backend.cpp to call
void raceUpdateFromHealth(const String& id, const String& name, const String& status, const String& startTimeIso, const String& courseName, const String& serverTimeIso) {
    if (id.length() == 0) {
        raceClear();
        return;
    }
    raceUpdateDetailed(id, name, status, startTimeIso, courseName, serverTimeIso);
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
