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

// Frame: everything except the header (top) and the hint bar (bottom).
// Wire area holds the course; the solid strip holds PRAC tag + next data.
static const int HDR_H = 28;
static const int MAP_X = 2;
static const int MAP_Y = 30;
static const int MAP_W = 316;
static const int WIRE_Y1 = 178;   // wire area: MAP_Y..WIRE_Y1
static const int STRIP_Y0 = 180;  // data strip: STRIP_Y0..208
static const int STRIP_Y1 = 208;

static const double DEG_M = 111320.0;

// View state (RAM only). RR cycles: 0 north-up, 1 bearing-up (next
// destination up), 2 best-fit (0°/90° whichever fills the screen).
static uint8_t viewMode = 0;
// Effective rotation applied by the last map draw (deg clockwise that
// points up) + effective mode, shared with the text row.
static double gRotEff = 0.0;
static int gEffMode = 0;

static int lastRaceCourseKey = -2; // sessionId+version fingerprint, -2 = none
static unsigned long lastMapDraw = 0;

static int raceCourseKey()
{
    if (!raceSession.valid) return -1;
    return (int)(raceSession.sessionId * 31 + raceSession.courseVersion);
}

// Projection: equirectangular around the course, uniform scale, with an
// optional rotation for course-up (heading points up).
struct RaceProj {
    double lat0 = 0.0;
    double lon0 = 0.0;
    double cosLat = 1.0;
    double cosH = 1.0;
    double sinH = 0.0;
    double rcx = 0.0; // rotated bbox center (course units)
    double rcy = 0.0;
    double scale = 1.0; // px per course unit
    int ox = 0;
    int oy = 0;
    bool ok = false;
};

static void buildProjection(RaceProj& p, double rotDeg)
{
    const double H = rotDeg * M_PI / 180.0;
    p.cosH = cos(H);
    p.sinH = sin(H);
    // Gather course points in raw units (deg lat, deg lon * cosLat).
    double raw[14][2];
    int n = 0;
    // Center on the course bbox first for a stable cosLat.
    double minLat = 1e9, maxLat = -1e9, minLon = 1e9, maxLon = -1e9;
    auto eatBox = [&](double lat, double lon) {
        if (lat < minLat) minLat = lat;
        if (lat > maxLat) maxLat = lat;
        if (lon < minLon) minLon = lon;
        if (lon > maxLon) maxLon = lon;
    };
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        eatBox(raceSession.marks[i].lat, raceSession.marks[i].lon);
    }
    if (raceSession.startLine.valid) {
        eatBox(raceSession.startLine.latA, raceSession.startLine.lonA);
        eatBox(raceSession.startLine.latB, raceSession.startLine.lonB);
    }
    if (raceSession.finishLine.valid && !raceSession.finishSameAsStart) {
        eatBox(raceSession.finishLine.latA, raceSession.finishLine.lonA);
        eatBox(raceSession.finishLine.latB, raceSession.finishLine.lonB);
    }
    if (minLat > 900.0) {
        p.ok = false;
        return;
    }
    p.lat0 = (minLat + maxLat) / 2.0;
    p.lon0 = (minLon + maxLon) / 2.0;
    p.cosLat = cos(p.lat0 * M_PI / 180.0);
    if (p.cosLat < 0.2) p.cosLat = 0.2;

    auto push = [&](double lat, double lon) {
        if (n >= 14) return;
        const double dE = (lon - p.lon0) * p.cosLat;
        const double dN = lat - p.lat0;
        raw[n][0] = dE * p.cosH - dN * p.sinH;
        raw[n][1] = dE * p.sinH + dN * p.cosH;
        n++;
    };
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        push(raceSession.marks[i].lat, raceSession.marks[i].lon);
    }
    if (raceSession.startLine.valid) {
        push(raceSession.startLine.latA, raceSession.startLine.lonA);
        push(raceSession.startLine.latB, raceSession.startLine.lonB);
    }
    if (raceSession.finishLine.valid && !raceSession.finishSameAsStart) {
        push(raceSession.finishLine.latA, raceSession.finishLine.lonA);
        push(raceSession.finishLine.latB, raceSession.finishLine.lonB);
    }
    double rminx = 1e9, rmaxx = -1e9, rminy = 1e9, rmaxy = -1e9;
    for (int i = 0; i < n; i++) {
        if (raw[i][0] < rminx) rminx = raw[i][0];
        if (raw[i][0] > rmaxx) rmaxx = raw[i][0];
        if (raw[i][1] < rminy) rminy = raw[i][1];
        if (raw[i][1] > rmaxy) rmaxy = raw[i][1];
    }
    double spanX = rmaxx - rminx, spanY = rmaxy - rminy;
    if (spanX < 0.0005) spanX = 0.0005;
    if (spanY < 0.0005) spanY = 0.0005;
    const int pad = 10;
    const int ww = MAP_W - 2 * pad;
    const int wh = (WIRE_Y1 - MAP_Y) - 2 * pad;
    const double sx = ww / spanX;
    const double sy = wh / spanY;
    p.scale = sx < sy ? sx : sy;
    p.rcx = (rminx + rmaxx) / 2.0;
    p.rcy = (rminy + rmaxy) / 2.0;
    p.ox = MAP_X + MAP_W / 2;
    p.oy = MAP_Y + (WIRE_Y1 - MAP_Y) / 2;
    p.ok = true;
}

static void projToPx(const RaceProj& p, double lat, double lon, int& x, int& y)
{
    const double dE = (lon - p.lon0) * p.cosLat;
    const double dN = lat - p.lat0;
    const double er = dE * p.cosH - dN * p.sinH;
    const double nr = dE * p.sinH + dN * p.cosH;
    x = (int)(p.ox + (er - p.rcx) * p.scale);
    y = (int)(p.oy - (nr - p.rcy) * p.scale);
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

static void drawBoatAt(int x, int y, double screenDeg)
{
    // Triangle pointing along the screen heading (0° = up). Size ~7px.
    const double a = screenDeg * M_PI / 180.0;
    const int s = 7;
    const double dx = sin(a), dy = -cos(a);
    const double px = cos(a), py = sin(a);
    const int x1 = (int)(x + dx * s), y1 = (int)(y + dy * s);
    const int x2 = (int)(x - dx * s * 0.7 + px * s * 0.6), y2 = (int)(y - dy * s * 0.7 + py * s * 0.6);
    const int x3 = (int)(x - dx * s * 0.7 - px * s * 0.6), y3 = (int)(y - dy * s * 0.7 - py * s * 0.6);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, RBOAT);
}

static void drawWindArrow(int cx, int cy, double screenDeg)
{
    // Points where the wind comes FROM, in screen frame. Bright white.
    const double a = screenDeg * M_PI / 180.0;
    const int len = 14;
    const int x2 = (int)(cx + sin(a) * len), y2 = (int)(cy - cos(a) * len);
    tft.drawLine(cx, cy, x2, y2, RFG);
    const double ha = 0.5;
    const int hx1 = (int)(x2 - sin(a - ha) * 5), hy1 = (int)(y2 + cos(a - ha) * 5);
    const int hx2 = (int)(x2 - sin(a + ha) * 5), hy2 = (int)(y2 + cos(a + ha) * 5);
    tft.drawLine(x2, y2, hx1, hy1, RFG);
    tft.drawLine(x2, y2, hx2, hy2, RFG);
}

static void drawDashed(int x0, int y0, int x1, int y1, uint16_t color)
{
    const double len = hypot((double)(x1 - x0), (double)(y1 - y0));
    if (len < 2.0) return;
    const double dash = 5.0, gap = 4.0;
    double d = 0.0;
    while (d < len) {
        const double d2 = d + dash < len ? d + dash : len;
        tft.drawLine((int)(x0 + (x1 - x0) * d / len), (int)(y0 + (y1 - y0) * d / len),
                     (int)(x0 + (x1 - x0) * d2 / len), (int)(y0 + (y1 - y0) * d2 / len), color);
        d += dash + gap;
    }
}

// Clamp an off-frame point onto the wire rect border (from the rect center).
// Returns false when already inside.
static bool clampToWire(int cx, int cy, int& x, int& y)
{
    const int x0 = MAP_X + 5, y0 = MAP_Y + 5;
    const int x1 = MAP_X + MAP_W - 5, y1 = WIRE_Y1 - 5;
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) return false;
    const double dx = (double)(x - cx), dy = (double)(y - cy);
    double t = 1e9;
    if (dx > 0) t = fmin(t, (double)(x1 - cx) / dx);
    if (dx < 0) t = fmin(t, (double)(x0 - cx) / dx);
    if (dy > 0) t = fmin(t, (double)(y1 - cy) / dy);
    if (dy < 0) t = fmin(t, (double)(y0 - cy) / dy);
    if (!(t > 0.0) || !(t < 1e8)) return false;
    x = (int)(cx + dx * t);
    y = (int)(cy + dy * t);
    return true;
}

// Next destination: pre-start it's the start (line center, else start
// point, else marks[0]); after the gun it's marks[0] (gates resolve to
// the pair center). Fills tag ("ST" / "1G") and position.
static bool nextDestination(TinyGPSPlus& gps, double& lat, double& lon, char* tag, size_t tagLen)
{
    (void)gps;
    if (!raceSession.valid) return false;
    const long now = raceGpsEpoch(gps);
    const bool preStart = (raceSession.startTime <= 0 || now <= 0 ||
        (raceSession.startTime + raceSession.startOffsetSec) > now);
    if (preStart) {
        if (raceSession.startLine.valid) {
            lat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
            lon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
            snprintf(tag, tagLen, "ST  ");
            return true;
        }
        for (uint8_t i = 0; i < raceSession.markCount; i++) {
            if (raceSession.marks[i].type == RaceMarkStart) {
                lat = raceSession.marks[i].lat;
                lon = raceSession.marks[i].lon;
                snprintf(tag, tagLen, "ST  ");
                return true;
            }
        }
    }
    if (raceSession.markCount == 0) return false;
    uint8_t idx = 0;
    const RaceMark& m = raceSession.marks[idx];
    if (m.type == RaceMarkGate && m.gate[0]) {
        // Pair center: find the sibling buoy.
        for (uint8_t i = 0; i < raceSession.markCount; i++) {
            if (i != idx && raceSession.marks[i].type == RaceMarkGate &&
                strcmp(raceSession.marks[i].gate, m.gate) == 0) {
                lat = (m.lat + raceSession.marks[i].lat) / 2.0;
                lon = (m.lon + raceSession.marks[i].lon) / 2.0;
                snprintf(tag, tagLen, "%d%c  ", idx + 1, m.side ? m.side : 'P');
                return true;
            }
        }
    }
    lat = m.lat;
    lon = m.lon;
    snprintf(tag, tagLen, "%d%c  ", idx + 1, m.side ? m.side : 'P');
    return true;
}

static void drawRaceMap(TinyGPSPlus& gps)
{
    // Wire area only — the strip keeps its text (updated every pass).
    tft.fillRect(MAP_X, MAP_Y, MAP_W, WIRE_Y1 - MAP_Y, RBG);
    tft.drawRect(MAP_X, MAP_Y, MAP_W, WIRE_Y1 - MAP_Y, RDIM);
    tft.drawRect(MAP_X, STRIP_Y0, MAP_W, STRIP_Y1 - STRIP_Y0, RDIM);
    if (!raceSession.valid) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(RDIM, RBG);
        tft.drawString("NO COURSE", MAP_X + MAP_W / 2, MAP_Y + (WIRE_Y1 - MAP_Y) / 2, 4);
        tft.drawString("assign a session", MAP_X + MAP_W / 2, MAP_Y + (WIRE_Y1 - MAP_Y) / 2 + 28, 2);
        return;
    }
    // Effective rotation: 0 north-up, bearing-to-destination up, or best
    // screen fit (0°/90° whichever fills more). Falls back to north-up
    // whenever the fix/destination is missing.
    double rotEff = 0.0;
    int effMode = 0;
    double destLat = 0.0, destLon = 0.0;
    char destTag[8] = {0};
    const bool haveDestHere = nextDestination(gps, destLat, destLon, destTag, sizeof(destTag));
    if (viewMode == 1 && gps.location.isValid() && haveDestHere) {
        double brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), destLat, destLon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        rotEff = brg;
        effMode = 1;
    }
    gRotEff = rotEff;
    gEffMode = effMode;

    RaceProj p;
    if (viewMode == 2) {
        RaceProj p0, p90;
        buildProjection(p0, 0.0);
        buildProjection(p90, 90.0);
        if (p90.ok && (!p0.ok || p90.scale > p0.scale)) {
            p = p90;
            rotEff = 90.0;
        } else {
            p = p0;
            rotEff = 0.0;
        }
        gRotEff = rotEff;
        gEffMode = 2;
    } else {
        buildProjection(p, rotEff);
    }
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
    // Marks: circles + index.
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
    // Wind arrow, top-left inside the frame, bright white (rotated frame).
    const double windScreen = raceSession.windDir - rotEff;
    drawWindArrow(MAP_X + 24, MAP_Y + 22, windScreen);

    // Boat: triangle on the chart, or a projected dot on the frame edge.
    // Dashed yellow line from the boat (or edge dot) to the destination.
    if (gps.location.isValid()) {
        projToPx(p, gps.location.lat(), gps.location.lng(), px, py);
        const int cx = MAP_X + MAP_W / 2, cy = MAP_Y + (WIRE_Y1 - MAP_Y) / 2;
        int bx = px, by = py;
        const bool outside = clampToWire(cx, cy, bx, by);
        double dLat = 0.0, dLon = 0.0;
        char tag[8] = {0};
        if (nextDestination(gps, dLat, dLon, tag, sizeof(tag))) {
            int dx = 0, dy = 0;
            projToPx(p, dLat, dLon, dx, dy);
            drawDashed(bx, by, dx, dy, RBOAT);
        }
        if (outside) {
            tft.drawCircle(bx, by, 4, RBOAT);
        } else {
            const double boatScreen = gps.course.isValid() ? (gps.course.deg() - rotEff) : 0.0;
            drawBoatAt(px, py, boatScreen);
        }
    }
}

static int fontPxH(uint8_t font)
{
    // Classic TFT_eSPI fonts used here: 2 = 16px, 4 = 26px.
    return font == 4 ? 26 : 16;
}

// Change-detect text: redraws (with exact-extent erase) only when the
// content actually changes. All call sites sit on solid backgrounds
// (header black, strip solid), so erase-then-draw never smears.
static void drawSmart(int x, int y, uint8_t font, int datum, uint16_t color,
                      const char* txt, char* last, size_t lastLen, int16_t& lastW)
{
    if (strcmp(txt, last) == 0) return;
    tft.setTextDatum(datum);
    const int h = fontPxH(font);
    int ex = x, ey = y;
    if (datum == TC_DATUM) ex = x - lastW / 2;
    else if (datum == TR_DATUM) ex = x - lastW;
    else if (datum == BL_DATUM) ey = y - h;
    else if (datum == BR_DATUM) { ex = x - lastW; ey = y - h; }
    if (lastW > 0) tft.fillRect(ex, ey, lastW, h, RBG);
    tft.setTextColor(color, RBG);
    lastW = tft.drawString(txt, x, y, font);
    strncpy(last, txt, lastLen - 1);
    last[lastLen - 1] = '\0';
}

static char lastCd[12] = {0};
static int16_t lastCdW = 0;
static char lastTag[8] = {0};
static int16_t lastTagW = 0;
static char lastPrac[8] = {0};
static int16_t lastPracW = 0;
static char lastRow[24] = {0};
static int16_t lastRowW = 0;
static char lastMode[8] = {0};
static int16_t lastModeW = 0;

static void drawRaceText(TinyGPSPlus& gps)
{
    // Header: countdown center, next-passage tag right (bright white).
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

    // Next-passage tag (ST pre-start, else number+side), bright white.
    char tag[8];
    double dLat = 0.0, dLon = 0.0;
    const bool haveDest = nextDestination(gps, dLat, dLon, tag, sizeof(tag));
    tft.setTextColor(RFG, RBG);
    tft.setTextDatum(TR_DATUM);
    if (!raceSession.valid) {
        tft.drawString("----", 312, 2, 4);
    } else {
        tft.drawString(haveDest ? tag : "----", 312, 2, 4);
    }

    // Strip: PRAC/RACE tag bottom-left, next data center, view mode right.
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(TL_DATUM);
    const bool isRace = strcmp(raceSession.mode, "race") == 0;
    tft.drawString(raceSession.valid ? (isRace ? "RACE" : "PRAC") : "----", 8, 184, 2);

    tft.setTextColor(RFG, RBG);
    char bbuf[8], dbuf[14];
    bool showBrg = false;
    // Strip layout changes between centered messages and the split bearing
    // view — clear the text band once on transition (rare, never per-pass).
    static int lastStripMode = -1;
    const int stripMode = (!raceSession.valid || !gps.location.isValid()) ? 0 : (haveDest ? 1 : 0);
    if (stripMode != lastStripMode) {
        tft.fillRect(8, STRIP_Y0, MAP_W - 16, STRIP_Y1 - STRIP_Y0, RBG);
        tft.drawFastHLine(MAP_X, STRIP_Y0, MAP_W, RDIM);
        lastStripMode = stripMode;
    }
    if (!raceSession.valid) {
        tft.setTextDatum(TC_DATUM);
        tft.drawString("NO COURSE       ", 170, 182, 4);
    } else if (!gps.location.isValid()) {
        tft.setTextDatum(TC_DATUM);
        tft.drawString("NO FIX          ", 170, 182, 4);
    } else if (haveDest) {
        const double dist = TinyGPSPlus::distanceBetween(
            gps.location.lat(), gps.location.lng(), dLat, dLon);
        double brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), dLat, dLon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        const long d = (long)dist > 9999 ? 9999 : (long)dist;
        // Bearing right-aligned ending at x=158, degree ring drawn at the
        // fixed slot (the font has no ° glyph), distance from x=178.
        snprintf(bbuf, sizeof(bbuf), "%3d", (int)brg);
        snprintf(dbuf, sizeof(dbuf), "%4ldm   ", d);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(bbuf, 158, 182, 4);
        tft.drawCircle(168, 188, 2, RFG);
        tft.setTextDatum(TL_DATUM);
        tft.drawString(dbuf, 178, 182, 4);
        showBrg = true;
    } else {
        tft.setTextDatum(TC_DATUM);
        tft.drawString(" ---           ", 170, 182, 4);
    }
    if (!showBrg) {
        // Erase a stale ring when leaving the bearing view.
        tft.drawCircle(168, 188, 2, RBG);
    }

    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(TR_DATUM);
    const char* viewTxt = gEffMode == 1 ? "BRG " : (gEffMode == 2 ? "FIT " : "N-UP");
    tft.drawString(viewTxt, 312, 184, 2);

    // Hint bar.
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("L Next", 8, 235, 2);
    tft.setTextDatum(BR_DATUM);
    tft.drawString("R Sync RR View", tft.width() - 8, 235, 2);
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
    // Right long: cycle views — north-up → bearing-up → best-fit.
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        viewMode = (viewMode + 1) % 3;
        lastMapDraw = 0; // force map redraw with the new orientation
        return;
    }
}
