#include "gps_mock.h"
#include "config.h"
#include "serial_buffer.h"
#include "race_run.h"
#include "race_session.h"
#include "server_link.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// 5s-cached flag so the 200ms UI path never blocks on NVS.
static bool mockCache = false;
static unsigned long mockCacheAt = 0;
static volatile bool mockOn = false;

static bool readMockFlag()
{
    Preferences prefs;
    if (!prefs.begin("mock", true)) return false;
    const bool v = prefs.getBool("on", false);
    prefs.end();
    return v;
}

bool gpsMockActive()
{
    const unsigned long now = millis();
    if (now - mockCacheAt > 5000) {
        mockCacheAt = now;
        mockCache = readMockFlag();
        mockOn = mockCache;
    }
    return mockOn;
}

void gpsMockSet(bool on)
{
    Preferences prefs;
    if (prefs.begin("mock", false)) {
        prefs.putBool("on", on);
        prefs.end();
    }
    mockCache = on;
    mockOn = on;
    mockCacheAt = millis();
    bufferedSerialPrintln(on ? "[MOCK] mock GPS on" : "[MOCK] mock GPS off");
}

static uint8_t nmeaChecksum(const char* s)
{
    uint8_t cs = 0;
    while (*s) cs ^= (uint8_t)(*s++);
    return cs;
}

// Decimal degrees → NMEA dm.mmmm + hemisphere. Returns chars written.
static void fmtLat(double lat, char* buf, size_t n, char& hemi)
{
    hemi = lat >= 0 ? 'N' : 'S';
    const double a = fabs(lat);
    const int d = (int)a;
    snprintf(buf, n, "%02d%07.4f", d, (a - d) * 60.0);
}

static void fmtLon(double lon, char* buf, size_t n, char& hemi)
{
    hemi = lon >= 0 ? 'E' : 'W';
    const double a = fabs(lon);
    const int d = (int)a;
    snprintf(buf, n, "%03d%07.4f", d, (a - d) * 60.0);
}

static void feedSentence(TinyGPSPlus& gps, const char* body)
{
    gps.encode('$');
    uint8_t cs = 0;
    for (const char* p = body; *p; p++) {
        cs ^= (uint8_t)(*p);
        gps.encode(*p);
    }
    char tail[8];
    snprintf(tail, sizeof(tail), "*%02X\r\n", cs);
    for (const char* p = tail; *p; p++) gps.encode(*p);
}

// Inverse civil date (Hinnant civil_from_days) for stamping wander fixes.
static void epochToYMDHMS(long epoch, int& Y, int& M, int& D, int& h, int& mi, int& s)
{
    long z = epoch / 86400L + 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    D = (int)(doy - (153 * mp + 2) / 5 + 1);
    M = (int)(mp < 10 ? mp + 3 : mp - 9);
    Y = (int)(y + (M <= 2 ? 1 : 0));
    const long sod = epoch % 86400L;
    h = (int)(sod / 3600L);
    mi = (int)((sod % 3600L) / 60L);
    s = (int)(sod % 60L);
}

// Wander mode: no scripted run and no race in progress → random-walk the
// boat around its anchor (last known fix, 150m leash, ~1.5kn, smooth
// heading wander). Keeps the map/countdown alive indoors between runs.
static double wLat = 0.0, wLon = 0.0, wHead = 0.0;
static double aLat = 0.0, aLon = 0.0;
static bool wInit = false;
static long mockClock = 0; // UTC epoch for wander stamps ( else millis-based)

static void feedWander(TinyGPSPlus& gps, double lat, double lon, double speedKn,
                       double courseDeg, long epoch)
{
    int Y, M, D, h, mi, s;
    epochToYMDHMS(epoch, Y, M, D, h, mi, s);
    char latB[16], lonB[16], ns = 'N', ew = 'E';
    fmtLat(lat, latB, sizeof(latB), ns);
    fmtLon(lon, lonB, sizeof(lonB), ew);
    char rmc[128], gga[128];
    snprintf(rmc, sizeof(rmc), "GPRMC,%02d%02d%02d.00,A,%s,%c,%s,%c,%.1f,%.1f,%02d%02d%02d,,",
             h, mi, s, latB, ns, lonB, ew, speedKn, courseDeg, D, M, Y % 100);
    snprintf(gga, sizeof(gga), "GPGGA,%02d%02d%02d.00,%s,%c,%s,%c,1,09,0.8,5.0,M,,,,",
             h, mi, s, latB, ns, lonB, ew);
    feedSentence(gps, rmc);
    feedSentence(gps, gga);
}

// Shared NMEA feed for server-driven fixes (scripted runs + server
// wander): ISO serverTime → RMC+GGA sentences. False on bad timestamps.
static void wanderTick(TinyGPSPlus& gps); // defined below; server-first arbiter calls it
static bool feedServerFix(TinyGPSPlus& gps, double lat, double lon,
                          double speed, double course, const char* st)
{
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, s = 0;
    if (!st || sscanf(st, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &mi, &s) < 6)
        return false;
    char latB[16], lonB[16], ns = 'N', ew = 'E';
    fmtLat(lat, latB, sizeof(latB), ns);
    fmtLon(lon, lonB, sizeof(lonB), ew);
    char rmc[128], gga[128];
    snprintf(rmc, sizeof(rmc), "GPRMC,%02d%02d%02d.00,A,%s,%c,%s,%c,%.1f,%.1f,%02d%02d%02d,,",
             h, mi, s, latB, ns, lonB, ew, speed, course, D, M, Y % 100);
    snprintf(gga, sizeof(gga), "GPGGA,%02d%02d%02d.00,%s,%c,%s,%c,1,09,0.8,5.0,M,,,,",
             h, mi, s, latB, ns, lonB, ew);
    feedSentence(gps, rmc);
    feedSentence(gps, gga);
    return true;
}

// Server wander (GET /sim/wander): the committee's random-walk brain.
// True when a fix was fed; false (no anchor/offline) → the caller falls
// back to the local wanderTick below.
static bool pollServerWander(TinyGPSPlus& gps)
{
    if (WiFi.status() != WL_CONNECTED) return false;
    ServerLink link;
    if (!link.begin(serverBaseUrl() + "/sim/wander?deviceId=" + String(WiFi.macAddress())))
        return false;
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        link.http.end();
        return false;
    }
    String payload = link.http.getString();
    link.http.end();
    if (payload.isEmpty() || payload.length() > 2048) return false;
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) return false;
    if (!(doc["wander"] | false)) return false;
    const bool ok = feedServerFix(gps,
                                  doc["lat"] | 0.0, doc["lon"] | 0.0,
                                  doc["speed"] | 0.0, doc["course"] | 0.0,
                                  doc["serverTime"] | "");
    if (ok) {
        const long e = raceGpsEpoch(gps);
        if (e > 0) mockClock = e;
        bufferedSerialPrintln("[MOCK] wander fix (server)");
    }
    return ok;
}

// Wander arbitration: a mid-race dropout holds the last fix instead of
// inventing one; otherwise the server brain leads, local walk follows.
static void wanderMaybe(TinyGPSPlus& gps)
{
    if (raceRunStarted() && !raceRunFinished()) return;
    if (!pollServerWander(gps)) wanderTick(gps);
}

static void wanderTick(TinyGPSPlus& gps)
{
    if (!wInit) {
        // Anchor: the course first (the only certain open water — sessions
        // live at sea), else live fix, else last coords, else NVS seed.
        // UART is drained in mock, so a fix may never arrive; the session
        // fallback keeps wander from deadlocking on an empty ocean.
        double slat = 0.0, slon = 0.0;
        bool haveSeed = false;
        if (raceSession.valid) {
            if (raceSession.startLine.valid) {
                slat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
                slon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
                haveSeed = true;
            } else if (raceSession.markCount > 0) {
                slat = raceSession.marks[0].lat;
                slon = raceSession.marks[0].lon;
                haveSeed = true;
            }
        }
        if (!haveSeed && gps.location.isValid()) {
            slat = gps.location.lat();
            slon = gps.location.lng();
            haveSeed = true;
        }
        if (!haveSeed) {
            const double ll = gps.location.lat(), lo = gps.location.lng();
            if (ll != 0.0 || lo != 0.0) {
                slat = ll;
                slon = lo;
                haveSeed = true;
            }
        }
        if (!haveSeed) {
            // Cross-boot fallback: last real fix, throttled-saved by the
            // backend loop (~10min cadence, negligible flash wear).
            Preferences prefs;
            if (prefs.begin("mock", true)) {
                slat = prefs.getDouble("seedLat", 0.0);
                slon = prefs.getDouble("seedLon", 0.0);
                prefs.end();
                haveSeed = (slat != 0.0 || slon != 0.0);
            }
        }
        if (!haveSeed) return;
        aLat = wLat = slat;
        aLon = wLon = slon;
        wHead = gps.course.isValid() ? gps.course.deg() : (double)(esp_random() % 360);
        const long e = raceGpsEpoch(gps);
        mockClock = e > 0 ? e : 946684800L + (long)(millis() / 1000); // else Y2K+uptime
        wInit = true;
        bufferedSerialPrintln("[MOCK] wander anchored");
    } else {
        mockClock += 3; // poll cadence
    }
    // Brisk reach: 6kn with gentle helm; steer home past the 150m leash.
    const double cosLat = cos(aLat * M_PI / 180.0);
    const double dx = (wLon - aLon) * 111320.0 * cosLat;
    const double dy = (wLat - aLat) * 111320.0;
    if (dx * dx + dy * dy > 150.0 * 150.0) {
        double home = TinyGPSPlus::courseTo(wLat, wLon, aLat, aLon);
        if (home < 0) home += 360.0;
        wHead = home;
    } else {
        wHead += (double)((int)(esp_random() % 31)) - 15.0;
        if (wHead < 0) wHead += 360.0;
        if (wHead >= 360.0) wHead -= 360.0;
    }
    const double stepM = 6.0 * 0.514444 * 3.0;
    const double hr = wHead * M_PI / 180.0;
    wLat += (stepM * cos(hr)) / 111320.0;
    wLon += (stepM * sin(hr)) / (111320.0 * cosLat);
    feedWander(gps, wLat, wLon, 6.0, wHead, mockClock);
}

void gpsMockPoll(TinyGPSPlus& gps)
{
    if (!gpsMockActive()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    static unsigned long lastPoll = 0;
    // A scripted run owns the fix exclusively: while its fixes arrive the
    // local wanderer stays silent, whatever the race state (pre-gun
    // practice gets scripts too — racing them against wander mixes positions).
    static unsigned long lastScriptFix = 0;
    const unsigned long now = millis();
    if (now - lastPoll < 3000) return;
    lastPoll = now;
    const bool scriptLive = (now - lastScriptFix < 9000);

    ServerLink link;
    if (!link.begin(serverBaseUrl() + "/sim/next?deviceId=" + String(WiFi.macAddress()))) return;
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        link.http.end();
        if (!scriptLive) wanderMaybe(gps);
        return;
    }
    String payload = link.http.getString();
    link.http.end();
    if (payload.isEmpty() || payload.length() > 2048) return;

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) return;
    if (doc["done"] | false) {
        bufferedSerialPrintln("[MOCK] run finished");
        lastScriptFix = 0; // script over: wander arbitration starts fresh
        wanderMaybe(gps);  // holds mid-race, else server/local wander
        return;
    }
    const double lat = doc["lat"] | 0.0;
    const double lon = doc["lon"] | 0.0;
    const double speed = doc["speed"] | 0.0;   // knots
    const double course = doc["course"] | 0.0; // degrees
    const char* st = doc["serverTime"] | "";
    const char* runId = doc["runId"] | "";
    if (!feedServerFix(gps, lat, lon, speed, course, st)) return;
    lastScriptFix = now;
    bufferedSerialPrintln("[MOCK] fix injected");
    // Keep the wander clock truthful across script→wander handoffs.
    const long scriptEpoch = raceGpsEpoch(gps);
    if (scriptEpoch > 0) mockClock = scriptEpoch;

    // Echo every 5th fix back (~15s): pipeline proof without touching tracks.
    static int pollCount = 0;
    if (runId[0] && (++pollCount % 5) == 0) {
        ServerLink echoLink;
        if (echoLink.begin(serverBaseUrl() + "/sim/echo")) {
            echoLink.http.addHeader("Content-Type", "application/json");
            String body = String("{\"runId\":\"") + runId +
                          String("\",\"t\":") + (long)(doc["t"] | 0) +
                          String(",\"lat\":") + String(lat, 7) +
                          String(",\"lon\":") + String(lon, 7) +
                          String(",\"speed\":") + String(speed, 1) +
                          String(",\"course\":") + String(course, 1) + "}";
            echoLink.http.POST(body);
            echoLink.http.end();
        }
    }
}
