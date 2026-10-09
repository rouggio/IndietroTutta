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

#include "canvas.h"

static const uint16_t RBG = TFT_BLACK;
static const uint16_t RFG = TFT_WHITE;
static const uint16_t RDIM = 0x8410; // neutral gray
static const uint16_t RBOAT = TFT_YELLOW;
static const uint16_t MENU_GRAY = 0x7BEF; // CONFIG gray: menu reference color

// Frame: full-bleed N/W/E border, south edge +4. Left viewport holds the
// map (below the header); right viewport (1/5 width) holds the values.
static const int HDR_H = 28;
static const int MAP_X = 0;
static const int MAP_Y = 30;
static const int MAP_W = 256;   // left viewport (4/5 of 320)
static const int WIRE_TOP = 30; // wire fills from below the header...
static const int WIRE_Y1 = 208; // ...to the frame south (strip is gone)
static const int FRAME_SOUTH = 213;
// Right values pane: 1/5 width, six mini-labeled rows filling the height.
static const int PANE_X = 256;
static const int PANE_W = 64;
static const int PANE_Y0 = 30;
static const int PANE_ROWH = 29;

static void drawFrameChrome()
{
    tft.drawRect(0, 0, 320, FRAME_SOUTH, RDIM);
    tft.drawLine(255, 30, 255, FRAME_SOUTH - 1, RDIM); // viewport divider
    tft.drawLine(255, 30, 319, 30, RDIM);              // pane top edge
}

static const double DEG_M = 111320.0;

// View state (RAM only). RR cycles: 0 north-up, 1 bearing-up (next
// destination up), 2 best-fit (0°/90° whichever fills the screen).
static uint8_t viewMode = 0;

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
    // Header wind readout moved to the right pane (WND row) — the arrow
    // lives there as degrees now. Mock banner stays top-left.
    // Mock state moved to the main screen (yellow FX tile there); keep this
    // header slot painted clean (header is never full-cleared otherwise).
    tft.fillRect(8, 4, 44, 18, RBG);
}

static void drawRaceMap(TinyGPSPlus& gps, bool full)
{
    if (!raceSession.valid) {
        if (full) {
            tft.fillRect(MAP_X, WIRE_TOP, MAP_W, WIRE_Y1 - WIRE_TOP, RBG);
            drawFrameChrome();
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
    double destLat = 0.0, destLon = 0.0;
    char destTag[8] = {0};
    const bool haveDestHere = nextDestination(gps, destLat, destLon, destTag, sizeof(destTag));
    if (viewMode == 1 && gps.location.isValid() && haveDestHere) {
        double brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), destLat, destLon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        rotEff = brg;
    }

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
        drawFrameChrome();
        drawStaticLayer(p, rotEff);
        oldRotEff = rotEff;
        lastMapDraw = millis();
        dynOk = false;
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
        drawFrameChrome();
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

static char pBan[12] = {0};
static int16_t pBanW = 0;
// Right-pane cells (mini label + value each).
static char pLabS[8] = {0}; static int16_t pLabSW = 0;
static char pLabB[8] = {0}; static int16_t pLabBW = 0;
static char pLabD[8] = {0}; static int16_t pLabDW = 0;
static char pLabN[8] = {0}; static int16_t pLabNW = 0;
static char pLabT[8] = {0}; static int16_t pLabTW = 0;
static char pSpd[10] = {0}; static int16_t pSpdW = 0;
static char pBrg[8] = {0}; static int16_t pBrgW = 0;
static char pDst[10] = {0}; static int16_t pDstW = 0;
static char pNxt[8] = {0}; static int16_t pNxtW = 0;
static char pTim[10] = {0}; static int16_t pTimW = 0;
static char pLabW[8] = {0}; static int16_t pLabWW = 0;
static char pWnd[8] = {0}; static int16_t pWndW = 0;
static bool paneClean = false;
// Hint bar texts (lengths vary by mode: repaint band on change).
static char lastHintL[16] = {0};
static char lastHintR[16] = {0};

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
static bool menuFull = false; // set on open: full clear + title, like CONFIG
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
    pBan[0] = 0; pBanW = 0;
    pLabS[0] = 0; pLabSW = 0;
    pLabB[0] = 0; pLabBW = 0;
    pLabD[0] = 0; pLabDW = 0;
    pLabN[0] = 0; pLabNW = 0;
    pLabT[0] = 0; pLabTW = 0;
    pSpd[0] = 0; pSpdW = 0;
    pBrg[0] = 0; pBrgW = 0;
    pDst[0] = 0; pDstW = 0;
    pNxt[0] = 0; pNxtW = 0;
    pTim[0] = 0; pTimW = 0;
    pLabW[0] = 0; pLabWW = 0;
    pWnd[0] = 0; pWndW = 0;
    lastHintL[0] = 0; lastHintR[0] = 0;
    paneClean = false;
}

static void drawRaceText(TinyGPSPlus& gps)
{
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

    // Header: committee banners only (the countdown lives in the pane now).
    char ban[12];
    if (millis() < sigUntil && sigMsg[0]) {
        snprintf(ban, sizeof(ban), "%-8.8s", sigMsg);
    } else if (millis() < transientUntil && transientMsg[0]) {
        snprintf(ban, sizeof(ban), "%-8.8s", transientMsg);
    } else {
        ban[0] = '\0';
    }
    drawSmart(160, 2, 4, TC_DATUM, RFG, ban, pBan, sizeof(pBan), pBanW);

    // Destination geometry (map boat/dash + pane cells).
    char tag[8];
    double dLat = 0.0, dLon = 0.0;
    const bool haveDest = nextDestination(gps, dLat, dLon, tag, sizeof(tag));
    const bool haveFix = gps.location.isValid();
    double brg = 0.0;
    long distM = 0;
    bool showBrg = false;
    if (raceSession.valid && haveDest && haveFix) {
        const double dist = TinyGPSPlus::distanceBetween(
            gps.location.lat(), gps.location.lng(), dLat, dLon);
        brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), dLat, dLon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        distM = (long)dist > 9999 ? 9999 : (long)dist;
        showBrg = true;
    }
    // Compact time string for the pane (header countdown moved here).
    char tim[10];
    if (raceSession.valid && gun > 0 && now > 0) {
        if (now < gun) {
            const long rem = gun - now;
            snprintf(tim, sizeof(tim), " %2ld:%02ld", rem / 60, rem % 60);
        } else if (!raceRunStarted()) {
            snprintf(tim, sizeof(tim), raceRunOcs() ? "OCS" : "GO");
        } else {
            const long base = raceRunFinished() && raceRunEndEpoch() > 0 ? raceRunEndEpoch() : now;
            const long el = base - raceRunStartEpoch();
            snprintf(tim, sizeof(tim), "+%2ld:%02ld", el / 60, el % 60);
        }
    } else if (raceSession.valid && gun > 0) {
        snprintf(tim, sizeof(tim), "WAIT");
    } else if (raceSession.valid && now > 0 &&
               strcmp(raceSession.mode, "race") != 0) {
        const long d = racePracticeDur();
        snprintf(tim, sizeof(tim), "D%1ld:%02ld", d / 60, d % 60);
    } else {
        snprintf(tim, sizeof(tim), "--:--");
    }

    // Right values pane: background + grid once, cells change-detect.
    // Rows fill the pane top-down with no head gap.
    if (!paneClean) {
        tft.fillRect(PANE_X, PANE_Y0, PANE_W, 178, RBG);
        for (int r = 1; r < 6; r++) {
            const int ly = PANE_Y0 + 1 + r * PANE_ROWH - 4;
            tft.drawLine(PANE_X + 2, ly, PANE_X + PANE_W - 3, ly, RDIM);
        }
        paneClean = true;
    }
    char cell[14];
    const int vy = PANE_Y0 + 1, vv = PANE_Y0 + 11;
    // Row 0: speed.
    drawSmart(PANE_X + 3, vy, gGrabbing ? 2 : 1, TL_DATUM, RDIM, "SPD KN",
              pLabS, sizeof(pLabS), pLabSW);
    if (gps.speed.isValid()) snprintf(cell, sizeof(cell), "%4.1f", gps.speed.knots());
    else snprintf(cell, sizeof(cell), " --- ");
    drawSmart(PANE_X + PANE_W - 6, vv, 2, TR_DATUM, RFG, cell,
              pSpd, sizeof(pSpd), pSpdW);
    // Row 1: bearing to destination.
    drawSmart(PANE_X + 3, vy + PANE_ROWH, gGrabbing ? 2 : 1, TL_DATUM, RDIM, "BRG",
              pLabB, sizeof(pLabB), pLabBW);
    if (showBrg) snprintf(cell, sizeof(cell), "%3d", (int)brg);
    else snprintf(cell, sizeof(cell), "---");
    drawSmart(PANE_X + PANE_W - 6, vv + PANE_ROWH, 2, TR_DATUM, RFG, cell,
              pBrg, sizeof(pBrg), pBrgW);
    // Row 2: distance.
    drawSmart(PANE_X + 3, vy + 2 * PANE_ROWH, gGrabbing ? 2 : 1, TL_DATUM, RDIM, "DST M",
              pLabD, sizeof(pLabD), pLabDW);
    if (showBrg) snprintf(cell, sizeof(cell), "%4ld", distM);
    else snprintf(cell, sizeof(cell), "----");
    drawSmart(PANE_X + PANE_W - 6, vv + 2 * PANE_ROWH, 2, TR_DATUM, RFG, cell,
              pDst, sizeof(pDst), pDstW);
    // Row 3: next destination.
    drawSmart(PANE_X + 3, vy + 3 * PANE_ROWH, gGrabbing ? 2 : 1, TL_DATUM, RDIM, "NEXT",
              pLabN, sizeof(pLabN), pLabNW);
    if (raceSession.valid && haveDest) snprintf(cell, sizeof(cell), "%-4.4s", tag);
    else snprintf(cell, sizeof(cell), "----");
    drawSmart(PANE_X + PANE_W - 6, vv + 3 * PANE_ROWH, 2, TR_DATUM, RFG, cell,
              pNxt, sizeof(pNxt), pNxtW);
    // Row 4: time.
    drawSmart(PANE_X + 3, vy + 4 * PANE_ROWH, gGrabbing ? 2 : 1, TL_DATUM, RDIM, "TIME",
              pLabT, sizeof(pLabT), pLabTW);
    drawSmart(PANE_X + PANE_W - 6, vv + 4 * PANE_ROWH, 2, TR_DATUM, RFG, tim,
              pTim, sizeof(pTim), pTimW);
    // Row 5: wind direction (from the session; header arrow retired here).
    drawSmart(PANE_X + 3, vy + 5 * PANE_ROWH, gGrabbing ? 2 : 1, TL_DATUM, RDIM, "WND",
              pLabW, sizeof(pLabW), pLabWW);
    if (raceSession.valid) snprintf(cell, sizeof(cell), "%3d", raceSession.windDir);
    else snprintf(cell, sizeof(cell), "---");
    drawSmart(PANE_X + PANE_W - 6, vv + 5 * PANE_ROWH, 2, TR_DATUM, RFG, cell,
              pWnd, sizeof(pWnd), pWndW);

    // Hint bar: texts change length across modes now, so repaint on change
    // (band clear once, then both sides).
    char hintL[16], hintR[16];
    snprintf(hintL, sizeof(hintL), "%s", (tplOpen || menuOpen) ? "L Back" : "L Next  LL Menu");
    snprintf(hintR, sizeof(hintR), "%s", (tplOpen || menuOpen) ? "R Sel RR Pick" : "RR View");
    if (strcmp(hintL, lastHintL) != 0 || strcmp(hintR, lastHintR) != 0) {
        tft.fillRect(0, 219, 320, 21, RBG);
        strncpy(lastHintL, hintL, sizeof(lastHintL) - 1);
        strncpy(lastHintR, hintR, sizeof(lastHintR) - 1);
    }
    tft.setTextColor(RDIM, RBG);
    tft.setTextDatum(BL_DATUM);
    tft.drawString(hintL, 8, 239, 2);
    tft.setTextDatum(BR_DATUM);
    tft.drawString(hintR, tft.width() - 8, 239, 2);
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

static void drawRaceMenu(bool full)
{
    // CONFIG is the menu reference: centered title + rule, `>` rows with
    // yellow selection, hint bar with rule. Same geometry, same grays.
    const bool isPractice = !(raceSession.valid && strcmp(raceSession.mode, "race") == 0);
    if (full) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(isPractice ? "PRAC MENU" : "RACE MENU", tft.width() / 2, 20, 4);
        tft.drawFastHLine(0, 44, tft.width(), MENU_GRAY);
    } else {
        tft.fillRect(0, 60, tft.width(), 70, TFT_BLACK);
    }
    char buf[16], row[20];
    for (uint8_t i = 0; i < menuN; i++) {
        menuText(isPractice, i, buf, sizeof(buf));
        snprintf(row, sizeof(row), "%s %s", i == menuSel ? ">" : " ", buf);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(i == menuSel ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
        tft.drawString(row, 12, 68 + i * 30, 2);
    }
    tft.drawFastHLine(0, 214, tft.width(), MENU_GRAY);
    tft.setTextColor(MENU_GRAY, TFT_BLACK);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("L Back", 8, 235, 2);
    tft.setTextDatum(BR_DATUM);
    tft.drawString("R Next  RR Pick", tft.width() - 8, 235, 2);
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
    tft.setTextDatum(TC_DATUM);
    tft.drawString(head, bx + bw / 2, by + 5, 2);
    tft.drawFastHLine(bx + 8, by + 26, bw - 16, RDIM);
    if (!n) {
        tft.setTextColor(RFG, RBG);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("none saved", bx + 8, by + 30, 2);
        return;
    }
    if (tplSel >= n) tplSel = 0;
    uint8_t top = tplSel > 2 ? tplSel - 2 : 0;
    if (top + 6 > n && n > 6) top = n - 6;
    for (uint8_t r = 0; r < 6 && top + r < n; r++) {
        const Tpl* t = tplGet(top + r);
        if (!t) continue;
        const int ry = by + 30 + r * 22;
        const bool sel = (top + r) == tplSel;
        char nm[36];
        snprintf(nm, sizeof(nm), "%s %d %s", sel ? ">" : " ", top + r + 1, t->name);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(sel ? TFT_YELLOW : RFG, RBG);
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
        if (!gGrabbing) raceRunReset(); // capture must not wipe a live run
    }
    const int key = raceCourseKey();
    const bool courseChanged = (key != lastRaceCourseKey);
    if (courseChanged) {
        lastRaceCourseKey = key;
        if (!gGrabbing) raceRunReset();
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
    if (menuOpen) {
        // Full-screen menu: engine already ran above; paint and stop.
        // The open transition always takes the full path (clear + title)
        // so no map ghosts survive behind the rows.
        if (menuFull) {
            drawRaceMenu(true);
            menuFull = false;
            menuDirty = false;
        } else if (menuDirty || requiresInit || courseChanged) {
            drawRaceMenu(requiresInit || courseChanged);
            menuDirty = false;
        }
        return;
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
    if (!tplOpen) drawRaceMap(gps, requiresInit || courseChanged);
    drawRaceText(gps);
    if (tplOpen && (tplDirty || requiresInit || courseChanged)) {
        drawTplList();
        tplDirty = false;
    }
}

// Transient duration banner for LL cycling ("SET 3:00", 2s).
static unsigned long durMsgUntil = 0;

void screenRaceButton(Button button, ButtonEvent event)
{
    // Template browse owns Short/Long while open (raw Press/Release pass
    // through, same release-after-open reason as the menu).
    if (tplOpen) {
        if (button == Button::Left &&
            (event == ButtonEvent::ShortPress || event == ButtonEvent::LongPress)) {
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
    // Menu owns Short/Long while open (standard mapping: R cycles, RR
    // picks, L backs out). Raw Press/Release pass through untouched (the
    // release of the opening long-press must not close it).
    if (menuOpen) {
        if (button == Button::Right && event == ButtonEvent::ShortPress) {
            menuSel = (uint8_t)((menuSel + 1) % (menuN ? menuN : 1));
            menuDirty = true;
            menuUntil = millis() + 10000;
            return;
        }
        if (button == Button::Right && event == ButtonEvent::LongPress) {
            menuConfirm();
            return;
        }
        if (event == ButtonEvent::ShortPress || event == ButtonEvent::LongPress) {
            menuClose();
        }
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
        menuFull = true;
        menuUntil = millis() + 10000;
        return;
    }
    // Right short does nothing on this screen (durations dropped).
    // Right long: cycle views — north-up → bearing-up → best-fit.
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        viewMode = (viewMode + 1) % 3;
        redrawCurrentPage();
        return;
    }
}
