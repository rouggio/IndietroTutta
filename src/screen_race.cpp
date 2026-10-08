#include "screen_race.h"
#include "screens.h"
#include "race_session.h"
#include "race_run.h"
#include "race_templates.h"
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

// Little rounding-side arrow on P/S marks (any type, gates included):
// half-arc on the side the arrow implies (P = west half CCW, S = east half
// CW) with a barb at the tip. Repainted identically with the static layer.
static void drawSideArrow(int cx, int cy, int rr, char side, uint16_t color)
{
    if (side != 'P' && side != 'S') return;
    if (rr < 2) rr = 2;
    const double dir = (side == 'S') ? 1.0 : -1.0;
    int px = cx, py = cy - rr; // bearing 0 (north of buoy)
    for (double d = 15.0; d <= 180.0; d += 15.0) {
        const double a = dir * d * M_PI / 180.0;
        const int nx = (int)(cx + rr * sin(a)), ny = (int)(cy - rr * cos(a));
        tft.drawLine(px, py, nx, ny, color);
        px = nx;
        py = ny;
    }
    // Barb at the tip (south point), along travel: S-arc heads west,
    // P-arc heads east.
    const double hd = (side == 'S' ? 270.0 : 90.0) * M_PI / 180.0;
    const int tx = cx, ty = cy + rr;
    for (double s = 150.0; s <= 210.0; s += 60.0) {
        const double w = hd + s * M_PI / 180.0;
        tft.drawLine(tx, ty, (int)(tx + 4 * sin(w)), (int)(ty - 4 * cos(w)), color);
    }
}

// Current target identity for map highlighting (mirrors nextDestination:
// pre-start = start, racing = marks[progIdx], finished = finish).
// Line targets: lineIdx 0 = start line, 1 = finish line (mark idx unused).
// Gate target: gatePair set, both buoys of gateId light up.
static void raceTarget(uint8_t& idx, bool& isLine, uint8_t& lineIdx,
                       bool& gatePair, char* gateId, size_t gateLen)
{
    idx = 255; // 255 = no mark target (nothing lights up)
    isLine = false;
    lineIdx = 0;
    gatePair = false;
    if (gateId && gateLen) gateId[0] = '\0';
    if (!raceSession.valid) return;
    const long gun = raceGunEpoch();
    const bool preStart = (gun <= 0 || !raceRunStarted());
    if (preStart) {
        if (raceSession.startLine.valid) {
            isLine = true;
            lineIdx = 0; // start line
            return;
        }
        for (uint8_t i = 0; i < raceSession.markCount; i++) {
            if (raceSession.marks[i].type == RaceMarkStart) {
                idx = i;
                return;
            }
        }
        return;
    }
    if (raceRunFinished()) {
        if (raceSession.finishSameAsStart) {
            // Finish is the start line itself: light it up.
            if (raceSession.startLine.valid) {
                isLine = true;
                lineIdx = 0;
            }
            return;
        }
        if (raceSession.finishLine.valid) {
            isLine = true;
            lineIdx = 1; // distinct finish line
            return;
        }
        if (raceSession.markCount) idx = raceSession.markCount - 1;
        return;
    }
    idx = raceProgIdx();
    if (raceSession.markCount == 0) {
        idx = 255;
        return;
    }
    if (idx >= raceSession.markCount) idx = raceSession.markCount - 1;
    const RaceMark& m = raceSession.marks[idx];
    if (m.type == RaceMarkGate && m.gate[0]) {
        gatePair = true;
        if (gateId && gateLen) {
            strncpy(gateId, m.gate, gateLen - 1);
            gateId[gateLen - 1] = '\0';
        }
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

static void drawWindArrow(int cx, int cy, double screenDeg, uint16_t color)
{
    // Points where the wind comes FROM, in screen frame.
    const double a = screenDeg * M_PI / 180.0;
    const int len = 14;
    const int x2 = (int)(cx + sin(a) * len), y2 = (int)(cy - cos(a) * len);
    tft.drawLine(cx, cy, x2, y2, color);
    const double ha = 0.5;
    const int hx1 = (int)(x2 - sin(a - ha) * 5), hy1 = (int)(y2 + cos(a - ha) * 5);
    const int hx2 = (int)(x2 - sin(a + ha) * 5), hy2 = (int)(y2 + cos(a + ha) * 5);
    tft.drawLine(x2, y2, hx1, hy1, color);
    tft.drawLine(x2, y2, hx2, hy2, color);
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
// point, else the current target); racing it's marks[progIdx] (gates resolve
// to the pair center); finished it's the finish ("FIN"). Fills tag.
static bool nextDestination(TinyGPSPlus& gps, double& lat, double& lon, char* tag, size_t tagLen)
{
    (void)gps;
    if (!raceSession.valid) return false;
    const long gun = raceGunEpoch();
    const bool preStart = (gun <= 0 || !raceRunStarted());
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
    if (raceRunFinished()) {
        const RaceSeg* fin = nullptr;
        if (raceSession.finishSameAsStart && raceSession.startLine.valid) {
            lat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
            lon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
        } else if (raceSession.finishLine.valid) {
            fin = &raceSession.finishLine;
            lat = (fin->latA + fin->latB) / 2.0;
            lon = (fin->lonA + fin->lonB) / 2.0;
        } else {
            const RaceMark& last = raceSession.marks[raceSession.markCount - 1];
            lat = last.lat;
            lon = last.lon;
        }
        snprintf(tag, tagLen, "FIN ");
        return true;
    }
    uint8_t idx = raceProgIdx();
    if (idx >= raceSession.markCount) idx = raceSession.markCount - 1;
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
    uint8_t tIdx = 255;
    bool tLine = false;
    uint8_t tLineIdx = 0;
    bool tGate = false;
    char tGateId[8] = {0};
    raceTarget(tIdx, tLine, tLineIdx, tGate, tGateId, sizeof(tGateId));
    if (raceSession.startLine.valid) {
        const RaceSeg& s = raceSession.startLine;
        projToPx(p, s.latA, s.lonA, px, py);
        projToPx(p, s.latB, s.lonB, qx, qy);
        const bool hl = tLine && tLineIdx == 0;
        tft.drawWideLine(px, py, qx, qy, 3, hl ? RBOAT : TFT_GREEN);
    }
    if (raceSession.finishLine.valid && !raceSession.finishSameAsStart) {
        const RaceSeg& s = raceSession.finishLine;
        projToPx(p, s.latA, s.lonA, px, py);
        projToPx(p, s.latB, s.lonB, qx, qy);
        const bool hl = tLine && tLineIdx == 1;
        tft.drawWideLine(px, py, qx, qy, 2, hl ? RBOAT : TFT_GREEN);
    }
    char num[12];
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        const RaceMark& m = raceSession.marks[i];
        projToPx(p, m.lat, m.lon, px, py);
        int rPx = (int)(m.r / DEG_M * p.scale);
        if (rPx < 3) rPx = 3;
        if (rPx > 40) rPx = 40;
        // Target object lights up yellow; everything else is white, lines
        // carry the green. Gate pairs light up together.
        bool isTgt = false;
        if (tGate && m.type == RaceMarkGate && m.gate[0] && strcmp(m.gate, tGateId) == 0) {
            isTgt = true;
        } else if (!tGate && !tLine && i == tIdx) {
            isTgt = true;
        }
        const uint16_t col = isTgt ? RBOAT : RFG;
        tft.drawCircle(px, py, rPx, col);
        drawSideArrow(px, py, (int)(rPx * 0.55), m.side, col);
        // Labels sit on the mark itself; coincident marks (piles) share one
        // concatenated label ("1-4"), drawn once by the first of the group.
        bool firstOfPile = true;
        for (uint8_t k = 0; k < i; k++) {
            if (raceSession.marks[k].lat == m.lat && raceSession.marks[k].lon == m.lon) {
                firstOfPile = false;
                break;
            }
        }
        if (!firstOfPile) continue;
        num[0] = '\0';
        bool first = true;
        for (uint8_t j = i; j < raceSession.markCount; j++) {
            if (raceSession.marks[j].lat != m.lat || raceSession.marks[j].lon != m.lon) continue;
            char tmp[5];
            snprintf(tmp, sizeof(tmp), "%s%d", first ? "" : "-", j + 1);
            strncat(num, tmp, sizeof(num) - strlen(num) - 1);
            first = false;
        }
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(i == 0 ? RFG : RDIM, RBG);
        tft.drawString(num, px, py, 2);
    }
    // Header wind (out of frame, top-left): arrow only. The arrow rotates
    // with the view, so the old one is erased first (header is never
    // full-cleared). The numeric degrees were dropped — the arrow is the UI.
    // Mock banner sits left of it when scripted fixes drive the screen.
    static double oldWindScreen = 0.0;
    static bool oldWindHave = false;
    const double windScreen = raceSession.windDir - rotEff;
    if (oldWindHave) drawWindArrow(64, 14, oldWindScreen, RBG);
    tft.fillRect(74, 4, 48, 18, RBG); // retired degrees slot, kept clean
    drawWindArrow(64, 14, windScreen, RFG);
    oldWindScreen = windScreen;
    oldWindHave = true;
    if (gpsMockActive()) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_YELLOW, RBG);
        tft.drawString("MOCK", 8, 6, 2);
    } else {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(RBG, RBG);
        tft.drawString("MOCK", 8, 6, 2);
    }
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
static char lastPrac[12] = {0};
static int16_t lastPracW = 0;

static char lastBbuf[8] = {0};
static int16_t lastBbufW = 0;
static char lastDbuf[14] = {0};
static int16_t lastDbufW = 0;
static char lastMsg[24] = {0};
static int16_t lastMsgW = 0;

static char lastSpd[10] = {0};
static int16_t lastSpdW = 0;

// Last GPS epoch seen on this page (buttons have no gps handle).
static long lastGpsNow = 0;
// Last fix for template placement (Repeat/Start need position + freshness).
static double lastGpsLat = 0.0, lastGpsLon = 0.0;
static unsigned long lastFixAt = 0;
// Transient header message (duration cycling), 2s.
static char transientMsg[12] = {0};
static unsigned long transientUntil = 0;
static void showTransient(const char* msg)
{
    strncpy(transientMsg, msg, sizeof(transientMsg) - 1);
    transientMsg[sizeof(transientMsg) - 1] = '\0';
    transientUntil = millis() + 2000;
}
// Committee-signal banner, 8s (outranks the transient in the header).
static char sigMsg[12] = {0};
static unsigned long sigUntil = 0;
static void showSignal(const char* msg)
{
    strncpy(sigMsg, msg, sizeof(sigMsg) - 1);
    sigMsg[sizeof(sigMsg) - 1] = '\0';
    sigUntil = millis() + 8000;
}

// Step 7: apply newly arrived committee signals (UI thread). Idempotent by
// signal id (cursor in race_run, NVS-backed per session).
static void raceSignalsApply(TinyGPSPlus& gps)
{
    if (!raceSession.valid || raceSession.sigCount == 0) return;
    long now = raceWallEpoch();
    if (now <= 0) now = raceGpsEpoch(gps);
    if (now <= 0) now = lastGpsNow;
    if (now <= 0) return;
    for (uint8_t i = 0; i < raceSession.sigCount; i++) {
        const RaceSignal& s = raceSession.signals[i];
        if (!raceSignalIsNew(s.id)) continue;
        raceApplySignal(s.kind, s.detail, now);
        raceLogEvent("SIG", now, s.kind);
        raceSignalMark(s.id);
        char msg[12];
        if (strcmp(s.kind, "SCP") == 0) {
            snprintf(msg, sizeof(msg), "SCP +%.5s", s.detail);
        } else if (strcmp(s.kind, "RECALL") == 0) {
            snprintf(msg, sizeof(msg), "RECALL ");
        } else if (strcmp(s.kind, "ABANDON") == 0) {
            snprintf(msg, sizeof(msg), "ABANDON ");
        } else {
            snprintf(msg, sizeof(msg), "%-7.7s! ", s.kind);
        }
        showSignal(msg);
    }
}

// LL menu state (actions defined near drawScreenRace below).
static bool menuOpen = false;
static uint8_t menuSel = 0;
static uint8_t menuN = 0;
static bool menuDirty = false;
static unsigned long menuUntil = 0;
// Template browse state (instant practice setup; map frozen while open).
static bool tplOpen = false;
static uint8_t tplSel = 0;
static bool tplDirty = false;
static bool tplAsked = false;
static bool tplWasReady = false;
static unsigned long tplT0 = 0;

static void resetRaceText()
{
    // Called on full clears so change-detect redraws everything next pass.
    lastSpd[0] = 0; lastSpdW = 0;
    lastCd[0] = 0; lastCdW = 0;
    lastPrac[0] = 0; lastPracW = 0;
    lastBbuf[0] = 0; lastBbufW = 0;
    lastDbuf[0] = 0; lastDbufW = 0;
    lastMsg[0] = 0; lastMsgW = 0;
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
    drawSmart(6, 33, 4, TL_DATUM, RFG, spd, lastSpd, sizeof(lastSpd), lastSpdW);

    // Header: countdown center, next-passage tag right (bright white).
    // Header: countdown pre-gun, GO/OCS while pending, elapsed once started.
    char cd[12];
    // Wall clock (GPS-calibrated, ticks through fix gaps) so the countdown
    // never freezes when fixes pause; raw GPS epoch as the cold fallback.
    long now = raceWallEpoch();
    if (now <= 0) now = raceGpsEpoch(gps);
    lastGpsNow = now;
    if (gps.location.isValid()) {
        lastGpsLat = gps.location.lat();
        lastGpsLon = gps.location.lng();
        lastFixAt = millis();
    }
    const long gun = raceGunEpoch();
    if (millis() < sigUntil && sigMsg[0]) {
        snprintf(cd, sizeof(cd), "%-8.8s", sigMsg);
    } else if (millis() < transientUntil && transientMsg[0]) {
        snprintf(cd, sizeof(cd), "%-8.8s", transientMsg);
    } else if (raceSession.valid && gun > 0 && now > 0) {        if (now < gun) {
            const long rem = gun - now;
            snprintf(cd, sizeof(cd), " %2ld:%02ld  ", rem / 60, rem % 60);
        } else if (!raceRunStarted()) {
            snprintf(cd, sizeof(cd), raceRunOcs() ? "OCS     " : "   GO   ");
        } else {
            const long base = raceRunFinished() && raceRunEndEpoch() > 0 ? raceRunEndEpoch() : now;
            const long el = base - raceRunStartEpoch();
            snprintf(cd, sizeof(cd), "+%2ld:%02ld  ", el / 60, el % 60);
        }
    } else if (raceSession.valid && gun > 0) {
        // Gun armed but no time source yet (waiting on the first fix).
        snprintf(cd, sizeof(cd), "WAIT    ");
    } else if (raceSession.valid && now > 0 &&
               strcmp(raceSession.mode, "race") != 0) {
        // Practice with no gun yet: show what R cycles (LL arms it).
        const long d = racePracticeDur();
        snprintf(cd, sizeof(cd), "DUR %1ld:%02ld", d / 60, d % 60);
    } else {
        snprintf(cd, sizeof(cd), " --:--   ");
    }
    drawSmart(160, 2, 4, TC_DATUM, RFG, cd, lastCd, sizeof(lastCd), lastCdW);

    // Next destination feeds the strip + boat/dash (the header tag is gone —
    // the target lights up yellow on the map instead).
    char tag[8];
    double dLat = 0.0, dLon = 0.0;
    const bool haveDest = nextDestination(gps, dLat, dLon, tag, sizeof(tag));
    (void)tag;

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
    // Left cell: mode only (the N-UP/BRG/FIT view label is gone — the map
    // rotation speaks for itself).
    drawSmart(8, 184, 2, TL_DATUM, RDIM,
              raceSession.valid ? (isRace ? "RACE" : "PRAC") : "----",
              lastPrac, sizeof(lastPrac), lastPracW);

    char bbuf[8], dbuf[14];
    bool showBrg = false;
    if (!raceSession.valid) {
        drawSmart(190, 182, 4, TC_DATUM, RFG, "NO COURSE", lastMsg, sizeof(lastMsg), lastMsgW);
    } else if (haveDest && gps.location.isValid()) {
        const double dist = TinyGPSPlus::distanceBetween(
            gps.location.lat(), gps.location.lng(), dLat, dLon);
        double brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), dLat, dLon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        const long d = (long)dist > 9999 ? 9999 : (long)dist;
        snprintf(bbuf, sizeof(bbuf), "%3d", (int)brg);
        snprintf(dbuf, sizeof(dbuf), "%4ldm", d);
        drawSmart(216, 182, 4, TR_DATUM, RFG, bbuf, lastBbuf, sizeof(lastBbuf), lastBbufW);
        drawSmart(312, 182, 4, TR_DATUM, RFG, dbuf, lastDbuf, sizeof(lastDbuf), lastDbufW);
        showBrg = true;
    } else {
        drawSmart(190, 182, 4, TC_DATUM, RFG, " --- ", lastMsg, sizeof(lastMsg), lastMsgW);
    }
    if (!showBrg && lastShowBrg) {
        drawSmart(216, 182, 4, TR_DATUM, RFG, "", lastBbuf, sizeof(lastBbuf), lastBbufW);
        drawSmart(312, 182, 4, TR_DATUM, RFG, "", lastDbuf, sizeof(lastDbuf), lastDbufW);
    }
    lastShowBrg = showBrg;
    // Degree ring glued to the bearing cell (idle pixels overdrawn, no smear).
    tft.fillCircle(222, 195, 2, showBrg ? RFG : RBG);

    // Hint bar (mode-dependent, fixed widths so re-modes overwrite cleanly).
    // Browse and menu repurpose the hints as their controls.
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(BL_DATUM);
    if (tplOpen) tft.drawString("L Back        ", 8, 239, 2);
    else tft.drawString(menuOpen ? "L Next LL OK  " : "L Next LL Menu", 8, 239, 2);
    tft.setTextDatum(BR_DATUM);
    if (tplOpen) tft.drawString("R Next RR Go  ", tft.width() - 8, 239, 2);
    else tft.drawString(menuOpen ? "R BackRR Back" : (isRace ? "R --- RR View " : "R Dur RR View "), tft.width() - 8, 239, 2);
}

// LL menu: explicit race actions (map frozen while open, box repainted on
// open/selection only; close does a full repaint). Practice: start /
// repeat / abandon. Race: resync / abandon (the gun belongs to committee).
static uint8_t menuCount(bool isPractice)
{
    return isPractice ? 3 : 2;
}

static void menuText(bool isPractice, uint8_t i, char* buf, size_t n)
{
    if (isPractice) {
        if (i == 0) snprintf(buf, n, "Start PRAC");
        else if (i == 1) snprintf(buf, n, "Repeat last");
        else snprintf(buf, n, "Abandon");
    } else {
        if (i == 0) snprintf(buf, n, "Resync now");
        else snprintf(buf, n, "Abandon");
    }
}

static void drawRaceMenu()
{
    const bool isPractice = !(raceSession.valid && strcmp(raceSession.mode, "race") == 0);
    const int bw = 170, bh = 24 + menuN * 22;
    const int bx = (tft.width() - bw) / 2, by = 80;
    tft.fillRect(bx, by, bw, bh, RBG);
    tft.drawRect(bx, by, bw, bh, RFG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(RDIM, RBG);
    tft.drawString(isPractice ? "PRAC MENU" : "RACE MENU", bx + 8, by + 5, 2);
    char buf[16];
    for (uint8_t i = 0; i < menuN; i++) {
        menuText(isPractice, i, buf, sizeof(buf));
        const int ry = by + 24 + i * 22;
        if (i == menuSel) {
            tft.fillRect(bx + 4, ry - 2, bw - 8, 20, RFG);
            tft.setTextColor(RBG, RFG);
        } else {
            tft.setTextColor(RFG, RBG);
        }
        tft.drawString(buf, bx + 12, ry, 2);
    }
    tft.setTextColor(RDIM, RBG);
}

static void menuClose()
{
    menuOpen = false;
    redrawCurrentPage();
}

static void tplClose()
{
    tplOpen = false;
    redrawCurrentPage();
}

// Template browse list (below header; map frozen behind it). Repainted on
// open/selection/fetch-ready only.
static void drawTplList()
{
    const int bx = 8, bw = 304, by = 34, bh = 172;
    tft.fillRect(bx, by, bw, bh, RBG);
    tft.drawRect(bx, by, bw, bh, RFG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(RDIM, RBG);
    if (!tplReady()) {
        tft.drawString("TEMPLATES ...", bx + 8, by + 5, 2);
        tft.setTextColor(RFG, RBG);
        tft.drawString("asking backend", bx + 8, by + 27, 2);
        return;
    }
    const uint8_t n = tplCount();
    char head[20];
    snprintf(head, sizeof(head), "TEMPLATES %d", n);
    tft.drawString(head, bx + 8, by + 5, 2);
    if (!n) {
        tft.setTextColor(RFG, RBG);
        tft.drawString("none saved", bx + 8, by + 27, 2);
        return;
    }
    if (tplSel >= n) tplSel = 0;
    uint8_t top = tplSel > 2 ? tplSel - 2 : 0;
    if (top + 6 > n && n > 6) top = n - 6;
    for (uint8_t r = 0; r < 6 && top + r < n; r++) {
        const Tpl* t = tplGet(top + r);
        if (!t) continue;
        const int ry = by + 26 + r * 22;
        if (top + r == tplSel) {
            tft.fillRect(bx + 4, ry - 2, bw - 8, 20, RFG);
            tft.setTextColor(RBG, RFG);
        } else {
            tft.setTextColor(RFG, RBG);
        }
        char nm[36];
        snprintf(nm, sizeof(nm), "%d %s", top + r + 1, t->name);
        tft.drawString(nm, bx + 12, ry, 2);
    }
    tft.setTextColor(RDIM, RBG);
}

static void menuConfirm()
{
    const bool isPractice = !(raceSession.valid && strcmp(raceSession.mode, "race") == 0);
    if (isPractice) {
        if (menuSel == 0) {
            // Start practice → browse templates (pick fires the +10s gun).
            menuOpen = false;
            tplOpen = true;
            tplSel = 0;
            tplDirty = true;
            tplAsked = false;
            tplWasReady = false;
            redrawCurrentPage();
        } else if (menuSel == 1) {
            // Repeat last → same course re-anchored at the boat, fresh gun.
            long now = raceWallEpoch();
            if (now <= 0) now = lastGpsNow;
            if (!raceSession.valid || raceSession.markCount == 0) {
                showTransient("NO COURSE ");
            } else if (now <= 0 || millis() - lastFixAt > 15000) {
                showTransient("NO FIX ");
            } else if (tplRepeatSession(lastGpsLat, lastGpsLon, now)) {
                menuClose();
            } else {
                showTransient("NO FIX ");
            }
        } else {
            raceRunReset();
            showSignal("ABANDON ");
        }
    } else {
        if (menuSel == 0) {
            backendPollHealthNow();
        } else {
            raceRunReset();
            showSignal("ABANDON ");
        }
    }
    menuClose();
}

void drawScreenRace(TinyGPSPlus &gps, bool requiresInit)
{
    if (requiresInit) {
        tft.fillScreen(RBG);
        lastRaceCourseKey = -2; // force map redraw
        resetRaceText();
        raceRunReset();
    }
    const int key = raceCourseKey();
    const bool courseChanged = (key != lastRaceCourseKey);
    if (courseChanged) {
        lastRaceCourseKey = key;
        raceRunReset();
    }
    raceRunUpdate(gps);
    raceSignalsApply(gps);
    // Wrong-side calls surface as a short header banner.
    if (raceWrongPoll()) {
        showTransient("WRONG!  ");
    }
    if (menuOpen && (long)(millis() - menuUntil) >= 0) {
        menuOpen = false; // timed out: full repaint drops the box
        redrawCurrentPage();
        return;
    }
    if (courseChanged && (menuOpen || tplOpen)) {
        menuOpen = false;
        tplOpen = false;
    }
    if (tplOpen) {
        // Template browse: fetch once, fail loud after 8s, freeze the map.
        if (!tplAsked) {
            tplAsked = true;
            tplT0 = millis();
            backendFetchTemplates();
        }
        if (tplReady() != tplWasReady) {
            tplWasReady = tplReady();
            tplDirty = true;
        }
        if (!tplReady() && millis() - tplT0 > 8000) {
            tplOpen = false;
            showTransient("OFFLINE ");
            redrawCurrentPage();
        }
    }
    if (!menuOpen && !tplOpen) drawRaceMap(gps, requiresInit || courseChanged);
    drawRaceText(gps);
    if (menuOpen) {
        if (menuDirty || requiresInit || courseChanged) {
            drawRaceMenu();
            menuDirty = false;
        }
    }
    if (tplOpen && (tplDirty || requiresInit || courseChanged)) {
        drawTplList();
        tplDirty = false;
    }
}

// Transient duration banner for LL cycling ("SET 3:00", 2s).
static unsigned long durMsgUntil = 0;

void screenRaceButton(Button button, ButtonEvent event)
{
    // Template browse owns every button while open: R steps, RR picks and
    // fires the +10s gun on the spot, L backs out.
    if (tplOpen) {
        if (button == Button::Left) {
            tplClose();
            return;
        }
        if (button == Button::Right && event == ButtonEvent::ShortPress) {
            const uint8_t n = tplCount();
            if (n) {
                tplSel = (uint8_t)((tplSel + 1) % n);
                tplDirty = true;
            }
            return;
        }
        if (button == Button::Right && event == ButtonEvent::LongPress) {
            long now = raceWallEpoch();
            if (now <= 0) now = lastGpsNow;
            if (now > 0 && millis() - lastFixAt <= 15000 &&
                tplStartSession(tplSel, lastGpsLat, lastGpsLon, now)) {
                tplClose(); // courseVersion bump redraws the new course
            } else {
                showTransient("NO FIX ");
            }
            return;
        }
        return;
    }
    // Menu owns every button while open: L cycles, LL confirms, R backs out.
    if (menuOpen) {
        if (button == Button::Left && event == ButtonEvent::ShortPress) {
            menuSel = (uint8_t)((menuSel + 1) % (menuN ? menuN : 1));
            menuDirty = true;
            menuUntil = millis() + 10000;
            return;
        }
        if (button == Button::Left && event == ButtonEvent::LongPress) {
            menuConfirm();
            return;
        }
        menuClose();
        return;
    }
    if (button == Button::Left && event == ButtonEvent::ShortPress) {
        nextScreen();
        return;
    }
    const bool isPractice = !(raceSession.valid && strcmp(raceSession.mode, "race") == 0);
    // Left long opens the menu (explicit actions beat hidden gestures).
    if (button == Button::Left && event == ButtonEvent::LongPress) {
        menuOpen = true;
        menuSel = 0;
        menuN = menuCount(isPractice);
        menuDirty = true;
        menuUntil = millis() + 10000;
        return;
    }
    // Right short: practice durations 1 → 3 → 5 min (pre-start only).
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        if (isPractice && !raceRunStarted()) {
            racePracticeCycleDur();
            const long d = racePracticeDur();
            char msg[12];
            snprintf(msg, sizeof(msg), "DUR %1ld:%02ld", d / 60, d % 60);
            showTransient(msg);
        }
        return;
    }
    // Right long: cycle views — north-up → bearing-up → best-fit.
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        viewMode = (viewMode + 1) % 3;
        redrawCurrentPage();
        return;
    }
}
