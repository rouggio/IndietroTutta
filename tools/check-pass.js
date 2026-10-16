// check-pass.js — mirror of the race_run.cpp pass core (spec mark-passing.md).
// Transcribed predicates, synthetic 1Hz tracks. Exits non-zero on failure.
// Usage: node tools/check-pass.js
'use strict';

const P = { EXC: 150, ARM: 135, TRI: 90, DEAD: 20 };
const R2D = 180 / Math.PI, D2R = Math.PI / 180;
const M = 111320;

const distM = (a, b, c, d) => {
    const s = Math.sin(((c - a) / 2) * D2R) ** 2 +
        Math.cos(a * D2R) * Math.cos(c * D2R) * Math.sin(((d - b) / 2) * D2R) ** 2;
    return 2 * 6371000 * Math.asin(Math.sqrt(s));
};
const brg = (a, b, c, d) => {
    const y = Math.sin((d - b) * D2R) * Math.cos(c * D2R);
    const x = Math.cos(a * D2R) * Math.sin(c * D2R) -
        Math.sin(a * D2R) * Math.cos(c * D2R) * Math.cos((d - b) * D2R);
    return ((Math.atan2(y, x) * R2D) % 360 + 360) % 360;
};
// Local meters -> lat/lon around origin.
const ll = (x, y, o) => [o[0] + y / M, o[1] + x / (M * Math.cos(o[0] * D2R))];
const norm360 = d => ((d % 360) + 360) % 360;
const unwrapNear = (ref, a) => {
    let d = a - ref;
    while (d < -180) d += 360;
    while (d > 180) d -= 360;
    return ref + d;
};
const triAngle = (p, A, B) => {
    let d = Math.abs(brg(p[0], p[1], A[0], A[1]) - brg(p[0], p[1], B[0], B[1]));
    d %= 360;
    return d > 180 ? 360 - d : d;
};
const segXing = (a, b, c, d) => {
    const k = 1; // meter plane already
    const d1x = b[0] - a[0], d1y = b[1] - a[1];
    const d2x = d[0] - c[0], d2y = d[1] - c[1];
    const den = d1x * d2y - d1y * d2x;
    if (Math.abs(den) < 1e-12) return false;
    const t = ((c[0] - a[0]) * d2y - (c[1] - a[1]) * d2x) / den;
    const u = ((c[0] - a[0]) * d1y - (c[1] - a[1]) * d1x) / den;
    return t >= 0 && t <= 1 && u >= 0 && u <= 1;
};
const toXY = (p, o) => [(p[1] - o[1]) * M * Math.cos(o[0] * D2R), (p[0] - o[0]) * M];

// One-leg engine mirror. Target T, next N, side 'P'/'S'/null(gate handled
// separately). Fixes: [{p:[lat,lon], cog}]. Returns event list.
function runLeg(T, N, side, fixes) {
    const ev = [];
    let excMin = 0, excMax = 0, init = false, sm = null;
    let distEMA = -1, minD = -1, armed = false, triHeld = false, prev = null;
    const reset = () => {
        init = false; excMin = excMax = 0; sm = null;
        distEMA = minD = -1; armed = false; triHeld = false; prev = null;
    };
    for (const f of fixes) {
        const raw = norm360(brg(f.p[0], f.p[1], T[0], T[1]));
        if (!init) { sm = raw; excMin = excMax = raw; init = true; }
        else {
            const u = unwrapNear(sm, raw);
            sm = 0.6 * sm + 0.4 * u;
            if (sm < excMin) excMin = sm;
            if (sm > excMax) excMax = sm;
        }
        const exc = excMax - excMin;
        const rd = distM(f.p[0], f.p[1], T[0], T[1]);
        distEMA = distEMA < 0 ? rd : 0.5 * distEMA + 0.5 * rd;
        if (minD < 0 || distEMA < minD) minD = distEMA;
        const tri = triAngle(f.p, T, N);
        if (!armed && exc >= P.ARM) { armed = true; ev.push('ARMED'); }
        if (armed && side && f.cog != null) {
            const rel = norm360(raw - f.cog);
            const dead = rel < P.DEAD || rel > 360 - P.DEAD || (rel > 160 && rel < 200);
            if (!dead) {
                const sailed = rel > 180 ? 'P' : 'S';
                if (sailed !== side) { ev.push('WRONG'); reset(); prev = f.p; continue; }
            }
        }
        if (tri > P.TRI) {
            if (triHeld) { if (exc >= P.EXC) { ev.push('PASS'); break; } }
            else triHeld = true;
        } else {
            triHeld = false;
            if (minD > 0 && distEMA > 2 * minD && tri < 60) { ev.push('RESET'); reset(); prev = f.p; continue; }
        }
        prev = f.p;
    }
    return ev;
}

function runGate(G1, G2, N, fixes) {
    const ev = [];
    let threaded = false, prev = null, triHeld = false;
    const cx = (G1[0] + G2[0]) / 2, cy = (G1[1] + G2[1]) / 2;
    for (const f of fixes) {
        const p = toXY(f.p, [0, 0]), a = toXY(G1, [0, 0]), b = toXY(G2, [0, 0]), n = toXY(N, [0, 0]);
        if (prev && !threaded && segXing(toXY(prev, [0, 0]), p, a, b)) { threaded = true; ev.push('ENTER'); }
        const tri = triAngle(f.p, [cx, cy], N);
        if (tri > P.TRI) triHeld = true; else triHeld = false;
        if (threaded && !segXing(p, n, a, b) && triHeld) { ev.push('PASS'); break; }
        prev = f.p;
    }
    return ev;
}

// Track builder in meters, 1Hz. pts: [x,y][], cog from segments.
function track(O, pts, speed = 5) {
    const fixes = [];
    let trail = [];
    for (const [x, y] of pts) {
        const last = trail.length ? trail[trail.length - 1] : [x - 1, y - 10];
        trail.push([x, y]);
        if (trail.length > 1) {
            const a = trail[trail.length - 2], b = trail[trail.length - 1];
            const segLen = Math.hypot(b[0] - a[0], b[1] - a[1]);
            const steps = Math.max(1, Math.round(segLen / speed));
            for (let s = (fixes.length ? 1 : 0); s <= steps; s++) {
                const ix = a[0] + (b[0] - a[0]) * s / steps;
                const iy = a[1] + (b[1] - a[1]) * s / steps;
                const cog = norm360(Math.atan2(b[0] - a[0], b[1] - a[1]) * R2D);
                fixes.push({ p: ll(ix, iy, O), cog });
            }
        }
    }
    return fixes;
}

let fails = 0;
function check(name, ev, want) {
    const ok = want.every(w => ev.includes(w)) &&
        ev.filter(e => e === 'PASS' || e === 'WRONG').every(e => want.includes(e));
    console.log((ok ? 'ok   ' : 'FAIL ') + name + ' -> [' + ev.join(',') + ']');
    if (!ok) fails++;
}

const O = [44.0, 9.0];
// A. Tacking beat + port rounding: must PASS, never WRONG on the tacks.
{
    const T = ll(0, 1000, O), N = ll(0, 0, O);
    const pts = [[0, -50]];
    for (let i = 0; i < 9; i++) pts.push([i % 2 ? 80 : -80, 100 + i * 90]);
    pts.push([60, 950], [40, 1010], [-30, 1030], [-70, 980], [-60, 900], [-40, 700]);
    check('A tacking beat + port rounding', runLeg(T, N, 'P', track(O, pts)), ['PASS']);
}
// B. Straight close pass (10m): must PASS.
{
    const T = ll(140, 220, O), N = ll(170, 540, O);
    check('B straight close pass', runLeg(T, N, 'P', track(O, [[140, -50], [150, 220], [160, 540]])), ['PASS']);
}
// C. Wide flyby correct side (150m): must NOT pass.
{
    const T = ll(140, 220, O), N = ll(170, 540, O);
    check('C wide flyby right side', runLeg(T, N, 'P', track(O, [[290, -50], [300, 220], [310, 540]])), []);
}
// D. Wide flyby wrong side: must NOT pass (WRONG from afar is acceptable).
{
    const T = ll(140, 220, O), N = ll(170, 540, O);
    const ev = runLeg(T, N, 'P', track(O, [[-10, -50], [0, 220], [10, 540]]));
    const ok = !ev.includes('PASS');
    console.log((ok ? 'ok   ' : 'FAIL ') + 'D wide flyby wrong side -> [' + ev.join(',') + ']');
    if (!ok) fails++;
}
// E. Wrong-way (starboard) loop: must WRONG, never PASS.
{
    const T = ll(0, 1000, O), N = ll(0, 0, O);
    const pts = [[0, 700], [-40, 950], [-30, 1030], [40, 1020], [60, 950], [40, 800]];
    check('E wrong-way loop', runLeg(T, N, 'P', track(O, pts)), ['WRONG']);
}
// F. Gate threaded: must PASS. G. Gate skirted: must NOT.
{
    const G1 = ll(-20, 1000, O), G2 = ll(20, 1000, O), N = ll(0, 1300, O);
    check('F gate threaded', runGate(G1, G2, N, track(O, [[0, 800], [0, 1100], [0, 1250]])), ['PASS']);
    check('G gate skirted', runGate(G1, G2, N, track(O, [[60, 800], [60, 1100], [40, 1250]])), []);
}

if (fails) { console.log(fails + ' FAILURES'); process.exit(1); }
console.log('all green');
