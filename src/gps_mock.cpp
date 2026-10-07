#include "gps_mock.h"
#include "config.h"
#include "serial_buffer.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SIM_NEXT_URL BASE_URL "/sim/next?deviceId="

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

void gpsMockPoll(TinyGPSPlus& gps)
{
    if (!gpsMockActive()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    static unsigned long lastPoll = 0;
    const unsigned long now = millis();
    if (now - lastPoll < 3000) return;
    lastPoll = now;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(SIM_NEXT_URL) + String(WiFi.macAddress());
    if (!http.begin(client, url)) return;
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();
    if (payload.isEmpty() || payload.length() > 2048) return;

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, payload)) return;
    if (doc["done"] | false) {
        bufferedSerialPrintln("[MOCK] run finished");
        return; // hold last fix (TinyGPS keeps it until age-out)
    }
    const double lat = doc["lat"] | 0.0;
    const double lon = doc["lon"] | 0.0;
    const double speed = doc["speed"] | 0.0;   // knots
    const double course = doc["course"] | 0.0; // degrees
    const char* st = doc["serverTime"] | "";
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, s = 0;
    if (sscanf(st, "%4d-%2d-%2dT%2d:%2d:%2d", &Y, &M, &D, &h, &mi, &s) < 6) return;

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
    bufferedSerialPrintln("[MOCK] fix injected");
}
