#include "gps_mock.h"
#include "config.h"
#include "serial_buffer.h"
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

// Mock GPS source for indoor testing: fixes come ONLY from the server —
// a scripted run (GET /sim/next) or the server-driven walk
// (GET /sim/wander) — synthesized into NMEA sentences. The device never
// invents positions: with no server answer it holds the last fix.
// The rest of the stack (screens, countdown, pass engine) can't tell.
// Shared NMEA feed for server-driven fixes (scripted runs + server
// wander): ISO serverTime → RMC+GGA sentences. False on bad timestamps.
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

// Single mock poll (1Hz): GET /sim/next answers with a scripted sample,
// the server walk, or 404 (anchorless/offline) → hold the last fix.
// The device never invents positions; mid-race it stays out entirely.
void gpsMockPoll(TinyGPSPlus& gps)
{
    if (!gpsMockActive()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    // NOTE: no mid-race hold here. Mock means testing: freezing the fix
    // for the whole run strands the engine (and the diagnostics Age).
    // A true dropout (no server answer) still holds via the 404 path.
    static unsigned long lastPoll = 0;
    const unsigned long now = millis();
    // 1Hz server poll: consumes 1Hz scripts losslessly, smoother walk.
    if (now - lastPoll < 1000) return;
    lastPoll = now;

    ServerLink link;
    if (!link.begin(serverBaseUrl() + "/sim/next?deviceId=" + String(WiFi.macAddress()))) return;
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        link.http.end();
        return;
    }
    String payload = link.http.getString();
    link.http.end();
    if (payload.isEmpty() || payload.length() > 2048) return;

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) return;
    const double lat = doc["lat"] | 0.0;
    const double lon = doc["lon"] | 0.0;
    const double speed = doc["speed"] | 0.0;   // knots
    const double course = doc["course"] | 0.0; // degrees
    const char* st = doc["serverTime"] | "";
    if (!feedServerFix(gps, lat, lon, speed, course, st)) return;
    const bool wander = doc["wander"] | false;
    const char* runId = wander ? "" : (doc["runId"] | "");
    bufferedSerialPrintln(wander ? "[MOCK] wander fix (server)" : "[MOCK] fix injected");

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
