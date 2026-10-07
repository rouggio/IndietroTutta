#include "screen_race.h"
#include "screens.h"
#include "race_session.h"
#include "backend.h"

#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern TFT_eSPI tft;

static const uint16_t RBG = TFT_BLACK;
static const uint16_t RFG = TFT_WHITE;
static const uint16_t RDIM = 0x8410; // neutral gray
static const uint16_t RBOAT = TFT_YELLOW;

// Map area (landscape 320x240). Text rows update every pass with padded
// strings (no clears); the map layer redraws at most 1Hz (region clear is
// acceptable off the 200ms path) or when the course changes.
static const int MAP_X = 6;
static const int MAP_Y = 26;
static const int MAP_W = 308;
static const int MAP_H = 140;

static const double DEG_M = 111320.0;

static int lastRaceCourseKey = -2; // sessionId+version fingerprint, -2 = none
static unsigned long lastMapDraw = 0;

static int raceCourseKey()
{
    if (!raceSession.valid) return -1;
    return (int)(raceSession.sessionId * 31 + raceSession.courseVersion);
}

// Project wind-frame-free absolute course into the map rect.
// Equirectangular around the course center; uniform scale, north up.
struct RaceProj {
    double lat0 = 0.0;
    double lon0 = 0.0;
    double cosLat = 1.0;
    double scale = 1.0; // px per degree lat
    int ox = 0;         // rect origin px (map-relative)
    int oy = 0;
    bool ok = false;
};

static void buildProjection(RaceProj& p)
{
    double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
    int n = 0;
    auto eat = [&](double lat, double lon) {
        if (lat < minLat) minLat = lat;
        if (lat > maxLat) maxLat = lat;
        if (lon < minLon) minLon = lon;
        if (lon > maxLon) maxLon = lon;
        n++;
    };
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        eat(raceSession.marks[i].lat, raceSession.marks[i].lon);
    }
    if (raceSession.startLine.valid) {
        eat(raceSession.startLine.latA, raceSession.startLine.lonA);
        eat(raceSession.startLine.latB, raceSession.startLine.lonB);
    }
    if (raceSession.finishLine.valid && !raceSession.finishSameAsStart) {
        eat(raceSession.finishLine.latA, raceSession.finishLine.lonA);
        eat(raceSession.finishLine.latB, raceSession.finishLine.lonB);
    }
    if (n == 0) {
        p.ok = false;
        return;
    }
    // Include the boat so it never leaves the frame (pad when alone).
    p.lat0 = (minLat + maxLat) / 2.0;
    p.lon0 = (minLon + maxLon) / 2.0;
    p.cosLat = cos(p.lat0 * M_PI / 180.0);
    if (p.cosLat < 0.2) p.cosLat = 0.2;
    double spanLat = maxLat - minLat;
    double spanLon = (maxLon - minLon) * p.cosLat;
    if (spanLat < 0.0005) spanLat = 0.0005;
    if (spanLon < 0.0005) spanLon = 0.0005;
    const int pad = 10;
    const double sx = (MAP_W - 2 * pad) / spanLat;
    const double sy = (MAP_H - 2 * pad) / spanLon;
    p.scale = sx < sy ? sx : sy;
    p.ox = MAP_X + MAP_W / 2;
    p.oy = MAP_Y + MAP_H / 2;
    p.ok = true;
}

static void projToPx(const RaceProj& p, double lat, double lon, int& x, int& y)
{
    const double dE = (lon - p.lon0) * p.cosLat;
    const double dN = lat - p.lat0;
    x = (int)(p.ox + dE * p.scale);
    y = (int)(p.oy - dN * p.scale);
}

static uint16_t markColor(RaceMarkType t)
{
    switch (t) {
        case RaceMarkStart: return TFT_GREEN;
        case RaceMarkGate: return 0xC0A0; // amber-ish purple stand-in (no alpha)
        case RaceMarkFinish: return TFT_RED;
        default: return TFT_ORANGE;
    }
}

static void drawBoatAt(int x, int y, double cogDeg, bool hasCog)
{
    const double a = hasCog ? cogDeg * M_PI / 180.0 : 0.0;
    // Triangle pointing along COG (screen: 0° = up). Size ~7px.
    const int s = 7;
    const double dx = sin(a), dy = -cos(a);
    const double px = cos(a), py = sin(a);
    const int x1 = (int)(x + dx * s), y1 = (int)(y + dy * s);
    const int x2 = (int)(x - dx * s * 0.7 + px * s * 0.6), y2 = (int)(y - dy * s * 0.7 + py * s * 0.6);
    const int x3 = (int)(x - dx * s * 0.7 - px * s * 0.6), y3 = (int)(y - dy * s * 0.7 - py * s * 0.6);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, RBOAT);
}

static void drawWindArrow(int cx, int cy, int windDir)
{
    // Points where the wind comes FROM. 0° = up.
    const double a = windDir * M_PI / 180.0;
    const int len = 14;
    const int x2 = (int)(cx + sin(a) * len), y2 = (int)(cy - cos(a) * len);
    tft.drawLine(cx, cy, x2, y2, RDIM);
    // Arrowhead at the tip (toward the source).
    const double ha = 0.5;
    const int hx1 = (int)(x2 - sin(a - ha) * 5), hy1 = (int)(y2 + cos(a - ha) * 5);
    const int hx2 = (int)(x2 - sin(a + ha) * 5), hy2 = (int)(y2 + cos(a + ha) * 5);
    tft.drawLine(x2, y2, hx1, hy1, RDIM);
    tft.drawLine(x2, y2, hx2, hy2, RDIM);
}

static void drawRaceMap(TinyGPSPlus& gps)
{
    tft.fillRect(MAP_X, MAP_Y, MAP_W, MAP_H, RBG);
    tft.drawRect(MAP_X, MAP_Y, MAP_W, MAP_H, RDIM);
    if (!raceSession.valid) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(RDIM, RBG);
        tft.drawString("NO COURSE", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2, 4);
        tft.drawString("assign a session", MAP_X + MAP_W / 2, MAP_Y + MAP_H / 2 + 28, 2);
        return;
    }
    RaceProj p;
    buildProjection(p);
    if (!p.ok) return;

    // Legs: consecutive marks in order.
    int px = 0, py = 0, qx = 0, qy = 0;
    for (uint8_t i = 0; i + 1 < raceSession.markCount; i++) {
        projToPx(p, raceSession.marks[i].lat, raceSession.marks[i].lon, px, py);
        projToPx(p, raceSession.marks[i + 1].lat, raceSession.marks[i + 1].lon, qx, qy);
        tft.drawLine(px, py, qx, qy, RDIM);
    }
    // Start/finish segments.
    if (raceSession.startLine.valid) {
        const RaceSeg& s = raceSession.startLine;
        projToPx(p, s.latA, s.lonA, px, py);
        projToPx(p, s.latB, s.lonB, qx, qy);
        tft.drawWideLine(px, py, qx, qy, 3, TFT_GREEN);
    }
    if (raceSession.finishLine.valid) {
        const RaceSeg& s = raceSession.finishSameAsStart ? raceSession.startLine : raceSession.finishLine;
        projToPx(p, s.latA, s.lonA, px, py);
        projToPx(p, s.latB, s.lonB, qx, qy);
        tft.drawWideLine(px, py, qx, qy, 2, TFT_RED);
    }
    // Marks: circles + index. Next mark (marks[0] in Step 3) brighter.
    char num[4];
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        const RaceMark& m = raceSession.marks[i];
        projToPx(p, m.lat, m.lon, px, py);
        int rPx = (int)(m.r / DEG_M * p.scale);
        if (rPx < 3) rPx = 3;
        if (rPx > 40) rPx = 40;
        tft.drawCircle(px, py, rPx, markColor(m.type));
        snprintf(num, sizeof(num), "%d", i + 1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(i == 0 ? RFG : RDIM, RBG);
        tft.drawString(num, px, py - rPx - 8, 2);
    }
    // Wind arrow, top-right inside the map.
    drawWindArrow(MAP_X + MAP_W - 24, MAP_Y + 22, raceSession.windDir);
    // Boat.
    if (gps.location.isValid()) {
        projToPx(p, gps.location.lat(), gps.location.lng(), px, py);
        drawBoatAt(px, py, gps.course.deg(), gps.course.isValid());
    }
}

static void drawRaceText(TinyGPSPlus& gps)
{
    // Header: tag left, countdown center, sats right. Fixed-width fields,
    // padded — overwrite in place, no clears.
    const bool isRace = strcmp(raceSession.mode, "race") == 0;
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(raceSession.valid ? (isRace ? "RACE" : "PRAC") : "----", 8, 4, 2);

    char cd[12];
    const long now = raceGpsEpoch(gps);
    if (raceSession.valid && raceSession.startTime > 0 && now > 0) {
        const long rem = (raceSession.startTime + raceSession.startOffsetSec) - now;
        if (rem >= 0) {
            snprintf(cd, sizeof(cd), " %2ld:%02ld  ", rem / 60, rem % 60);
        } else {
            const long el = -rem;
            snprintf(cd, sizeof(cd), "GO+%2ld:%02ld", el / 60, el % 60);
        }
    } else {
        snprintf(cd, sizeof(cd), " --:--   ");
    }
    tft.setTextColor(RFG, RBG);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(cd, 160, 2, 4);

    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(TR_DATUM);
    if (gps.satellites.isValid()) {
        char sats[8];
        snprintf(sats, sizeof(sats), "S%02d", gps.satellites.value());
        tft.drawString(sats, 312, 4, 2);
    } else {
        tft.drawString("S--", 312, 4, 2);
    }

    // Next-mark row (Step 3: marks[0]; sequencing arrives in Step 5).
    tft.setTextColor(RFG, RBG);
    tft.setTextDatum(TL_DATUM);
    char row[32];
    if (!raceSession.valid || raceSession.markCount == 0) {
        snprintf(row, sizeof(row), "NO COURSE            ");
    } else if (!gps.location.isValid()) {
        snprintf(row, sizeof(row), "NO FIX               ");
    } else {
        const RaceMark& m = raceSession.marks[0];
        const double dist = TinyGPSPlus::distanceBetween(
            gps.location.lat(), gps.location.lng(), m.lat, m.lon);
        double brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), m.lat, m.lon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        snprintf(row, sizeof(row), "1%c %3d %4dm      ",
                 m.side ? m.side : 'P', (int)brg, (int)dist);
    }
    tft.drawString(row, 8, 178, 4);

    // Hint bar.
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("L Next", 8, 235, 2);
    tft.setTextDatum(BR_DATUM);
    tft.drawString("R Sync", tft.width() - 8, 235, 2);
}

void drawScreenRace(TinyGPSPlus &gps, bool requiresInit)
{
    if (requiresInit) {
        tft.fillScreen(RBG);
        lastRaceCourseKey = -2; // force map redraw
    }
    const int key = raceCourseKey();
    const bool courseChanged = (key != lastRaceCourseKey);
    if (courseChanged) {
        lastRaceCourseKey = key;
    }
    if (requiresInit || courseChanged || millis() - lastMapDraw > 1000) {
        drawRaceMap(gps);
        lastMapDraw = millis();
    }
    drawRaceText(gps);
}

void screenRaceButton(Button button, ButtonEvent event)
{
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        nextScreen();
        return;
    }
    // Right short: re-poll health now (pull a freshly pushed session
    // without waiting for the 30s tick).
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        backendPollHealthNow();
        return;
    }
}
