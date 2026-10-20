#include "screen_race.h"
#include "screens.h"
#include "race_session.h"
#include "race_run.h"
#include "race_courses.h"
#include "backend.h"
#include "gps_mock.h"
#include "serial_buffer.h"

#include <TFT_eSPI.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "sound.h"

static const uint16_t RBG = TFT_BLACK;
static const uint16_t RFG = TFT_WHITE;
static const uint16_t RDIM = 0x8410; // neutral gray
static const uint16_t RBOAT = TFT_YELLOW;
// Second dynamic segment (todo 39): the boat's own course over ground. Cyan is
// the one bright colour this screen does not already spend — yellow is the
// boat, the mark circles and the target stub, green the course lines, red the
// buoys themselves (todo 43; the first red OBJECT on this map, which used to be
// alerts only) and red every alert — so the two stubs are distinguishable
// without a legend.
static const uint16_t RCOG = TFT_CYAN;
// Below this the COG is noise: gps.course is direction of TRAVEL (there is no
// compass on the device), so at rest it wanders around the mast. The stub is
// then simply absent instead of pointing somewhere that means nothing.
static const double COG_MIN_KN = 0.5;
// Both dynamic stubs: the length is what tells them apart at a glance as much
// as the colour. The destination stub is 2/3 of the COG stub's length
// (2026-10-20, user) — 40 * 2/3 = 26.7, kept as 27 px.
static const int STUB_PX = 40;      // the boat's own COG
static const int STUB_DEST_PX = 27; // toward the destination

// Finished-race hold (todo 38). The backend stops pushing a session the moment
// it derives `finished` — i.e. right after our run uploads — and the page then
// falls to its empty state, which clears the header too: the sailor's time used
// to die with the session it belonged to. So on the finish edge the screen
// snapshots the result and keeps the header alive (FINISHED banner + the frozen
// elapsed time) until the next run/session, or until the page is left.
// RAM only, on purpose: a reboot has no race to report.
static bool finHold = false;
static long finHoldSession = -1; // the session the held result belongs to
static long finHoldElapsed = 0;  // gun -> finish, seconds

void raceFinishHoldClear()
{
    finHold = false;
    finHoldSession = -1;
    finHoldElapsed = 0;
}
static const uint16_t MENU_GRAY = 0x7BEF; // CONFIG gray: menu reference color

// Frame: full-bleed N/W/E border, south edge +4.
//
// Layout rule (todo 37): the map is ONE rectangle and it owns every pixel
// inside it. All chrome — the readout rows, the banners — lives in the header
// band above WIRE_TOP, and the map's fill, its fit box and, in time, its clip
// all reference WIRE_TOP/WIRE_Y1 so the three can never disagree on "where the
// map is". Nothing is drawn over the sailing picture except a deliberate
// alert (a banner, the BAD RUN tag), which is transient and outranks the
// geometry.
//
// Header: ONE font-4 row of 26px, split into three cells of the same width —
// DST | SPD | TIME — separated by 1px rules. The banner used to share this
// band (row 1 = banner + speed, row 2 = distance + clock); it now owns the
// bottom band (see BAND_*), which handed the map the second row's 26px back.
// The distance and the clock used to live INSIDE the map (top-right and
// bottom-right, the clock in font 6) and the clock's box alone masked ~11% of
// the fit box — including the leeward half of the start line in the overview,
// which is what the sailor looks at before the gun.
static const int HDR_H = 30;           // one 26px text row + 4px top inset
static const int HDR_ROW1_Y = 4;       // glyph top of the row
static const int HDR_CY = HDR_ROW1_Y + 13; // MC_DATUM centre of a font-4 value
static const int MAP_X = 0;
static const int MAP_W = 320;          // full screen width (no side pane)
static const int WIRE_TOP = HDR_H + 2; // map top: the map owns from here down
static const int WIRE_Y1 = 208;        // ...to the frame south (strip is gone)
static const int FRAME_SOUTH = 213;
// The grid's bottom rule. It closes the three cells AND is the map area's top
// border: the map fill starts at WIRE_TOP, so this row is never painted over.
static const int HDR_RULE_Y = WIRE_TOP - 1;  // 31
// The grid: three cells across 320px. 320 is not divisible by 3, so the odd
// pixel goes to the two OUTER cells (107 | 106 | 107): symmetric, and 106px is
// enough for the widest value each cell can hold ("15.7 Kn" ~83px, "+99:59"
// ~90px), so a readout never touches a rule.
static const int CELL_EDGE[4] = {0, 107, 213, 320};
static const int CELL_CX[3] = {53, 160, 266}; // DST | SPD | TIME centres
// Bottom band (the hint region, far bottom): it holds the HINTS or the BANNER,
// never both — the banner is the alert you must not miss (FINISHED, OCS, GO,
// the gun countdown, a committee signal), so while it is up the hints step
// aside and they come back when it goes. 26px tall, i.e. exactly one font-4
// line under the frame's south rule; the font-2 hints keep their baseline.
static const int BAND_Y = FRAME_SOUTH + 1;  // 214
static const int BAND_H = 26;               // 214..239
static const int BAND_HINT_Y = 239;         // BL baseline of the hint pair
static const int BAND_BANNER_Y = BAND_Y;    // TC top of a font-4 banner
// Right values pane: 1/5 width, six mini-labeled rows filling the height.
static const int PANE_X = 256;
static const int PANE_W = 64;
static const int PANE_TOP = 1;                 // starts at the very top (under the 1px frame)
static const int PANE_H = WIRE_Y1 - PANE_TOP;  // fills down to the map bottom
// Top edge of pane row r (6 equal rows filling the pane, no top gap).
static inline int paneRowY(int r) { return PANE_TOP + (r * PANE_H) / 6; }

static void drawFrameChrome()
{
    tft.drawRect(0, 0, 320, FRAME_SOUTH, RDIM);
    // The header grid: two vertical rules closing three equal cells, and a
    // horizontal rule along their bottom (todo 50). The horizontal one doubles
    // as the map area's top border. Chrome, like the frame: drawn once per full
    // repaint, and every readout erases only inside its own cell, so the rules
    // survive the 200ms path untouched.
    for (int i = 1; i < 3; i++)
        tft.drawFastVLine(CELL_EDGE[i], 1, HDR_RULE_Y, RDIM);
    tft.drawFastHLine(0, HDR_RULE_Y, 320, RDIM);
}

static const int AUTO_DIST_OPTIONS[] = {10, 50, 100, 200};
static const uint8_t AUTO_DIST_N = 4;
static int autoDistM = -1;
// Auto-mode clearance hold. The engine advances progIdx at the clear-route
// event, i.e. the instant the pass is scored — and for a rounding that is
// still ON the buoy, so dropping the zoom on that edge (the old behaviour)
// cut it before the boat had actually got round the mark, which on the last
// mark looked like the view abandoning it mid-turn. Keep the zoom pinned to
// the point being rounded until the boat is clear of it by this much, then
// hand back to the overview and let the normal distance rule re-engage.
static const double AUTO_CLEAR_MARGIN_M = 30.0; // added to the mark's radius
static const double AUTO_CLEAR_MIN_M = 60.0;    // never release closer than this
static const double AUTO_CLEAR_MAX_M = 150.0;   // a huge radius must not pin the view
static uint8_t focusMode = 0; // 0 map,1 target,2 auto
static bool focusEnabled = false;
static bool focusNext = false;
static double targetDistM = -1.0;
static bool focusValid = false;
static double focusLat = 0.0, focusLon = 0.0;
static char autoHoldTag[12] = {0};
// The point the zoom was centred on during the PREVIOUS pass — i.e. the mark
// being rounded when a pass advances progIdx. Captured before the new target
// overwrites focusLat/focusLon (nextDestination has already switched by the
// time the pass is seen).
static double prevFocusLat = 0.0, prevFocusLon = 0.0;
// Active clearance hold: keep zooming on (lat,lon) until the boat is
// autoClearR metres from it (see AUTO_CLEAR_*).
static bool autoClearHold = false;
static double autoClearLat = 0.0, autoClearLon = 0.0, autoClearR = 0.0;
// Was the zoom ON during the PREVIOUS pass? The auto block above re-qualifies
// the NEW target before the pass is even looked at, and the new leg is
// usually out of range, so focusEnabled is already false by then: the
// clearance hold has to ask what the view was doing a pass earlier.
static bool prevZoomOn = false;

static void autoDistEnsure()
{
    if (autoDistM >= 0) return;
    autoDistM = 100;
    Preferences p;
    if (p.begin("race", true)) { autoDistM = (int)p.getInt("autoDist", 100); p.end(); }
}

static void autoDistSave(int v)
{
    Preferences p;
    if (p.begin("race", false)) { p.putInt("autoDist", v); p.end(); }
}

static int autoDistNext()
{
    autoDistEnsure();
    for (uint8_t i = 0; i < AUTO_DIST_N; i++) {
        if (autoDistM <= AUTO_DIST_OPTIONS[i]) {
            int n = AUTO_DIST_OPTIONS[(i + 1) % AUTO_DIST_N];
            autoDistM = n; autoDistSave(n); return n;
        }
    }
    autoDistM = AUTO_DIST_OPTIONS[0]; autoDistSave(autoDistM); return autoDistM;
}

static void viewModeEnsure()
{
    static bool done = false;
    if (done) return;
    done = true;
    Preferences p;
    if (p.begin("race", true)) {
        int v = (int)p.getInt("viewMode", 0);
        if (v < 0 || v > 2) v = 0;
        focusMode = (uint8_t)v;
        p.end();
    }
}

static void viewModeSave()
{
    Preferences p;
    if (p.begin("race", false)) { p.putInt("viewMode", focusMode); p.end(); }
}

static const double DEG_M = 111320.0;

// View state (RAM only). RR cycles the BEARING: 0 north-up, 1 bearing-up
// (next destination up), 2 WIDE — the course's own orientation, chosen to fill
// a 320x150 window. In Target/Auto framing the span is fixed, so no rotation
// can help and wide resolves to north-up.
static uint8_t viewMode = 0;

static int lastRaceCourseKey = -1; // sessionId+version fingerprint
static int lastResetCourseKey = -2; // tracks the session key raceRun was last reset for
static bool mapDirty = true;       // a full clear is waiting for a map redraw
static unsigned long lastMapDraw = 0;
// Set when the map actually repainted this pass. The view tag lives in the
// map's bottom-right corner (the only free space large enough), so it must
// be redrawn with the map, not just when viewMode changes — otherwise the
// next 1Hz map pass paints straight over it.
static bool mapRepainted = false;

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
    double maxR = 0.0; // largest mark radius (m) — grows the fit box
    auto eatBox = [&](double lat, double lon) {
        if (lat < minLat) minLat = lat;
        if (lat > maxLat) maxLat = lat;
        if (lon < minLon) minLon = lon;
        if (lon > maxLon) maxLon = lon;
    };
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        eatBox(raceSession.marks[i].lat, raceSession.marks[i].lon);
        if (raceSession.marks[i].r > maxR) maxR = raceSession.marks[i].r;
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
    if (focusNext && focusValid) {
        p.lat0 = focusLat; p.lon0 = focusLon;
    } else {
        p.lat0 = (minLat + maxLat) / 2.0;
        p.lon0 = (minLon + maxLon) / 2.0;
    }
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
    // Grow the fit box by the largest mark radius so the circles (and their
    // labels) stay fully inside and the course is centered, not clipped.
    const double rGrowth = maxR / DEG_M;
    spanX += 2 * rGrowth;
    spanY += 2 * rGrowth;
    if (focusNext && focusValid) {
        spanX = 0.0003; spanY = 0.0003;
    }
    if (spanX < 0.0005) spanX = 0.0005;
    if (spanY < 0.0005) spanY = 0.0005;
    const int pad = 12;
    const int ww = MAP_W - 2 * pad;
    const int wh = (WIRE_Y1 - WIRE_TOP) - 2 * pad;
    const double sx = ww / spanX;
    const double sy = wh / spanY;
    p.scale = sx < sy ? sx : sy;
    if (focusNext && focusValid) { p.rcx = 0.0; p.rcy = 0.0; }
    else { p.rcx = (rminx + rmaxx) / 2.0; p.rcy = (rminy + rmaxy) / 2.0; }
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

// Todo 48, "wide": pick the rotation that fits the course best in a landscape
// window. The overview fits the course's bounding box, and a frame twice as
// wide as it is tall wastes half of itself whenever the course runs the other
// way — a diagonal course wastes both ways. Search the full circle in 5 deg
// steps and keep the orientation with the LARGEST scale, so the whole course
// stays on screen while the picture grows to the biggest one the frame allows.
// buildProjection is pure arithmetic over the course's own points (marks, start
// and finish lines — never the boat), so the chosen angle depends on the course
// alone: it cannot drift or jitter while sailing, and 5 deg costs cos(5) =
// 0.4% of the scale at worst.
static double wideRotation()
{
    double best = 0.0, bestScale = -1.0;
    for (int a = 0; a < 180; a += 5) {
        RaceProj q;
        buildProjection(q, (double)a);
        if (q.ok && q.scale > bestScale) { bestScale = q.scale; best = (double)a; }
    }
    return best;
}

// Little rounding-side arrow on P/S marks (any type, gates included):
// half-arc on the side the arrow implies (P = west half CCW, S = east half
// CW) with a barb at the tip. Repainted identically with the static layer.
static void drawRoundArrows(int cx, int cy, int rr, char side, uint16_t color)
{
    if (side != 'P' && side != 'S') return;
    if (rr < 5) return; // no room for a tooth at all
    // Todo 44: the rounding direction lives ON the circle, not in an arc inside
    // it. Four teeth, at 45/135/225/315 (user decision) - the diagonals leave
    // the top, bottom and sides clear for the label, the number and the boat -
    // phase fixed to the SCREEN, so they do not rotate with the bearing-up or
    // fit views: the set means "go round this way", not "where is upwind".
    //
    // A tooth is a solid saw-tooth whose LONG side is the ARC itself - it spans
    // a few degrees of the circle and is filled right up to the ring, so there
    // is no wedge of background between the tooth and the mark - with the SHORT
    // side standing out of the circle at its trailing end (long = 2 x short).
    // That shallow-edge-forward / steep-back profile is what reads as a
    // direction of travel, and nothing is added inside the ring, where the buoy
    // dot, the number and the ring live. (A straight tangent side was tried and
    // rejected: on a 16 px radius a 12 px tangent chord leaves a visible gap of
    // background between the tooth and the circle.)
    const bool cw = (side == 'S'); // S rounds clockwise on screen
    int L = (2 * rr) / 5;          // short side, px (twice the old rr/5)
    if (L < 4) L = 4;
    if (L > 10) L = 10;
    double dA = (2.0 * L) / rr;    // angular width of the long side, radians
    if (dA > 1.30) dA = 1.30;      // ~75 deg: never let neighbouring teeth meet
    const double sgn = cw ? -1.0 : 1.0; // backwards along travel
    for (int a = 45; a < 360; a += 90) {
        const double t = a * M_PI / 180.0;
        const double tb = t + sgn * dA;         // trailing end of the long side
        const double sb = sin(tb), cb = cos(tb);
        // Outer corner: one short side out of the circle, at the trailing end.
        const int ox = (int)(cx + sb * (rr - 1 + L)), oy = (int)(cy - cb * (rr - 1 + L));
        // Fan from that corner along the arc (inner edge at rr - 1, i.e. over the
        // ring by a pixel, so no hairline of background can show through).
        const int steps = 6;
        int px0 = (int)(cx + sb * (rr - 1)), py0 = (int)(cy - cb * (rr - 1));
        for (int s = 1; s <= steps; s++) {
            const double tt = tb + (t - tb) * s / steps;
            const int px1 = (int)(cx + (rr - 1) * sin(tt)), py1 = (int)(cy - (rr - 1) * cos(tt));
            tft.fillTriangle(ox, oy, px0, py0, px1, py1, color);
            px0 = px1;
            py0 = py1;
        }
    }
}

// Small wind arrow at (cx,cy): points toward `fromDeg` (the direction the
// wind comes FROM, meteorological) with a barbed head at the tip.
static void drawWindArrow(int cx, int cy, int fromDeg, int r, uint16_t color)
{
    const double a = fromDeg * M_PI / 180.0;
    const double sx = sin(a), cz = cos(a);
    const int tx = cx + (int)(r * sx), ty = cy - (int)(r * cz); // tip (upwind)
    const int bx = cx - (int)(r * sx), by = cy + (int)(r * cz); // tail
    tft.drawLine(bx, by, tx, ty, color);
    // Head: two barbs off the tip, unit tail-ward vector rotated ±25°.
    const double ux = -sx, uy = cz;
    const double hd = r * 0.5;
    for (double s = -25.0; s <= 25.0; s += 50.0) {
        const double w = s * M_PI / 180.0;
        const double rx = ux * cos(w) - uy * sin(w);
        const double ry = ux * sin(w) + uy * cos(w);
        tft.drawLine(tx, ty, tx + (int)(rx * hd), ty + (int)(ry * hd), color);
    }
}
// pre-start = start, racing = marks[progIdx], finished = finish).
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
    const bool preStart = (gun <= 0 || (!raceRunStarted() && strcmp(raceSession.status, "finished") != 0 && !raceRunFinished()));
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
    if (raceSession.markCount == 0) {
        idx = 255;
        return;
    }
    idx = raceProgIdx();
    // Every mark scored but the run is not finished yet: the finish LINE is
    // armed (scorePass sets progIdx = markCount and only a line cross ends it,
    // race_run.cpp). This branch used to be missing, so the highlight clamped
    // to the LAST mark and stayed on it all the way down to the finish, while
    // the destination stub and the pane already said FINISH — the target ring
    // and the boat's actual target disagreed for the whole last leg. Mirror
    // nextDestination() exactly: finishSameAsStart lights the start line
    // (lineIdx 0), a distinct finish is lineIdx 1.
    if (idx >= raceSession.markCount) {
        if (raceSession.finishSameAsStart) {
            if (raceSession.startLine.valid) {
                isLine = true;
                lineIdx = 0;
                return;
            }
        } else if (raceSession.finishLine.valid) {
            isLine = true;
            lineIdx = 1;
            return;
        }
        idx = raceSession.markCount - 1; // no line to light: keep the last mark
    }
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
static int oldCog[2] = {0, 0};
static bool oldHaveCog = false;
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
// Target tags stay COMPACT ("ST", "FIN", "1P"): they render in the fixed
// -width right-pane cells, so spelling them out ("START", "1 - Port")
// overflowed the cell and changed its width.
static bool nextDestination(TinyGPSPlus& gps, double& lat, double& lon, char* tag, size_t tagLen)
{
    (void)gps;
    if (!raceSession.valid) return false;
    const long gun = raceGunEpoch();
    const bool preStart = (gun <= 0 || (!raceRunStarted() && strcmp(raceSession.status, "finished") != 0 && !raceRunFinished()));
    if (preStart) {
        if (raceSession.startLine.valid) {
            lat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
            lon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
            snprintf(tag, tagLen, "START");
            return true;
        }
        for (uint8_t i = 0; i < raceSession.markCount; i++) {
            if (raceSession.marks[i].type == RaceMarkStart) {
                lat = raceSession.marks[i].lat;
                lon = raceSession.marks[i].lon;
                snprintf(tag, tagLen, "START");
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
        snprintf(tag, tagLen, "FINISH");
        return true;
    }
    if (raceRunStarted() && raceProgIdx() >= raceSession.markCount) {
        if (raceSession.finishSameAsStart && raceSession.startLine.valid) {
            lat = (raceSession.startLine.latA + raceSession.startLine.latB) / 2.0;
            lon = (raceSession.startLine.lonA + raceSession.startLine.lonB) / 2.0;
        } else if (raceSession.finishLine.valid) {
            const RaceSeg& fin = raceSession.finishLine;
            lat = (fin.latA + fin.latB) / 2.0;
            lon = (fin.lonA + fin.lonB) / 2.0;
        } else {
            const RaceMark& last = raceSession.marks[raceSession.markCount - 1];
            lat = last.lat; lon = last.lon;
        }
        snprintf(tag, tagLen, "FINISH");
        return true;
    }
    // The engine switches progIdx exactly at the clear-route event,
    // so the display follows it directly: no presentation hold of its own.
    uint8_t targetIdx = raceProgIdx();
    if (targetIdx >= raceSession.markCount) targetIdx = raceSession.markCount; // finish
    // progIdx IS the target: the engine advanced it at the switch already.
    uint8_t idx = (targetIdx >= raceSession.markCount) ?
        (raceSession.markCount ? raceSession.markCount - 1 : 0) : targetIdx;
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

// Todo 45: the map's own scale, read off the corner it belongs to.
//
// The ZERO is the map's bottom-right corner: a horizontal bar runs LEFT from
// it and the label above it says what that bar covers. The corner is a real
// anchor (the frame), so nothing has to float in the picture, and the bar is
// the last thing the static layer draws, so a course line never cuts it.
//
// Sizing (user decision): the bar must be between 1/5 and 1/2 of the map
// frame's height — 30..75 px, i.e. big enough to read at the helm and never
// half the picture. The DISTANCE is what gets chosen, from a 1-2-5 sequence
// (…10, 20, 50, 100, 200, 500, 1000…), so the label is always a round number:
// the largest one whose bar still fits under the cap. The 1-2-5 steps are at
// most 2.5x apart and the window is exactly 2.5x tall, so a fitting value
// always exists (one of any adjacent pair lands inside).
static void drawScaleBar(const RaceProj& p)
{
    if (p.scale <= 0) return;
    const double mPerPx = DEG_M / p.scale;
    const int minPx = (WIRE_Y1 - WIRE_TOP) / 5; // 30 px
    const int maxPx = (WIRE_Y1 - WIRE_TOP) / 2; // 75 px
    const int zeroX = MAP_W - 4;                // the frame's inner corner (2 px
                                                // of air before the border)
    const int zeroY = WIRE_Y1 - 3;
    int bestPx = 0, bestM = 0;
    double bestErr = 1e9;
    for (uint8_t e = 0; e <= 5; e++) {          // 1 m .. 50 km
        const double dec = pow(10.0, e);
        for (uint8_t k = 0; k < 3; k++) {
            const double d = dec * (k == 0 ? 1.0 : (k == 1 ? 2.0 : 5.0));
            const double lenPx = d / mPerPx;
            if (lenPx > maxPx) break;           // ascending: nothing bigger fits
            if (lenPx < minPx) continue;
            bestM = (int)d;                     // the largest that fits wins
            bestPx = (int)(lenPx + 0.5);
        }
    }
    if (!bestM) {
        // Nothing fits the window (an extreme zoom): fall back to the closest.
        for (uint8_t e = 0; e <= 5; e++) {
            const double dec = pow(10.0, e);
            for (uint8_t k = 0; k < 3; k++) {
                const double d = dec * (k == 0 ? 1.0 : (k == 1 ? 2.0 : 5.0));
                const double lenPx = d / mPerPx;
                const double err = fabs(lenPx - (minPx + maxPx) / 2.0);
                if (err < bestErr) {
                    bestErr = err;
                    bestM = (int)d;
                    bestPx = (int)(lenPx + 0.5);
                }
            }
        }
    }
    if (!bestPx) return;
    const int farX = zeroX - bestPx;
    // Bar with a tick at each end (the far tick is what makes it read as a
    // measurement rather than a stray line).
    tft.drawLine(farX, zeroY, zeroX, zeroY, RDIM);
    tft.drawLine(farX, zeroY - 3, farX, zeroY + 1, RDIM);
    tft.drawLine(zeroX, zeroY - 3, zeroX, zeroY + 1, RDIM);
    char txt[12];
    snprintf(txt, sizeof(txt), "%d m", bestM);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(RDIM, RBG);
    tft.drawString(txt, zeroX - 2, zeroY - 17, 2);
    tft.setTextDatum(MC_DATUM);
}

// --- Map-rect clip (todo 36) -------------------------------------------------
// The static layer projects the course in whatever view is active and then
// draws it, but the geometry is not bounded by the map: in Target view the
// span is forced to 0.0003 deg around the target, and in heading-up the fit
// box itself rotates, so most of the course falls outside the window and a
// mark-to-mark leg becomes a page-long chord running over the header readouts
// and across the frame. Clip every segment against the rectangle the map owns
// (WIRE_TOP..WIRE_Y1) with Liang-Barsky, and reject wholly-outside geometry
// before it costs a single SPI byte.
// `inset` shrinks the rect: a wide line is drawn centred on the segment, so a
// 3px start line clipped exactly on the border would still leave a row in the
// header band.
static bool clipToMap(int& x0, int& y0, int& x1, int& y1, int inset = 0)
{
    const double left = MAP_X + inset, right = MAP_X + MAP_W - 1 - inset;
    const double top = WIRE_TOP + inset, bottom = WIRE_Y1 - 1 - inset;
    const double dx = (double)(x1 - x0), dy = (double)(y1 - y0);
    double t0 = 0.0, t1 = 1.0;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {(double)x0 - left, right - (double)x0,
                         (double)y0 - top, bottom - (double)y0};
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false; // parallel to this edge, outside it
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
    }
    const int nx0 = (int)lround((double)x0 + t0 * dx);
    const int ny0 = (int)lround((double)y0 + t0 * dy);
    const int nx1 = (int)lround((double)x0 + t1 * dx);
    const int ny1 = (int)lround((double)y0 + t1 * dy);
    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
    return true;
}

// A mark is worth drawing only if its circle (plus the round arrows standing
// ~2*rPx/5 outside it) meets the map rect at all.
static bool circleOnMap(int cx, int cy, int r)
{
    return cx + r >= MAP_X && cx - r < MAP_X + MAP_W &&
           cy + r >= WIRE_TOP && cy - r < WIRE_Y1;
}

// Static course layer: legs, segments, circles, numbers, header wind.
// Identical pixels every time — safe to repaint over dirty regions.
static void drawStaticLayer(const RaceProj& p, double rotEff)
{
    int px = 0, py = 0, qx = 0, qy = 0;
    for (uint8_t i = 0; i + 1 < raceSession.markCount; i++) {
        projToPx(p, raceSession.marks[i].lat, raceSession.marks[i].lon, px, py);
        projToPx(p, raceSession.marks[i + 1].lat, raceSession.marks[i + 1].lon, qx, qy);
        if (clipToMap(px, py, qx, qy)) tft.drawLine(px, py, qx, qy, RDIM);
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
        if (clipToMap(px, py, qx, qy, 2))
            tft.drawWideLine(px, py, qx, qy, 3, hl ? RBOAT : TFT_GREEN);
    }
    if (raceSession.finishLine.valid && !raceSession.finishSameAsStart) {
        const RaceSeg& s = raceSession.finishLine;
        projToPx(p, s.latA, s.lonA, px, py);
        projToPx(p, s.latB, s.lonB, qx, qy);
        const bool hl = tLine && tLineIdx == 1;
        if (clipToMap(px, py, qx, qy, 1))
            tft.drawWideLine(px, py, qx, qy, 2, hl ? RBOAT : TFT_GREEN);
    }
    char num[12];
    // Target circles are deferred to the end: on a pile of coincident marks
    // (e.g. W/L 1 and 3 on the same buoy) the later white ring would paint
    // over the yellow target. The highlighted one always prevails.
    uint8_t defer[10];
    uint8_t deferN = 0;
    for (uint8_t i = 0; i < raceSession.markCount; i++) {
        const RaceMark& m = raceSession.marks[i];
        projToPx(p, m.lat, m.lon, px, py);
        int rPx = (int)(m.r / DEG_M * p.scale);
        if (rPx < 3) rPx = 3;
        if (rPx > 40) rPx = 40;
        // Marks outside the window are skipped whole (todo 36): a circle
        // centred in the header band is not clipped by the rect, it paints
        // there. +12 covers the round arrows, which stand outside the ring.
        const bool onMap = circleOnMap(px, py, rPx + 12);
        // Coincident marks (piles) are ONE physical buoy, and two things now
        // depend on that: the label draws once (its !firstOfPile skip below) and
        // the buoy dot draws once (todo 43). The circles still draw per mark.
        bool firstOfPile = true;
        for (uint8_t k = 0; k < i; k++) {
            if (raceSession.marks[k].lat == m.lat && raceSession.marks[k].lon == m.lon) {
                firstOfPile = false;
                break;
            }
        }
        // Target object lights up yellow; everything else is white, lines
        // carry the green. Gate pairs light up together.
        bool isTgt = false;
        if (tGate && m.type == RaceMarkGate && m.gate[0] && strcmp(m.gate, tGateId) == 0) {
            isTgt = true;
        } else if (!tGate && !tLine && i == tIdx) {
            isTgt = true;
        }
        if (isTgt) {
            if (deferN < 10) defer[deferN++] = i;
        } else if (onMap) {
            tft.drawCircle(px, py, rPx, RFG);
            drawRoundArrows(px, py, rPx, m.side, RFG);
        }
        if (firstOfPile && onMap) {
            // Todo 43: the buoy ITSELF, at true scale. The circle above is the
            // rounding radius (30 m by default) - 60 m across - while a real
            // racing buoy is 1-5 m, so "how far off the buoy am I" had no
            // picture at all. buoyDiam is the course's physical diameter and it
            // already rides every mark in the session payload.
            //
            // True scale, with a floor and a cap: at overview the honest size is
            // ~1 px (0.23 px/m for a full-size course), which would be a ghost,
            // and in the next-target focus it is ~11 px, which for a 30 m mark
            // would swallow the ring's interior. Between those the dot IS the
            // buoy's size on screen.
            int bR = (int)((m.buoyDiam / 2.0f) / DEG_M * p.scale + 0.5f);
            if (bR < 2) bR = 2;
            if (bR > 6) bR = 6;
            tft.fillCircle(px, py, bR, TFT_RED);
        }
        if (!firstOfPile) continue;
        if (!onMap) continue; // label would land outside the rect (todo 36)
        num[0] = '\0';
        bool first = true;
        for (uint8_t j = i; j < raceSession.markCount; j++) {
            if (raceSession.marks[j].lat != m.lat || raceSession.marks[j].lon != m.lon) continue;
            char tmp[5];
            snprintf(tmp, sizeof(tmp), "%s%d", first ? "" : "-", j + 1);
            strncat(num, tmp, sizeof(num) - strlen(num) - 1);
            first = false;
        }
        // Todo 40: the label is drawn per index, not as one string, so inside a
        // pile the index the ring is on is legible: "1-3" reads as **1**-3
        // upwind and 1-**3** on the way back. Colour only (yellow digits, the
        // same RBOAT as the ring) — a box would move the label and cost an erase
        // path of its own — and the whole string is still centred as one block,
        // so its position never shifts as the target moves. A gate target
        // highlights BOTH of its indices, exactly as both rings light up.
        // Always above the circle so it never overlaps the ring — nor a line
        // (start/finish) the mark happens to sit on. Clamped to the map's top
        // edge: in Target view a mark near the top would otherwise put its
        // label up in the header band, where the map's own clear rect cannot
        // erase it (that is how a stale label survived a view change).
        int ly = py - rPx - 10;
        if (ly < WIRE_TOP + 9) ly = WIRE_TOP + 9;
        int lx = px - tft.textWidth(num, 2) / 2;
        bool firstRun = true;
        for (uint8_t j = i; j < raceSession.markCount; j++) {
            const RaceMark& pj = raceSession.marks[j];
            if (pj.lat != m.lat || pj.lon != m.lon) continue;
            if (!firstRun) {
                tft.setTextDatum(ML_DATUM);
                tft.setTextColor(RDIM, RBG);
                tft.drawString("-", lx, ly, 2);
                lx += tft.textWidth("-", 2);
            }
            const bool jTgt =
                (tGate && pj.type == RaceMarkGate && pj.gate[0] &&
                 strcmp(pj.gate, tGateId) == 0) ||
                (!tGate && !tLine && j == tIdx);
            char idx[4];
            snprintf(idx, sizeof(idx), "%d", j + 1);
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(jTgt ? RBOAT : RDIM, RBG);
            tft.drawString(idx, lx, ly, 2);
            lx += tft.textWidth(idx, 2);
            firstRun = false;
        }
        tft.setTextDatum(MC_DATUM);
    }
    // Deferred target rings, on top of everything: the highlighted mark
    // prevails even inside its own pile.
    for (uint8_t d = 0; d < deferN; d++) {
        const RaceMark& m = raceSession.marks[defer[d]];
        projToPx(p, m.lat, m.lon, px, py);
        int rPx = (int)(m.r / DEG_M * p.scale);
        if (rPx < 3) rPx = 3;
        if (rPx > 40) rPx = 40;
        if (!circleOnMap(px, py, rPx + 12)) continue; // todo 36
        tft.drawCircle(px, py, rPx, RBOAT);
        // Same heads at the same radius as the white pass, so the yellow ones
        // land exactly on the white ones: the highlight follows the circle by
        // construction (todo 44).
        drawRoundArrows(px, py, rPx, m.side, RBOAT);
    }
    // Header wind readout moved to the right pane (WND row) — the arrow
    // lives there as degrees now.
    // The mock banner also moved to the main screen (yellow FX tile). Its
    // header slot no longer holds anything, so the old
    // fillRect(8, 4, 44, 18) that kept it "clean" was removed: with the
    // countdown banner now at the top-left it punched a black hole over
    // the text on every 1Hz map repaint.
    drawScaleBar(p);
}

static void resetRaceText(); // defined below; the empty state clears and needs it

// Overlay readout caches (see drawReadoutC): the last string drawn and its
// exact pixel width, so a box that shrinks can blank its own leftover first.
static char ovSpd[16] = {0}; static int16_t ovSpdW = 0;
static char ovDst[16] = {0}; static int16_t ovDstW = 0;
static char ovTim[12] = {0}; static int16_t ovTimW = 0;

static void drawRaceMap(TinyGPSPlus& gps, bool full)
{
    if (!raceSession.valid) {
        if (full) {
            // Clear the map AND the header: stale course pixels must not
            // survive the empty state, and the header now carries the readouts
            // (todo 37), so it has to come down with the map. Starting at
            // WIRE_TOP would leave SPD/DST/TIME floating over the "no session"
            // message. PANE_TOP is the legacy name for the row just inside the
            // frame, i.e. 1.
            tft.fillRect(0, PANE_TOP, 320, WIRE_Y1 - PANE_TOP, RBG);
            drawFrameChrome();
            // The clear wiped the header banner and the readouts too, and all
            // of them are change-detected, so their caches must be reset or
            // they simply never come back.
            resetRaceText();
            const int cx = MAP_X + MAP_W / 2;
            const int cy = WIRE_TOP + (WIRE_Y1 - WIRE_TOP) / 2;
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(RFG, RBG);
            if (finHold) {
                // The race is over and the backend has taken the session away;
                // the header still carries the result (todo 38), so this must
                // not claim there was never a session.
                tft.drawString("Race finished", cx, cy - 30, 2);
            } else {
                tft.drawString("No session available", cx, cy - 30, 2);
            }
            drawLabel1C(cx, cy + 2, "Start a practice session from menu", RDIM);
            drawLabel1C(cx, cy + 18, "or await committee instructions", RDIM);
            // The view tag sits inside the cleared area; force it back.
            mapRepainted = true;
        }
        dynOk = false;
        return;
    }
    // Effective rotation: 0 north-up, bearing-to-destination up, or wide — the
    // course's own orientation from wideRotation(). Falls back to north-up
    // whenever the fix/destination is missing.
    double rotEff = 0.0;
    double destLat = 0.0, destLon = 0.0;
    char destTag[12] = {0};
    const bool haveDestHere = nextDestination(gps, destLat, destLon, destTag, sizeof(destTag));
    if (viewMode == 1 && gps.location.isValid() && haveDestHere) {
        double brg = TinyGPSPlus::courseTo(
            gps.location.lat(), gps.location.lng(), destLat, destLon);
        if (brg < 0) brg += 360.0;
        if (brg >= 360.0) brg -= 360.0;
        rotEff = brg;
    } else if (viewMode == 2) {
        rotEff = wideRotation();
    }

    RaceProj p;
    buildProjection(p, rotEff);
    if (!p.ok) return;

    // Todo 46: the dirty path below erases the boat and dashes, then REPAINTS
    // the static layer — which paints but never clears, so it is only "identical
    // pixels" while the PROJECTION is unchanged. In Target view every mark
    // switch re-centres the fit on the new target (the span is fixed, the origin
    // moves), and no focus switch happens there (focusEnabled never toggles), so
    // nothing asked for a clear: the old mark labels, circles and legs stayed
    // standing beside the new view. Fingerprint the camera — origin and scale —
    // and force one full repaint whenever it moves. Rotation itself is not
    // compared here (the throttled branch below owns it), but the origin it
    // produces must be: see the ordering note.
    // Rotating views can't use dirty updates (the whole frame turns): throttle
    // them to 1Hz full redraws instead. Decide this FIRST: the fit origin is
    // computed in ROTATED space, so every heading change moves p.lat0/lon0 —
    // if the fingerprint below were allowed to read that as "the camera moved"
    // it would promote every rotating pass to `full` and the throttle would
    // never run (the map was cleared and repainted at the 200ms pass rate, up
    // to 5x/s, which is what heading-up flicker was). The epsilon is degrees of
    // bearing-to-destination: 0.1 was noise-level (2 m of GPS jitter 30 m out
    // moves the bearing by ~4 deg), 2 deg is under what the eye can see on a
    // still picture and above what a boat holding a course produces.
    static const double ROT_EPS_DEG = 2.0;
    const bool rotating = fabs(rotEff - oldRotEff) > ROT_EPS_DEG;

    static double lastLat0 = 1e9, lastLon0 = 1e9, lastScale = 0.0;
    const bool camMoved =
        fabs(p.lat0 - lastLat0) * p.scale > 0.5 ||
        fabs((p.lon0 - lastLon0) * p.cosLat) * p.scale > 0.5 ||
        fabs(p.scale - lastScale) > lastScale * 0.001;
    if (camMoved && !rotating) full = true;

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
        // This fill stops at WIRE_TOP, i.e. below the header: it no longer
        // touches the readouts (that is the point of todo 37), so their cached
        // extents stay valid and the erase keeps working normally. Nothing
        // needs forgetting here any more.
        drawFrameChrome();
        drawStaticLayer(p, rotEff);
        lastLat0 = p.lat0; lastLon0 = p.lon0; lastScale = p.scale;
        oldRotEff = rotEff;
        lastMapDraw = millis();
        dynOk = false;
        mapRepainted = true; // the view tag lives in this area; see below
    }

    // Boat: triangle on the chart, or a projected dot on the frame edge.
    // Dashed yellow stub from the boat (or edge dot) toward the destination.
    // Dirty path: erase old dash + old boat, repaint identical static pixels,
    // draw the new dash + boat. Skipped entirely when nothing moved.
    int px = 0, py = 0;
    int bx = -1, by = -1, dx = -1, dy = -1, ex = -1, ey = -1;
    int cogX = -1, cogY = -1;
    bool haveBoat = false, haveDash = false, isDot = false, haveCog = false;
    double ang = 0.0;
    if (gps.location.isValid()) {
        projToPx(p, gps.location.lat(), gps.location.lng(), px, py);
        const int cx = MAP_X + MAP_W / 2, cy = WIRE_TOP + (WIRE_Y1 - WIRE_TOP) / 2;
        bx = px; by = py;
        isDot = clampToWire(cx, cy, bx, by);
        haveBoat = true;
        double dLat = 0.0, dLon = 0.0;
        char tag[12] = {0};
        if (nextDestination(gps, dLat, dLon, tag, sizeof(tag))) {
            projToPx(p, dLat, dLon, dx, dy);
            haveDash = true;
            // Short fixed stub (~1 cm) toward the checkpoint, not the whole leg.
            const double vx = dx - bx, vy = dy - by;
            const double len = hypot(vx, vy);
            const int stub = STUB_DEST_PX;
            if (len > stub) {
                ex = (int)(bx + vx / len * stub);
                ey = (int)(by + vy / len * stub);
            } else {
                ex = dx; ey = dy;
            }
            // The stub must not cross the window border either (todo 36): the
            // target can sit outside the rect in Focus/heading-up, and this is
            // the only line that starts at the boat rather than at a mark.
            clampToWire(bx, by, ex, ey);
        }
        ang = gps.course.isValid() ? (gps.course.deg() - rotEff) : 0.0;
        // Second stub (todo 39): where the boat is ACTUALLY going. Same length
        // and dash pattern as the yellow destination stub, in cyan, so the pair
        // reads "steering this way / need to go that way" at a glance.
        if (gps.course.isValid() && gps.speed.isValid() &&
            gps.speed.knots() >= COG_MIN_KN) {
            const double a = ang * M_PI / 180.0;
            cogX = (int)(bx + sin(a) * STUB_PX);
            cogY = (int)(by - cos(a) * STUB_PX);
            // The COG points anywhere — up into the header band included, which
            // the destination stub can never do (its target is inside the fit
            // box). Shorten it at the race window's border (todo 37's rect),
            // then hard-clamp as a net for the boat-sitting-on-the-border case,
            // where the ray leaves at t == 0 and clampToWire declines to act.
            clampToWire(bx, by, cogX, cogY);
            if (cogX < MAP_X + 5) cogX = MAP_X + 5;
            else if (cogX > MAP_X + MAP_W - 5) cogX = MAP_X + MAP_W - 5;
            if (cogY < WIRE_TOP + 5) cogY = WIRE_TOP + 5;
            else if (cogY > WIRE_Y1 - 5) cogY = WIRE_Y1 - 5;
            haveCog = true;
        }
    }
    if (!dynOk) {
        // Fresh draw, nothing to erase.
    } else if (haveBoat == oldHaveBoat && (!haveBoat ||
               (bx == oldBx && by == oldBy && ang == oldAng && isDot == oldWasDot &&
                haveDash == oldHaveDash && (!haveDash ||
                 (ex == oldDash[0] && ey == oldDash[1])) &&
                haveCog == oldHaveCog && (!haveCog ||
                 (cogX == oldCog[0] && cogY == oldCog[1]))))) {
        return; // nothing moved
    } else {
        // Erase old dash + old boat, then restore identical static pixels.
        if (oldHaveDash) {
            drawDashed(oldBx, oldBy, oldDash[0], oldDash[1], RBG);
        }
        if (oldHaveCog) {
            drawDashed(oldBx, oldBy, oldCog[0], oldCog[1], RBG);
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
        drawDashed(bx, by, ex, ey, RBOAT);
    }
    if (haveCog) {
        drawDashed(bx, by, cogX, cogY, RCOG);
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
    oldDash[0] = ex; oldDash[1] = ey; oldDash[2] = 0; oldDash[3] = 0;
    oldCog[0] = cogX; oldCog[1] = cogY; oldHaveCog = haveCog;
    dynOk = true;
}

static int fontPxH(uint8_t font)
{
    // Classic TFT_eSPI fonts used here: 2 = 16px, 4 = 26px, 6 = 48px (the
    // chr_hgt_f* heights in TFT_eSPI's Font16/32/64 tables).
    if (font == 6) return 48;
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

    // Erase only the CHANGING SUFFIX, not the whole string.
    //
    // The panel scans out continuously, so blanking the full old string
    // and redrawing it shows up as a dark flash every time the value
    // changes — and the gun countdown changes once a second, so the
    // header blinked throughout the final minute. The prefix is almost
    // always shared ("GUN 0:4" then only "0"→"9"), so erasing from the
    // first differing glyph leaves most of the text lit and the flash
    // confined to a couple of characters.
    int ex = x, ey = y;
    if (datum == TC_DATUM) ex = x - lastW / 2;
    else if (datum == MC_DATUM) { ex = x - lastW / 2; ey = y - h / 2; }
    else if (datum == TR_DATUM) ex = x - lastW;
    else if (datum == BL_DATUM) ey = y - h;
    else if (datum == BR_DATUM) { ex = x - lastW; ey = y - h; }

    if (lastW > 0) {
        size_t keep = 0;
        while (last[keep] && txt[keep] && last[keep] == txt[keep]) keep++;
        // A CENTRED string also shifts sideways when its length changes, and
        // the suffix erase cannot reach the strip that leaves behind on the old
        // LEFT edge: the countdown going "GUN 1'00''" -> "GUN 59''" is two
        // glyphs shorter, so the new text starts ~14px right of the old one and
        // the old "G" stayed standing to the left of it. Blank that strip too;
        // it is exactly the shift, so it runs only on a length change (once or
        // twice per countdown), never once a second.
        if (datum == TC_DATUM || datum == MC_DATUM) {
            const int newW = tft.textWidth(txt, font);
            if (newW < lastW && ex < x - newW / 2)
                tft.fillRect(ex, ey, (x - newW / 2) - ex, h, RBG);
        }
        // textWidth() of the shared prefix, so the erase starts exactly
        // where the first differing glyph begins.
        const int prefixW = tft.textWidth(String(last).substring(0, keep).c_str(), font);
        int eraseW = lastW - prefixW;
        if (eraseW > 0) tft.fillRect(ex + prefixW, ey, eraseW, h, RBG);
    }
    tft.setTextColor(color, RBG);
    lastW = tft.drawString(txt, x, y, font);
    strncpy(last, txt, lastLen - 1);
    last[lastLen - 1] = '\0';
}

// Race-screen readouts (DST / SPD / TIME), one CENTRED value per cell of the
// header grid. This is drawSmart WITHOUT the identical-string skip, on purpose:
// the readouts are cheap and a cached "already drawn" alone would hide one
// until its value next changed. Redrawing every pass is what the old opaque
// overlay did anyway.
//
// What it adds is the ERASE. An opaque drawString erases exactly the NEW
// string's box, so a box that shrinks in PIXELS leaves the old edge standing as
// ghost pixels (in font 4 the digits are tabular, advance 14, so the shift is
// real pixels). The old edge-anchored layout could blank only the changing
// SUFFIX because a left/right datum keeps the other edge still; a centred box
// moves on BOTH sides, so shortening "15.7 Kn" to "9.9 Kn" would shift the left
// edge right by ~20px and leave the first glyphs of the old value behind. Clear
// the UNION of the old and new boxes instead: it never leaves the cell (the
// widest value is ~90px in a 106px cell) and it only runs when the string
// changes, which is the same rule the suffix eraser followed.
static void drawReadoutC(int x, int y, uint8_t font, const char* txt,
                         char* last, size_t lastLen, int16_t& lastW)
{
    tft.setTextDatum(MC_DATUM);
    const int h = fontPxH(font);
    if (lastW > 0 && strcmp(txt, last) != 0) {
        const int newW = tft.textWidth(txt, font);
        const int w = (lastW > newW ? lastW : newW) + 2;
        tft.fillRect(x - w / 2, y - h / 2, w, h, RBG);
    }
    tft.setTextColor(RFG, RBG);
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
static char pNxt[12] = {0}; static int16_t pNxtW = 0;
static char pTim[10] = {0}; static int16_t pTimW = 0;
static char pLabW[8] = {0}; static int16_t pLabWW = 0;
static char pWnd[8] = {0}; static int16_t pWndW = 0;
static bool paneClean = false;
// Hint bar texts (lengths vary by mode: repaint band on change).
static char lastHintL[16] = {0};
static char lastHintR[16] = {0};
// What the bottom band is currently showing: 0 nothing/unknown, 1 hints,
// 2 banner. A change of MODE clears the band; a change of banner TEXT does
// not (drawSmart erases the suffix only).
static uint8_t lastBandMode = 0;

// Last GPS epoch seen on this page (buttons have no gps handle).
static long lastGpsNow = 0;
// Last fix for course placement (Repeat/Start need position + freshness).
static double lastGpsLat = 0.0, lastGpsLon = 0.0;
static int lastGpsCourse = -1;
static unsigned long lastFixAt = 0;
// 8s committee/gun signal banner lifetime (declared early so showTransient
// can check it; the msg buffer is defined with showSignal below).
static unsigned long sigUntil = 0;
// Transient header message (duration cycling), 2s.
static char transientMsg[12] = {0};
static unsigned long transientUntil = 0;
static void showTransient(const char* msg)
{
    // An active 8s committee/gun signal always outranks a 2s transient
    // banner (NO FIX/OFFLINE/WRONG). Do not let one overwrite it.
    if (sigUntil > millis()) return;
    strncpy(transientMsg, msg, sizeof(transientMsg) - 1);
    transientMsg[sizeof(transientMsg) - 1] = '\0';
    transientUntil = millis() + 2000;
}
// Committee-signal banner, 8s (outranks the transient in the header).
static char sigMsg[12] = {0};
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
// Course browse state (practice setup; map frozen while open). courseView:
// 0 = course list, 1 = session options.
static bool courseOpen = false;
static uint8_t courseView = 0;
static uint8_t courseSel = 0;
static bool coursePreselected = false; // picker already jumped to the default
static bool courseDirty = false;
static bool courseAsked = false;
static bool courseWasReady = false;
static unsigned long courseT0 = 0;

static void resetRaceText()
{
    // Called on full clears so change-detect redraws everything next pass.
    pBan[0] = 0; pBanW = 0;
    // The header readouts are wiped by the same clears, and they are
    // change-detected caches too: leave a stale extent here and the next
    // erase would blank a box that no longer exists.
    ovSpd[0] = 0; ovSpdW = 0;
    ovDst[0] = 0; ovDstW = 0;
    ovTim[0] = 0; ovTimW = 0;
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
    lastBandMode = 0;
    pBan[0] = 0; pBanW = 0;
    paneClean = false;
}

// Live fix snapshot used for course placement (Start/Repeat). Sampled on
// EVERY pass, overlays included: drawRaceText is skipped while the menu or the
// picker is open, and a snapshot frozen when the overlay opened made "is my
// fix fresh?" fail after 15s of staring at the menu.
static void raceSampleFix(TinyGPSPlus& gps)
{
    if (!gps.location.isValid()) return;
    lastGpsLat = gps.location.lat();
    lastGpsLon = gps.location.lng();
    // Boat bearing: the fallback wind source when no station reports (the boat
    // sits in the no-go angle, so its heading points at where the wind comes
    // FROM).
    lastGpsCourse = gps.course.isValid() ? (int)(gps.course.deg() + 0.5) % 360 : -1;
    lastFixAt = millis();
}

// Race-screen clock notation: sailing minutes and seconds — `'` and `''` —
// instead of the `:` separator, e.g. "+2'21''". Under a minute the minute part
// is dropped but the seconds stay marked: "-46''". The sign is part of the
// string, so the TIME cell is a signed race clock: negative to the gun,
// positive after it.
static void fmtRaceClock(char* out, size_t n, char sign, long secs)
{
    if (secs < 0) secs = 0;
    char sgn[2] = {sign, 0};
    const long m = secs / 60;
    const long s = secs % 60;
    if (m > 0) snprintf(out, n, "%s%ld'%02ld''", sgn, m, s);
    else snprintf(out, n, "%s%ld''", sgn, secs);
}

static void drawRaceText(TinyGPSPlus& gps)
{
    // Wall clock (GPS-calibrated, ticks through fix gaps) so the countdown
    // never freezes when fixes pause; raw GPS epoch as the cold fallback.
    long now = raceWallEpoch();
    if (now <= 0) now = raceGpsEpoch(gps);
    lastGpsNow = now;
    const long gun = raceGunEpoch();

    // Snapshot the finish on its edge (todo 38) — see the state block above.
    // The run is the only thing that knows the result, and it is reset when the
    // session goes away, so this has to happen BEFORE that.
    if (raceRunFinished() && raceRunEndEpoch() > 0 && raceRunStartEpoch() > 0) {
        if (!finHold) {
            finHold = true;
            finHoldSession = raceSession.valid ? raceSession.sessionId : -1;
            finHoldElapsed = raceRunEndEpoch() - raceRunStartEpoch();
        }
    } else if (finHold && raceRunStarted()) {
        finHold = false; // a new run is under way: the last result is history
    } else if (finHold && raceSession.valid && finHoldSession >= 0 &&
               raceSession.sessionId != finHoldSession) {
        finHold = false; // a different session arrived: new race, new picture
    }

    // Banner precedence: committee signal (8s) > held FINISHED > countdown >
    // OCS > 2s transient. It renders in the bottom band (todo 50).
    char ban[12];
    // The held finish is the resting state after the line, but an 8s committee
    // signal still interrupts it — a DSQ/SCP has to be seen — and FINISHED
    // comes back on its own when the signal expires.
    const bool finBanner = finHold && !(millis() < sigUntil && sigMsg[0]);
    if (millis() < sigUntil && sigMsg[0]) {
        snprintf(ban, sizeof(ban), "%-8.8s", sigMsg);
    } else if (finBanner) {
        // A finished race outranks the countdown/OCS/transients: the gun is
        // gone, and this is the line the sailor is looking at.
        snprintf(ban, sizeof(ban), "FINISHED ");
    } else if (raceSession.valid && gun > 0 && now > 0 && now < gun && gun - now <= 60000) {
        // Countdown takes the banner from one minute out (below signals,
        // above transients): the eyes are on the band as the gun approaches.
        // Sailing notation throughout (fmtRaceClock), and ONE MINUTE OR LESS
        // DROPS THE MINUTE PART ("GUN 46''"): the last minute is the part read
        // tick by tick, and a zero minute is a character nobody counts. The
        // TIME cell follows the same two rules.
        const long left = gun - now;
        // `left` is SECONDS: divide by 60, not 60000. (Scaling by 1000
        // here is a silent trap — it made every countdown render as
        // "GUN 0:00" while the TIME cell, which divides by 60,
        // showed the right value, so the two disagreed on screen.)
        // Same notation as the TIME cell, minus the sign (a countdown is never
        // "elapsed"): "GUN 1'30''", and bare seconds in the last minute —
        // "GUN 46''".
        char body[12];
        fmtRaceClock(body, sizeof(body), '\0', left);
        snprintf(ban, sizeof(ban), "GUN %s", body);
    } else if (raceSession.valid && gun > 0 && now >= gun && !raceRunStarted() && raceRunOcs()) {
        // OCS lives on the BANNER now, so the TIME cell can keep running
        // (elapsed since the gun) instead of being overwritten with a
        // word. State-based, not edge-based: the boat stays OCS until it
        // re-crosses cleanly, and the banner should keep saying so.
        snprintf(ban, sizeof(ban), "OCS");
    } else if (millis() < transientUntil && transientMsg[0]) {
        snprintf(ban, sizeof(ban), "%-8.8s", transientMsg);
    } else {
        ban[0] = '\0';
    }
    // The banner is no longer drawn here: it owns the bottom band, together
    // with the hints (see the band block at the end of this function).

    // The view states live in the hint bar (and now share the bottom band with
    // the banner, todo 50). Nothing but the map writes inside the map rect.

    // Bad-run watchdog tag (race_run.cpp): the engine stopped the run, and
    // the sailor has to SEE that rather than wonder why the numbers stopped
    // moving. Red, bottom-left of the map, redrawn only on change or on a map
    // repaint, so it never shimmers. Nothing clears it: the only way out of a
    // frozen run is a reset, which repaints the whole page anyway.
    if (raceRunBadRun()) {
        static int badTagLast = -1;
        if (badTagLast != 1 || mapRepainted) {
            badTagLast = 1;
            const uint8_t prevDatum = tft.getTextDatum();
            const int bby = WIRE_Y1 - 20;
            tft.fillRect(4, bby - 2, 76, 20, TFT_RED);
            tft.setTextDatum(TL_DATUM);
            tft.setTextColor(TFT_WHITE, TFT_RED);
            tft.drawString("BAD RUN", 8, bby, 2);
            tft.setTextColor(TFT_WHITE);
            tft.setTextDatum(prevDatum);
        }
    }

    // Destination geometry (map boat/dash + pane cells).
    char tag[12];
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
    // Compact race clock for the TIME cell: sailing notation (fmtRaceClock),
    // SIGNED ONLY WHILE COUNTING DOWN: negative to the gun, and unsigned once
    // the race is running (a "+" on every elapsed second was pure noise).
    char tim[14];
    if (finHold) {
        // The held result (todo 38): frozen at the finish, and it survives the
        // session being dropped by the backend — which is the whole point.
        fmtRaceClock(tim, sizeof(tim), '\0', finHoldElapsed);
    } else if (raceSession.valid && gun > 0 && now > 0) {
        if (now < gun) {
            // Negative until the gun: the countdown reads as "time to go" and
            // the same cell flips to "0'11''" past the gun without changing
            // shape. Under a minute it is seconds only ("-46''") — see
            // fmtRaceClock.
            fmtRaceClock(tim, sizeof(tim), '-', gun - now);
        } else if (!raceRunStarted()) {
            // Past the gun but not away cleanly. Keep the clock running
            // from the gun — OCS is announced on the banner, so this cell
            // stays a time (it used to read "OCS"/"GO" and went blank as
            // a clock while the boat was still OCS).
            fmtRaceClock(tim, sizeof(tim), '\0', now - gun);
        } else {
            const long base = raceRunFinished() && raceRunEndEpoch() > 0 ? raceRunEndEpoch() : now;
            fmtRaceClock(tim, sizeof(tim), '\0', base - raceRunStartEpoch());
        }
    } else if (raceSession.valid && gun > 0) {
        snprintf(tim, sizeof(tim), "WAIT");
    } else {
        tim[0] = '\0'; // no clock to report: blank, not a placeholder
    }

    // Readouts: ONE row, three equal cells — DST | SPD | TIME — inside the
    // header band, each value centred in its cell (todo 50). Nothing is drawn
    // over the map: the clock's old font-6 box covered ~11% of the fit box, the
    // leeward half of the start line included. All three go through
    // drawReadoutC, not a bare drawString: an opaque draw only erases the new
    // (often narrower) string's box, which is how the leading glyph of the
    // previous value survived as ghost pixels (todo 34).
    //
    // A modal overlay (menu / course picker) stands them down: it paints a box
    // across the header after this runs, so an unconditional redraw would keep
    // punching the clock and the distance back through the overlay's own
    // border. Closing an overlay is a full redraw, which resets these caches.
    if (!courseOpen && !menuOpen) {
        char ov[16];
        // Nothing to report means nothing drawn — distance, speed and clock
        // alike: the " --.- Kn"/"--- m"/"--:--" placeholders read as values at
        // a glance from a helm. An empty string makes the readout blank its
        // whole previous box, which is the only correct way to remove one.
        if (showBrg && !finHold) snprintf(ov, sizeof(ov), "%ld m", distM);
        else ov[0] = '\0'; // finished: the distance back to the line is noise
        drawReadoutC(CELL_CX[0], HDR_CY, 4, ov, ovDst, sizeof(ovDst), ovDstW);
        if (gps.speed.isValid()) snprintf(ov, sizeof(ov), "%4.1f Kn", gps.speed.knots());
        else ov[0] = '\0'; // no fix yet: blank, same rule as distance/clock
        drawReadoutC(CELL_CX[1], HDR_CY, 4, ov, ovSpd, sizeof(ovSpd), ovSpdW);
        drawReadoutC(CELL_CX[2], HDR_CY, 4, tim, ovTim, sizeof(ovTim), ovTimW);
    }

    // Bottom band: the banner OR the hints, never both (todo 50). Switching
    // between them clears the band once and resets both caches — everything in
    // here is change-detected, so a clear without a cache reset would leave the
    // band black for ever.
    //
    // A banner change does NOT clear: the countdown re-renders once a second,
    // and a full-band blank every second would flash the way the header
    // countdown used to (see drawSmart), so the banner keeps its suffix erase.
    const bool haveBanner = ban[0] != '\0';
    const uint8_t bandMode = haveBanner ? 2 : 1;
    char hintL[16], hintR[16];
    if (courseOpen && courseView == 1) {
        snprintf(hintL, sizeof(hintL), "L Row  LL Back");
        snprintf(hintR, sizeof(hintR), "R Set  RR Start");
    } else if (courseOpen || menuOpen) {
        snprintf(hintL, sizeof(hintL), "L Back");
        snprintf(hintR, sizeof(hintR), "R Sel RR Pick");
    } else {
        snprintf(hintL, sizeof(hintL), "L Next  LL Menu");
        const char* viewName = focusMode == 0 ? "Map" : focusMode == 1 ? "Tgt" : "Auto";
        const char* bearName = viewMode == 0 ? "N" : viewMode == 1 ? "BRG" : "Wide";
        snprintf(hintR, sizeof(hintR), "R %s RR %s", viewName, bearName);
    }
    if (bandMode != lastBandMode ||
        (!haveBanner && (strcmp(hintL, lastHintL) != 0 || strcmp(hintR, lastHintR) != 0))) {
        tft.fillRect(0, BAND_Y, MAP_W, BAND_H, RBG);
        lastBandMode = bandMode;
        pBan[0] = 0; pBanW = 0; // the band is black: force the banner back
        lastHintL[0] = 0; lastHintR[0] = 0;
    }
    if (haveBanner) {
        // Centred, one font-4 line: the band is 26px, exactly the glyph height.
        drawSmart(MAP_W / 2, BAND_BANNER_Y, 4, TC_DATUM,
                  finBanner ? TFT_GREEN : RFG, ban, pBan, sizeof(pBan), pBanW);
    } else {
        tft.setTextColor(RDIM, RBG);
        tft.setTextDatum(BL_DATUM);
        tft.drawString(hintL, 8, BAND_HINT_Y, 2);
        tft.setTextDatum(BR_DATUM);
        tft.drawString(hintR, tft.width() - 8, BAND_HINT_Y, 2);
        strncpy(lastHintL, hintL, sizeof(lastHintL) - 1);
        strncpy(lastHintR, hintR, sizeof(lastHintR) - 1);
    }
}

// LL menu: explicit race actions (map frozen while open, box repainted on
// open/selection only; close does a full repaint). The rows depend on
// what is going on — you cannot start over a running session, cannot
// repeat a committee race, cannot abandon nothing.
enum MenuRow : uint8_t {
    MenuStart = 0,  // start a practice session (no session running)
    MenuRepeat,     // re-create the last practice session at the boat
    MenuResync,     // pull the committee session now (race mode)
    MenuAutoDist,   // cycle auto focus switch distance (10/50/100/200 m)
    MenuAbandon,    // end the running session
};

static MenuRow menuRows[5];
static char menuAutoDistLabel[24] = {0};

static uint8_t buildMenuRows()
 {
    autoDistEnsure();
    const bool has = raceSession.valid;
    const bool race = has && strcmp(raceSession.mode, "race") == 0;
    uint8_t n = 0;
    if (!has) menuRows[n++] = MenuStart;
    if (!race && practicePrefsValid()) menuRows[n++] = MenuRepeat;
    if (race) menuRows[n++] = MenuResync;
    menuRows[n++] = MenuAutoDist;   // always adjustable
    if (has) menuRows[n++] = MenuAbandon;
    snprintf(menuAutoDistLabel, sizeof(menuAutoDistLabel), "Auto distance %dm", autoDistM);
    return n;
}

static const char* menuRowText(MenuRow r)
{
    switch (r) {
        case MenuStart: return "Start practice session";
        case MenuRepeat: return "Repeat last session";
        case MenuResync: return "Resync now";
        case MenuAutoDist: return menuAutoDistLabel;
        case MenuAbandon: return "Abandon session";
    }
    return "";
}

static void drawRaceMenu(bool full)
{
    // CONFIG is the menu reference: centered title + rule, `>` rows with
    // yellow selection, hint bar with rule. Same geometry, same grays.
    if (full) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("RACE/PRACTICE MENU", tft.width() / 2, 20, 2);
        tft.drawFastHLine(0, 44, tft.width(), MENU_GRAY);
    } else {
        tft.fillRect(0, 60, tft.width(), 70, TFT_BLACK);
    }
    char row[40];
    for (uint8_t i = 0; i < menuN; i++) {
        snprintf(row, sizeof(row), "%s %s", i == menuSel ? ">" : " ", menuRowText(menuRows[i]));
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

static void courseClose()
{
    courseOpen = false;
    courseView = 0;
    redrawCurrentPage();
}

// ---- Session options (right after picking a course) ----------------------
// Two rows; R cycles the highlighted row's value, RR starts the session.
static uint8_t optRow = 0;

static void gunCycle(int dir)
{
    const unsigned n = sizeof(PRACTICE_GUNS) / sizeof(PRACTICE_GUNS[0]);
    int cur = 0;
    while (cur < (int)n && PRACTICE_GUNS[cur] != practicePrefs().gunSec) cur++;
    if (cur >= (int)n) cur = 1; // 30s
    cur = (cur + (dir > 0 ? 1 : n - 1)) % (int)n;
    practicePrefsSetGun(PRACTICE_GUNS[cur]);
}

static void distCycle(int dir)
{
    const unsigned n = sizeof(PRACTICE_DISTS) / sizeof(PRACTICE_DISTS[0]);
    int cur = 0;
    while (cur < (int)n && PRACTICE_DISTS[cur] != practicePrefs().distM) cur++;
    if (cur >= (int)n) cur = 1; // 20m
    cur = (cur + (dir > 0 ? 1 : n - 1)) % (int)n;
    practicePrefsSetDist(PRACTICE_DISTS[cur]);
}

static void drawCourseOptions()
{
    const int bx = 8, bw = 304, by = 34, bh = 172;
    tft.fillRect(bx, by, bw, bh, RBG);
    tft.drawRect(bx, by, bw, bh, RFG);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(RFG, RBG);
    char head[20];
    const Course* c = courseGet(courseSel);
    snprintf(head, sizeof(head), "%s", c && c->name[0] ? c->name : "OPTIONS");
    tft.drawString(head, bx + bw / 2, by + 5, 2);
    tft.drawFastHLine(bx + 8, by + 26, bw - 16, RDIM);

    const int g = practicePrefs().gunSec;
    const int d = practicePrefs().distM;
    char rowG[28], rowD[28];
    snprintf(rowG, sizeof(rowG), "%s Gun      %d:%02d", optRow == 0 ? ">" : " ", g / 60, g % 60);
    snprintf(rowD, sizeof(rowD), "%s Distance %3d m", optRow == 1 ? ">" : " ", d);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(optRow == 0 ? TFT_YELLOW : RFG, RBG);
    tft.drawString(rowG, bx + 12, by + 46, 2);
    tft.setTextColor(optRow == 1 ? TFT_YELLOW : RFG, RBG);
    tft.drawString(rowD, bx + 12, by + 76, 2);

    tft.setTextColor(RDIM, RBG);
    tft.drawString("R change   RR start", bx + 12, by + 116, 2);
    tft.drawString("course placed upwind of you", bx + 12, by + 140, 2);
}

// Course browse list (below header; map frozen behind it). Repainted on
// open/selection/fetch-ready only.
static void drawCourseList()
{
    const int bx = 8, bw = 304, by = 34, bh = 172;
    tft.fillRect(bx, by, bw, bh, RBG);
    tft.drawRect(bx, by, bw, bh, RFG);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(RDIM, RBG);
    if (!courseReady()) {
        tft.drawString("COURSES ...", bx + 8, by + 5, 2);
        tft.setTextColor(RFG, RBG);
        tft.drawString("asking backend", bx + 8, by + 27, 2);
        return;
    }
    const uint8_t n = courseCount();
    char head[20];
    snprintf(head, sizeof(head), "COURSES %d", n);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(head, bx + bw / 2, by + 5, 2);
    tft.drawFastHLine(bx + 8, by + 26, bw - 16, RDIM);
    if (!n) {
        tft.setTextColor(RFG, RBG);
        tft.setTextDatum(TL_DATUM);
        tft.drawString("none saved", bx + 8, by + 30, 2);
        return;
    }
    if (courseSel >= n) courseSel = 0;
    uint8_t top = courseSel > 2 ? courseSel - 2 : 0;
    if (top + 6 > n && n > 6) top = n - 6;
    for (uint8_t r = 0; r < 6 && top + r < n; r++) {
        const Course* t = courseGet(top + r);
        if (!t) continue;
        const int ry = by + 30 + r * 22;
        const bool sel = (top + r) == courseSel;
        char nm[36];
        snprintf(nm, sizeof(nm), "%s %d %s", sel ? ">" : " ", top + r + 1, t->name);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(sel ? TFT_YELLOW : RFG, RBG);
        tft.drawString(nm, bx + 12, ry, 2);
    }
    tft.setTextColor(RDIM, RBG);
}

// Open the course picker, pre-selected on the remembered course.
static void openCoursePicker()
{
    menuOpen = false;
    courseOpen = true;
    courseView = 0;
    courseSel = 0;
    coursePreselected = false;
    if (courseReady()) {
        courseSel = coursePreselectIndex(); // library already in RAM
        coursePreselected = true;
    }
    courseDirty = true;
    courseAsked = false;
    courseWasReady = courseReady();
    redrawCurrentPage();
}

// Start the practice session the user just configured. Needs a fix and the
// clock; the course goes to the backend in the background.
static bool startPickedCourse()
{
    long now = raceWallEpoch();
    if (now <= 0) now = lastGpsNow;
    if (now <= 0 || millis() - lastFixAt > 15000) return false;
    return courseStartSession(courseSel, lastGpsLat, lastGpsLon, now, lastGpsCourse);
}

// Repeat needs the remembered course SHAPE, which lives in the RAM pool —
// wiped by every reboot. So Repeat is a two-phase action: ask the backend for
// the library, then build the session when it lands. One button press, no
// screens in between: the sailor stays on the race screen meanwhile.
static bool pendingRepeat = false;
static unsigned long pendingRepeatAt = 0;

static void repeatCourseLoaded()
{
    pendingRepeat = false;
    long now = raceWallEpoch();
    if (now <= 0) now = lastGpsNow;
    if (now <= 0 || millis() - lastFixAt > 15000) {
        showTransient("NO FIX ");
        return;
    }
    if (!courseRepeatSession(lastGpsLat, lastGpsLon, now, lastGpsCourse)) {
        showTransient("NO COURSE ");
        bufferedSerialPrintln("[MENU] repeat: course missing after fetch");
    }
}

static void menuConfirm()
{
    if (menuSel >= menuN) { menuClose(); return; }
    switch (menuRows[menuSel]) {
        case MenuStart:
            openCoursePicker();
            return;
        case MenuRepeat: {
            // Re-create the remembered practice at the boat, fresh gun.
            long now = raceWallEpoch();
            if (now <= 0) now = lastGpsNow;
            if (!practicePrefsValid()) {
                showTransient("NO COURSE ");
                bufferedSerialPrintln("[MENU] repeat: nothing remembered");
            } else if (now <= 0) {
                showTransient("NO FIX ");
                bufferedSerialPrintln("[MENU] repeat: no wall clock");
            } else if (millis() - lastFixAt > 15000) {
                showTransient("NO FIX ");
                bufferedSerialPrintln("[MENU] repeat: fix stale");
            } else if (!courseHaveId(practicePrefs().courseId)) {
                // Shape not in RAM (first repeat since boot): fetch, then run.
                pendingRepeat = true;
                pendingRepeatAt = millis();
                backendFetchCourses();
                menuClose();
                showTransient("LOADING ");
                bufferedSerialPrintln("[MENU] repeat: fetching course library");
            } else if (courseRepeatSession(lastGpsLat, lastGpsLon, now, lastGpsCourse)) {
                menuClose();
            } else {
                showTransient("NO COURSE ");
                bufferedSerialPrintln("[MENU] repeat: start refused");
            }
            break;
        }
        case MenuResync:
            backendPollHealthNow();
            break;
        case MenuAutoDist: {
            int nv = autoDistNext();
            snprintf(menuAutoDistLabel, sizeof(menuAutoDistLabel),
                     "Auto distance %dm", nv);
            showTransient("AUTO DIST");
            menuDirty = true;
            return; // stay in menu, like config rows
        }
        case MenuAbandon:
            courseAbandonSession();
            showSignal("ABANDON ");
            break;
    }
    menuClose();
}

void drawScreenRace(TinyGPSPlus &gps, bool requiresInit)
{
    raceSampleFix(gps);
    autoDistEnsure();
    viewModeEnsure();
    if (requiresInit) {
        tft.fillScreen(RBG);
        mapDirty = true; // force a full map redraw after the clear
        resetRaceText();
        // A REPAINT MUST NEVER RESET THE RUN. This used to call
        // raceRunReset() whenever !raceRunStarted(), and `started` only
        // becomes true once the START is SCORED — so any redraw before that
        // moment (R, closing a menu, a page switch, the OTA redraw) threw away
        // the pre-start side memory (gunSide/hasGunSide/lastSide/approachSide/
        // ocs). The clean-crossing test needs exactly that memory, so the boat
        // could never score its start and fell through to the 60s "late join"
        // escape: the GO banner then appeared a minute after the gun. Session
        // changes own the reset, right below.
    }
    // courseChanged means the SESSION changed under us — that is what closes
    // an open overlay. It must NOT be triggered by a forced repaint: a full
    // redraw happens whenever an overlay opens, and treating that as a course
    // change closed the menu/picker on the very pass it was opened.
    const int key = raceCourseKey();
    const bool courseChanged = (key != lastRaceCourseKey);
    lastRaceCourseKey = key;
    const bool mapFull = mapDirty || courseChanged;
    mapDirty = false;
    if (courseChanged && key != lastResetCourseKey && !gGrabbing) {
        lastResetCourseKey = key;
        raceRunReset();
        // A course change can follow an overlay or the empty state that wiped
        // the header without resetting its caches, so force the text repaint.
        //
        // resetRaceText() forgets the boxes it describes, so the pixels must go
        // with it: the map's own full repaint only covers WIRE_TOP downwards,
        // so without this clear the next banner/readout draw lands with no
        // erase at all and leaves the tail of the previous string standing.
        // That is the panel artifact where "ABANDON " was followed by "GO" and
        // read as "GOANDON" - the cached extent was forgotten, the ink was not.
        tft.fillRect(0, PANE_TOP, MAP_W, WIRE_TOP - PANE_TOP, RBG);
        resetRaceText();
        drawFrameChrome(); // the clear took the header grid rules with it
    }
    raceRunUpdate(gps);
    raceSignalsApply(gps);
    // Wrong-side calls surface as a short header banner.
    if (raceWrongPoll()) {
        showTransient("WRONG!  ");
    }
    // Pre-gun cues: the one-minute warning and the 10..1 ticks. Both are
    // edge-driven and consumed in race_run, and both sit ABOVE the
    // menu/course early-returns below so they still sound with a menu open.
    if (!gGrabbing) {
        if (raceGunMinuteCue()) soundGunMinute();
        const int tick = raceGunTickCue();
        if (tick >= 0) soundGunTick(tick);
    }
    // Gun edge banner: GO, or OCS when over early — once per start, never
    // re-fired by reconnects (edge consumed above); skipped during grabs.
    // The signal outranks any concurrent 2s transient (NO FIX/WRONG/etc.).
    if (!gGrabbing) {
        bool edgeOcs = false;
        if (raceGunEdge(&edgeOcs)) {
            showSignal(edgeOcs ? "OCS" : "GO");
            transientUntil = 0;    // do not let a concurrent transient override
            soundGun(edgeOcs);
        }
    }
    if (courseChanged && (menuOpen || courseOpen) && !gGrabbing) {
        // A full-screen menu / course list sat over the page; force a clean
        // repaint so its pixels (header title, pane grid/labels) don't ghost.
        menuOpen = false;
        courseOpen = false;
        redrawCurrentPage();
        return;
    }
    if (menuOpen) {
        // Full-screen menu: engine already ran above; paint and stop.
        // The open transition always takes the full path (clear + title)
        // so no map ghosts survive behind the rows.
        if (menuFull) {
            drawRaceMenu(true);
            menuFull = false;
            menuDirty = false;
        } else if (menuDirty || mapFull) {
            drawRaceMenu(mapFull);
            menuDirty = false;
        }
        return;
    }
    if (courseOpen) {
        // Course browse: fetch once, fail loud after 8s, freeze the map.
        if (courseView == 0) {
            if (!courseAsked) {
                courseAsked = true;
                courseT0 = millis();
                backendFetchCourses();
            }
            if (courseReady() != courseWasReady) {
                courseWasReady = courseReady();
                // First time the library lands: jump to the remembered
                // course (or the Windward-Leeward default) instead of row 1.
                if (courseWasReady && !coursePreselected) {
                    courseSel = coursePreselectIndex();
                    coursePreselected = true;
                }
                courseDirty = true;
            }
            if (!courseReady() && millis() - courseT0 > 8000) {
                courseOpen = false;
                showTransient("OFFLINE ");
                redrawCurrentPage();
            }
        }
    }
    char tTag[12] = {0};
    if (raceSession.valid) {
        double tLat = 0, tLon = 0;
        if (nextDestination(gps, tLat, tLon, tTag, sizeof(tTag))) {
            // Remember where the zoom was pointed before this pass's target
            // takes over: a pass switches progIdx to the NEXT leg, but the
            // rounding of the mark just taken is not over yet (see the
            // clearance hold below).
            if (focusValid) { prevFocusLat = focusLat; prevFocusLon = focusLon; }
            focusLat = tLat; focusLon = tLon; focusValid = true;
            if (gps.location.isValid()) targetDistM =
                TinyGPSPlus::distanceBetween(gps.location.lat(), gps.location.lng(),
                                             tLat, tLon);
            // TEMPORARY DST-jump diagnostic (user-reported +80 m jumps just
            // before the focus engages): log ONLY anomalous single-eval
            // jumps and target changes — quiet otherwise. Remove once settled.
            {
                static double lastDstShown = -1.0;
                static char lastDstTag[12] = {0};
                if (targetDistM >= 0) {
                    if (lastDstTag[0] != '\0' && strcmp(tTag, lastDstTag) != 0) {
                        char jmsg[128];
                        snprintf(jmsg, sizeof(jmsg),
                                 "[RACE] dst new target %s %.0f (was %s %.0f)",
                                 tTag, targetDistM, lastDstTag, lastDstShown);
                        bufferedSerialPrintln(jmsg);
                    } else if (lastDstShown >= 0 && targetDistM - lastDstShown > 30.0) {
                        char jmsg[160];
                        snprintf(jmsg, sizeof(jmsg),
                                 "[RACE] dst JUMP %s %.0f -> %.0f f=%.5f,%.5f t=%.5f,%.5f",
                                 tTag, lastDstShown, targetDistM,
                                 gps.location.lat(), gps.location.lng(), tLat, tLon);
                        bufferedSerialPrintln(jmsg);
                    }
                    lastDstShown = targetDistM;
                    strncpy(lastDstTag, tTag, sizeof(lastDstTag) - 1);
                    lastDstTag[sizeof(lastDstTag) - 1] = '\0';
                } else {
                    lastDstShown = -1.0;
                    lastDstTag[0] = '\0';
                }
            }
        } else {
            focusValid = false; targetDistM = -1.0; tTag[0] = '\0';
        }
    } else {
        focusValid = false; targetDistM = -1.0;
    }
    autoDistEnsure();
    if (focusMode == 1) focusEnabled = true;
    else if (focusMode == 2 && focusValid) {
        if (autoHoldTag[0] == '\0' || strcmp(autoHoldTag, tTag) != 0) {
            if (targetDistM >= 0 && targetDistM <= (double)autoDistM) {
                strcpy(autoHoldTag, tTag); // engage
                focusEnabled = true;
            } else {
                autoHoldTag[0] = '\0';
                focusEnabled = false;
            }
        } else {
            focusEnabled = true;
        }
    } else {
        focusEnabled = false; autoHoldTag[0] = '\0';
    }
    if (focusMode == 0) autoHoldTag[0] = '\0';
    // A scored switch does NOT leave the zoom any more: the engine advances
    // progIdx at the clear-route event, which for a rounding is still at the
    // buoy (on the last mark that cut the view off mid-turn). Pin the zoom to
    // the point that was being rounded and release it only once the boat is
    // clear of that mark; then the overview returns and the new leg (the
    // finish line included) re-engages by distance as usual.
    static uint8_t lastFocusProg = 255;
    const uint8_t nowProg = raceProgIdx();
    if (lastFocusProg != nowProg) {
        const uint8_t prevProg = lastFocusProg;
        lastFocusProg = nowProg;
        if (focusMode == 2 && nowProg > prevProg && prevProg < raceSession.markCount) {
            if (prevZoomOn) {
                autoClearHold = true;
                autoClearLat = prevFocusLat;
                autoClearLon = prevFocusLon;
                double cr = (double)raceSession.marks[prevProg].r + AUTO_CLEAR_MARGIN_M;
                if (cr < AUTO_CLEAR_MIN_M) cr = AUTO_CLEAR_MIN_M;
                if (cr > AUTO_CLEAR_MAX_M) cr = AUTO_CLEAR_MAX_M;
                autoClearR = cr;
                char cmsg[96];
                snprintf(cmsg, sizeof(cmsg),
                         "[RACE] auto zoom held on mark %u until clear (%.0fm)",
                         (unsigned)(prevProg + 1), autoClearR);
                bufferedSerialPrintln(cmsg);
            } else {
                autoClearHold = false;
                autoHoldTag[0] = '\0';
            }
        } else if (focusMode == 2) {
            focusEnabled = false;
            autoHoldTag[0] = '\0';
            autoClearHold = false;
        }
    }
    if (focusMode != 2) autoClearHold = false;
    if (autoClearHold) {
        if (autoHoldTag[0] != '\0') {
            // The NEW target already qualified by distance (legs shorter than
            // the threshold, e.g. a scaled test course): no reason to hold.
            autoClearHold = false;
        } else {
            double d = -1.0;
            if (gps.location.isValid()) d = TinyGPSPlus::distanceBetween(
                gps.location.lat(), gps.location.lng(), autoClearLat, autoClearLon);
            if (d >= 0.0 && d <= autoClearR) {
                focusLat = autoClearLat; focusLon = autoClearLon;
                focusValid = true;
                focusEnabled = true;
            } else {
                autoClearHold = false; // rounded and gone: back to the overview
                focusEnabled = false;
            }
        }
    }
    focusNext = focusEnabled;
    prevZoomOn = focusEnabled; // what a pass next time must hold on to
    static bool lastFocusEnabled = false;
    const bool focusSwitch = (focusEnabled != lastFocusEnabled);
    lastFocusEnabled = focusEnabled;
    if (focusSwitch) {
        tft.fillRect(MAP_X, WIRE_TOP, MAP_W, WIRE_Y1 - WIRE_TOP, RBG);
        mapDirty = true;
    }
    if (!courseOpen) {
        mapRepainted = false;
        drawRaceMap(gps, mapFull || focusSwitch);
    }
    // Deferred repeat: the library landed (or never did), act on it now.
    if (pendingRepeat) {
        if (courseReady()) {
            repeatCourseLoaded();
        } else if (millis() - pendingRepeatAt > 8000) {
            pendingRepeat = false;
            showTransient("OFFLINE ");
            bufferedSerialPrintln("[MENU] repeat: course library unreachable");
        }
    }
    drawRaceText(gps);
    // The backend answers a device-started create asynchronously: adopt the
    // real session id as soon as it lands (until then the local copy runs).
    long createdId = 0;
    if (backendSessionCreated(&createdId)) {
        if (createdId > 0 && raceSession.valid) {
            raceSession.sessionId = createdId;
            raceSessionSave();
        } else if (createdId < 0) {
            showTransient("OFFLINE ");
        }
    }
    // An abandon that never reached the backend leaves the web thinking the
    // session is still live, with this boat committed to it — say so, loudly
    // enough that the sailor doesn't sail on assuming the web agrees.
    if (backendAbandonFailed()) {
        showSignal("ABANDON ERR");
    }
    if (courseOpen && (courseDirty || mapFull)) {
        if (courseView == 0) drawCourseList();
        else drawCourseOptions();
        courseDirty = false;
    }
}

void screenRaceButton(Button button, ButtonEvent event)
{
    // Course browse owns Short/Long while open (raw Press/Release pass
    // through, same release-after-open reason as the menu).
    if (courseOpen) {
        if (button == Button::Left && event == ButtonEvent::ShortPress) {
            if (courseView == 1) {
                // Options: L moves between the two rows, LL backs out.
                optRow = (uint8_t)((optRow + 1) % 2);
                courseDirty = true;
            } else {
                courseClose();
            }
            return;
        }
        if (button == Button::Left && event == ButtonEvent::LongPress) {
            if (courseView == 1) {
                courseView = 0; // options -> back to the list
                courseDirty = true;
            } else {
                courseClose();
            }
            return;
        }
        if (courseView == 1) {
            // Options: R cycles the highlighted row, RR starts.
            if (button == Button::Right && event == ButtonEvent::ShortPress) {
                if (optRow == 0) gunCycle(1);
                else distCycle(1);
                courseDirty = true;
                return;
            }
            if (button == Button::Right && event == ButtonEvent::LongPress) {
                if (startPickedCourse()) {
                    courseClose(); // courseVersion bump redraws the new course
                } else {
                    showTransient("NO FIX ");
                }
                return;
            }
            return;
        }
        if (button == Button::Right && event == ButtonEvent::ShortPress) {
            const uint8_t n = courseCount();
            if (n) {
                courseSel = (uint8_t)((courseSel + 1) % n);
                courseDirty = true;
            }
            return;
        }
        if (button == Button::Right && event == ButtonEvent::LongPress) {
            if (!courseCount()) {
                showTransient("NO COURSE ");
                return;
            }
            courseView = 1; // pick -> options
            optRow = 0;
            courseDirty = true;
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
    // Left long opens the menu (explicit actions beat hidden gestures).
    if (button == Button::Left && event == ButtonEvent::LongPress) {
        menuOpen = true;
        menuSel = 0;
        menuN = buildMenuRows();
        menuDirty = true;
        menuFull = true;
        return;
    }
    // Right short: cycle map views — overall, next-target, auto (10/50/100/200 m).
    if (button == Button::Right && event == ButtonEvent::ShortPress) {
        focusMode = (focusMode + 1) % 3;
        focusEnabled = (focusMode == 1); // auto upgrades in draw pass
        focusNext = focusEnabled;
        viewModeSave();
        redrawCurrentPage();
        return;
    }
    // Right long: cycle the bearing — north-up, heading-up, wide (todo 48).
    // `redrawCurrentPage()` forces the repaint, so a new orientation shows up
    // immediately instead of waiting for the 1 Hz rotation throttle.
    if (button == Button::Right && event == ButtonEvent::LongPress) {
        viewMode = (uint8_t)((viewMode + 1) % 3);
        redrawCurrentPage();
        return;
    }
}
