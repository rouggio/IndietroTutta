#include "screen_race.h"
#include "screens.h"
#include "race_session.h"
#include "backend.h"
#include "gps_mock.h"

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
static const int WIRE_TOP = 58;   // course wire area starts here (speed zone above)
static const int WIRE_Y1 = 178;   // wire area: WIRE_TOP..WIRE_Y1
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
    const int wh = (WIRE_Y1 - WIRE_TOP) - 2 * pad;
    const double sx = ww / spanX;
    const double sy = wh / spanY;
    p.scale = sx < sy ? sx : sy;
    p.rcx = (rminx + rmaxx) / 2.0;
    p.rcy = (rminy + rmaxy) / 2.0;
    p.ox = MAP_X + MAP_W / 2;
    p.oy = WIRE_TOP + (WIRE_Y1 - WIRE_TOP) / 2;
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

static void boatTri(int x, int y, double screenDeg,
                    int& x1, int& y1, int& x2, int& y2, int& x3, int& y3)
{
    const double a = screenDeg * M_PI / 180.0;
    const int s = 7;
    const double dx = sin(a), dy = -cos(a);
    const double px = cos(a), py = sin(a);
    x1 = (int)(x + dx * s); y1 = (int)(y + dy * s);
    x2 = (int)(x - dx * s * 0.7 + px * s * 0.6); y2 = (int)(y - dy * s * 0.7 + py * s * 0.6);
    x3 = (int)(x - dx * s * 0.7 - px * s * 0.6); y3 = (int)(y - dy * s * 0.7 - py * s * 0.6);
}

static void drawBoatAt(int x, int y, double screenDeg)
{
    int x1, y1, x2, y2, x3, y3;
    boatTri(x, y, screenDeg, x1, y1, x2, y2, x3, y3);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, RBOAT);
}

// Dynamic boat/dash state for dirty updates (no full clears on the fast path).
static int oldBx = -1, oldBy = -1;
static double oldAng = 0.0;
static bool oldWasDot = false;
static bool oldHaveBoat = false;
static int oldDash[4] = {0, 0, 0, 0};
static bool oldHaveDash = false;
static bool dynOk = false;
static double oldRotEff = 0.0;

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
    const int x0 = MAP_X + 5, y0 = WIRE_TOP + 5;
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

// Static course layer: legs, segments, circles, numbers, header wind.
// Identical pixels every time — safe to repaint over dirty regions.
static void drawStaticLayer(const RaceProj& p, double rotEff)
{
    int px = 0, py = 0, qx = 0, qy = 0;
    for (uint8_t i = 0; i + 1 < raceSession.markCount; i++) {
        projToPx(p, raceSession.marks[i].lat, raceSession.marks[i].lon, px, py);
        projToPx(p, raceSession.marks[i + 1].lat, raceSession.marks[i + 1].lon, qx, qy);
        tft.drawLine(px, py, qx, qy, RDIM);
    }
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
    // Header wind (out of frame, top-left): arrow + degrees, bright/white-gray.
    // Mock banner sits left of it when scripted fixes drive the screen.
    const double windScreen = raceSession.windDir - rotEff;
    if (gpsMockActive()) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_YELLOW, RBG);
        tft.drawString("MOCK", 8, 6, 2);
    } else {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(RBG, RBG);
        tft.drawString("MOCK", 8, 6, 2);
    }
    drawWindArrow(64, 14, windScreen);
    char wdeg[8];
    snprintf(wdeg, sizeof(wdeg), "%d", raceSession.windDir);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(RDIM, RBG);
    tft.drawString(wdeg, 78, 6, 2);
}

static void drawRaceMap(TinyGPSPlus& gps, bool full)
{
    if (!raceSession.valid) {
        if (full) {
            tft.fillRect(MAP_X, WIRE_TOP, MAP_W, WIRE_Y1 - WIRE_TOP, RBG);
            tft.drawRect(MAP_X, MAP_Y, MAP_W, STRIP_Y1 - MAP_Y, RDIM);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(RDIM, RBG);
            tft.drawString("NO COURSE", MAP_X + MAP_W / 2, WIRE_TOP + (WIRE_Y1 - WIRE_TOP) / 2, 4);
            tft.drawString("assign a session", MAP_X + MAP_W / 2, WIRE_TOP + (WIRE_Y1 - WIRE_TOP) / 2 + 28, 2);
        }
        dynOk = false;
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

    // Rotating views can't use dirty updates (the whole frame turns):
    // throttle them to 1Hz full redraws instead.
    const bool rotating = fabs(rotEff - oldRotEff) > 0.1;
    if (!full && rotating) {
        if (millis() - lastMapDraw < 1000) return;
        full = true;
    }
    // Fix gain/loss changes the in-frame NO FIX overlay — full path redraws it.
    static bool lastFixOk = false;
    const bool fixOk = gps.location.isValid();
    if (fixOk != lastFixOk) {
        full = true;
        lastFixOk = fixOk;
    }
    if (full) {
        tft.fillRect(MAP_X, WIRE_TOP, MAP_W, WIRE_Y1 - WIRE_TOP, RBG);
        tft.drawRect(MAP_X, MAP_Y, MAP_W, STRIP_Y1 - MAP_Y, RDIM);
        drawStaticLayer(p, rotEff);
        oldRotEff = rotEff;
        lastMapDraw = millis();
        dynOk = false;
        // NO FIX overlay, top-right inside the frame (full path only, so it
        // never smears the wireframe underneath).
        if (!fixOk) {
            tft.setTextDatum(TR_DATUM);
            tft.setTextColor(TFT_YELLOW, RBG);
            tft.drawString("NO FIX", MAP_X + MAP_W - 8, MAP_Y + 6, 2);
        }
    }

    // Boat: triangle on the chart, or a projected dot on the frame edge.
    // Dashed yellow line from the boat (or edge dot) to the destination.
    // Dirty path: erase old dash + old boat, repaint identical static pixels,
    // draw the new dash + boat. Skipped entirely when nothing moved.
    int px = 0, py = 0;
    int bx = -1, by = -1, dx = -1, dy = -1;
    bool haveBoat = false, haveDash = false, isDot = false;
    double ang = 0.0;
    if (gps.location.isValid()) {
        projToPx(p, gps.location.lat(), gps.location.lng(), px, py);
        const int cx = MAP_X + MAP_W / 2, cy = WIRE_TOP + (WIRE_Y1 - WIRE_TOP) / 2;
        bx = px; by = py;
        isDot = clampToWire(cx, cy, bx, by);
        haveBoat = true;
        double dLat = 0.0, dLon = 0.0;
        char tag[8] = {0};
        if (nextDestination(gps, dLat, dLon, tag, sizeof(tag))) {
            projToPx(p, dLat, dLon, dx, dy);
            haveDash = true;
        }
        ang = gps.course.isValid() ? (gps.course.deg() - rotEff) : 0.0;
    }
    if (!dynOk) {
        // Fresh draw, nothing to erase.
    } else if (haveBoat == oldHaveBoat && (!haveBoat ||
               (bx == oldBx && by == oldBy && ang == oldAng && isDot == oldWasDot &&
                haveDash == oldHaveDash && (!haveDash ||
                 (dx == oldDash[0] && dy == oldDash[1]))))) {
        return; // nothing moved
    } else {
        // Erase old dash + old boat, then restore identical static pixels.
        if (oldHaveDash) {
            drawDashed(oldBx, oldBy, oldDash[0], oldDash[1], RBG);
        }
        if (oldHaveBoat) {
            if (oldWasDot) {
                tft.drawCircle(oldBx, oldBy, 4, RBG);
            } else {
                int x1, y1, x2, y2, x3, y3;
                boatTri(oldBx, oldBy, oldAng, x1, y1, x2, y2, x3, y3);
                tft.fillTriangle(x1, y1, x2, y2, x3, y3, RBG);
            }
        }
        drawStaticLayer(p, rotEff);
        tft.drawRect(MAP_X, MAP_Y, MAP_W, STRIP_Y1 - MAP_Y, RDIM);
    }
    if (haveDash) {
        drawDashed(bx, by, dx, dy, RBOAT);
    }
    if (haveBoat) {
        if (isDot) {
            tft.drawCircle(bx, by, 4, RBOAT);
        } else {
            drawBoatAt(bx, by, ang);
        }
    }
    oldBx = bx; oldBy = by; oldAng = ang; oldWasDot = isDot;
    oldHaveBoat = haveBoat; oldHaveDash = haveDash;
    oldDash[0] = dx; oldDash[1] = dy; oldDash[2] = 0; oldDash[3] = 0;
    dynOk = true;
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
static char lastMode[8] = {0};
static int16_t lastModeW = 0;

static char lastBbuf[8] = {0};
static int16_t lastBbufW = 0;
static char lastDbuf[14] = {0};
static int16_t lastDbufW = 0;
static char lastMsg[24] = {0};
static int16_t lastMsgW = 0;

static char lastSpd[10] = {0};
static int16_t lastSpdW = 0;

static void resetRaceText()
{
    // Called on full clears so change-detect redraws everything next pass.
    lastSpd[0] = 0; lastSpdW = 0;
    lastCd[0] = 0; lastCdW = 0;
    lastTag[0] = 0; lastTagW = 0;
    lastPrac[0] = 0; lastPracW = 0;
    lastBbuf[0] = 0; lastBbufW = 0;
    lastDbuf[0] = 0; lastDbufW = 0;
    lastMsg[0] = 0; lastMsgW = 0;
    lastMode[0] = 0; lastModeW = 0;
}

static void drawRaceText(TinyGPSPlus& gps)
{
    // Instant speed, big white, top-left frame corner (dedicated solid zone).
    char spd[10];
    if (gps.speed.isValid()) {
        snprintf(spd, sizeof(spd), "%4.1fkn", gps.speed.knots());
    } else {
        snprintf(spd, sizeof(spd), "  ---  ");
    }
    drawSmart(8, 31, 4, TL_DATUM, RFG, spd, lastSpd, sizeof(lastSpd), lastSpdW);

    // Header: countdown center, next-passage tag right (bright white).
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
    drawSmart(160, 2, 4, TC_DATUM, RFG, cd, lastCd, sizeof(lastCd), lastCdW);

    // Next-passage tag (ST pre-start, else number+side), bright white.
    char tag[8];
    double dLat = 0.0, dLon = 0.0;
    const bool haveDest = nextDestination(gps, dLat, dLon, tag, sizeof(tag));
    drawSmart(312, 2, 4, TR_DATUM, RFG,
              !raceSession.valid ? "----" : (haveDest ? tag : "----"),
              lastTag, sizeof(lastTag), lastTagW);

    // Bottom zone: PRAC/RACE tag left, next data center, view mode right.
    // Layout flips between centered messages and the split bearing view —
    // clear the band once on transition, then draw everything change-detect.
    static int lastStripMode = -1;
    static bool lastShowBrg = false;
    const int stripMode = !raceSession.valid ? 0 : 1;
    if (stripMode != lastStripMode) {
        tft.fillRect(8, STRIP_Y0, MAP_W - 16, STRIP_Y1 - STRIP_Y0, RBG);
        lastMsg[0] = 0; lastMsgW = 0;
        lastBbuf[0] = 0; lastBbufW = 0;
        lastDbuf[0] = 0; lastDbufW = 0;
        lastStripMode = stripMode;
    }
    const bool isRace = strcmp(raceSession.mode, "race") == 0;
    drawSmart(8, 184, 2, TL_DATUM, RDIM,
              raceSession.valid ? (isRace ? "RACE" : "PRAC") : "----",
              lastPrac, sizeof(lastPrac), lastPracW);

    char bbuf[8], dbuf[14];
    bool showBrg = false;
    if (!raceSession.valid) {
        drawSmart(170, 182, 4, TC_DATUM, RFG, "NO COURSE       ", lastMsg, sizeof(lastMsg), lastMsgW);
    } else if (haveDest && gps.location.isValid()) {
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
        drawSmart(158, 182, 4, TR_DATUM, RFG, bbuf, lastBbuf, sizeof(lastBbuf), lastBbufW);
        drawSmart(178, 182, 4, TL_DATUM, RFG, dbuf, lastDbuf, sizeof(lastDbuf), lastDbufW);
        showBrg = true;
    } else {
        drawSmart(170, 182, 4, TC_DATUM, RFG, " ---           ", lastMsg, sizeof(lastMsg), lastMsgW);
    }
    if (showBrg != lastShowBrg) {
        tft.drawCircle(168, 188, 2, showBrg ? RFG : RBG);
        lastShowBrg = showBrg;
    }

    const char* viewTxt = gEffMode == 1 ? "BRG " : (gEffMode == 2 ? "FIT " : "N-UP");
    drawSmart(312, 184, 2, TR_DATUM, RDIM, viewTxt, lastMode, sizeof(lastMode), lastModeW);

    // Hint bar.
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("L Next", 8, 235, 2);
    tft.setTextDatum(BR_DATUM);
    tft.drawString("R View RR Sync", tft.width() - 8, 235, 2);
}

void drawScreenRace(TinyGPSPlus &gps, bool requiresInit)
{
    if (requiresInit) {
        tft.fillScreen(RBG);
        lastRaceCourseKey = -2; // force map redraw
        resetRaceText();
    }
    const int key = raceCourseKey();
    const bool courseChanged = (key != lastRaceCourseKey);
    if (courseChanged) {
        lastRaceCourseKey = key;
    }
    drawRaceMap(gps, requiresInit || courseChanged);
    drawRaceText(gps);
}

void screenRaceButton(Button button, ButtonEvent event)
{
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        nextScreen();
        return;
    }
    // Right short: cycle views — north-up → bearing-up → best-fit.
    // Right long: re-poll health now (pull a freshly pushed session
    // without waiting for the 30s tick).
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        viewMode = (viewMode + 1) % 3;
        redrawCurrentPage();
        return;
    }
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        backendPollHealthNow();
        return;
    }
}
