#include "gps_mock.h"
#include "config.h"
#include "race_session.h"
#include "screens.h"
#include "serial_buffer.h"
#include "server_link.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
// The journey is a TAPE, not a stream.
//
// The mock feed used to poll a 30-fix window every ~30s and walk it. The
// window was cut by the SERVER's clock, and that broke races two ways:
//
//   - a device that adopted the session late began mid-journey. Race 182
//     started at t=130s: the start crossing and the first mark's rounding
//     were already behind it, so the boat's target stayed one mark behind
//     for the whole race, and it then crossed the finish line still waiting
//     for a mark the route had already given it. Nothing looked wrong.
//   - a device that had run AHEAD of the window was handed fixes it had
//     already passed; they were dropped in 13-fix lumps, a ~150m hole in
//     the track. A hole that lands on a rounding is a pass that never
//     happened.
//
// So one request now buys the WHOLE journey (GET /sim/tape, CSV, ~18KB for
// an 8 minute race) and the device plays it from RAM, one fix per second,
// through to the end. The network can delay the START of a mock race; it
// can no longer punch a hole in the middle of one, and a stalled render
// pass cannot make the boat skip either.
//
// A fix is 16 bytes (E7 degrees, tenths, epoch seconds), so a whole journey
// is ~7KB of static RAM instead of a 64-deep window of 60-byte structs. The
// ISO stamp NMEA wants is BUILT at release time (isoFromEpoch), to the same
// second the JSON feed used to carry.
//
// The wander (mock with no scripted run) is endless, so it arrives 30 fixes
// at a time from the same tape URL and is refilled — but it lands in the
// same tape and is released by the same 1Hz player.
// ---------------------------------------------------------------------
static const int SIM_TAPE_MAX = 640;   // fixes: ~10 minutes at 1Hz
static const int SIM_FIX_MS = 1000;    // the tape plays at the GPS's own rate
static const int SIM_TAPE_LOW = 20;    // page / refill the tape below this
static const long SIM_RUN_CHECK_MS = 20000; // "same run still?" cadence

// The tape is written by the backend task (fetch) and read by the UI loop
// (release), so every head/count mutation lives under this short critical
// section. feedServerFix() is deliberately OUTSIDE it: encoding NMEA is far
// too slow to hold a spinlock. It is a RING, so the endless walk can refill
// it for as long as the boat walks.
static portMUX_TYPE simMux = portMUX_INITIALIZER_UNLOCKED;

struct MockFix {
    int32_t latE7, lonE7;   // degrees x 1e7 (~1cm)
    int16_t spd10, crs10;   // knots x 10, degrees x 10
    uint32_t epoch;         // fix time, whole seconds
};

static MockFix simTape[SIM_TAPE_MAX];
static int simHead = 0;                 // next fix to release
static volatile int simCount = 0;       // fixes in the tape
static volatile int simLoaded = 0;      // fixes the server has handed us (page cursor)
static volatile int simTotal = 0;       // fixes in the current journey
static volatile long simSessionId = 0;  // the session this journey is raced in
static volatile long simGunSec = 0;     // script seconds from tape start to the gun
static unsigned long simNextFixAt = 0;  // when the next fix is due
static bool simFetching = false;
static unsigned long simNextAttemptAt = 0; // fetch backoff (a 404 is not free)
static unsigned long simLastCheck = 0;     // last "same run?" check
static char simRunId[24] = {0};
static volatile bool simWander = false; // the tape holds the walk, not a run
// UI thread only: -1 unknown, 0 held, 1 sailing. Logged on change, so a held
// journey says WHY it is held instead of just looking like a dead boat.
static int8_t simGateState = -1;

static void tapeResetLocked()
{
    simHead = 0;
    simCount = 0;
    simLoaded = 0;
    simTotal = 0;
    simNextFixAt = millis();   // a fresh tape is due immediately
    simGateState = -1;         // ...and says why if it does not sail
}

// Append one fix. Caller holds simMux. False when the tape is full.
static bool tapePushLocked(double lat, double lon, double speed,
                           double course, long epoch)
{
    if (epoch <= 0) return false;
    if (simCount >= SIM_TAPE_MAX) return false;
    const int idx = (simHead + simCount) % SIM_TAPE_MAX;
    simTape[idx].latE7 = (int32_t)lround(lat * 1e7);
    simTape[idx].lonE7 = (int32_t)lround(lon * 1e7);
    simTape[idx].spd10 = (int16_t)lround(speed * 10.0);
    simTape[idx].crs10 = (int16_t)lround(course * 10.0);
    simTape[idx].epoch = (uint32_t)epoch;
    simCount++;
    return true;
}

// Epoch seconds -> the ISO stamp feedServerFix parses. UTC, which is what
// the script (and the server's own clock) is in.
static void isoFromEpoch(uint32_t epoch, char* out, size_t n)
{
    time_t t = (time_t)epoch;
    struct tm tmv;
    if (!gmtime_r(&t, &tmv)) { out[0] = '\0'; return; }
    snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02d",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

static String tapeUrl(int from)
{
    return serverBaseUrl() + "/sim/tape?deviceId=" + String(WiFi.macAddress()) +
           "&from=" + String(from);
}

// GET the tape — a whole journey, the next page of one, or 30 fixes of the
// walk — and push it into RAM. Returns false when the server has nothing
// (404) or the socket died.
//
// The header names the source. A DIFFERENT run id means a new journey (or
// the walk taking over): the tape is dropped and refilled from that feed's
// own fix 0. When we were reading a cursor into the OLD feed, the rows that
// follow are the new feed seen from that stale cursor, so they are skipped
// and the next pass reloads from zero; when the read was already from 0,
// the rows that follow ARE the new feed's start and are kept.
static bool simFetchTape(int from)
{
    const unsigned long t0 = millis();
    ServerLink link;
    if (!link.begin(tapeUrl(from))) {
        bufferedSerialPrintln("[MOCK] fetch could not start");
        return false;
    }
    // A whole journey is ~18KB; the 8s default is a heartbeat's timeout.
    link.http.setTimeout(20000);
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        link.http.end();
        bufferedSerialPrintln(String("[MOCK] fetch HTTP ") + code);
        return false;
    }
    WiFiClient& st = link.http.getStream();
    const int len = link.http.getSize();
    st.setTimeout(15000);

    char line[96];
    char rid[24] = {0};
    long sid = 0, gun = 0, dur = 0, total = 0, nowEpoch = 0;
    int added = 0;
    bool changed = false;    // the feed is not the one in the tape
    bool restart = false;    // ...and these rows start at the old cursor
    size_t got = 0;

    while (len <= 0 || got < (size_t)len) {
        const size_t n = st.readBytesUntil('\n', line, sizeof(line) - 1);
        if (n == 0) break;                       // stalled or end of body
        got += n + 1;
        line[n] = '\0';
        if (line[n - 1] == '\r') line[n - 1] = '\0';
        if (line[0] == '#') {
            if (sscanf(line, "#%23[^,],%ld,%ld,%ld,%ld,%ld",
                       rid, &sid, &gun, &dur, &total, &nowEpoch) >= 1) {
                if (nowEpoch > 0) raceSetWallEpochFromServer(nowEpoch);
                portENTER_CRITICAL(&simMux);
                if (rid[0] && strcmp(rid, simRunId) != 0) {
                    tapeResetLocked();
                    strncpy(simRunId, rid, sizeof(simRunId) - 1);
                    simRunId[sizeof(simRunId) - 1] = '\0';
                    simWander = (strcmp(rid, "wander") == 0);
                    simTotal = (int)total;
                    simSessionId = sid;
                    simGunSec = gun;   // the tape's own clock: crossing = gun + 5 s
                    changed = true;
                    restart = (from > 0);
                }
                portEXIT_CRITICAL(&simMux);
            }
            if (restart) break;
            continue;
        }
        long epoch = 0;
        double lat = 0, lon = 0, spd = 0, crs = 0;
        if (sscanf(line, "%ld,%lf,%lf,%lf,%lf", &epoch, &lat, &lon, &spd, &crs) != 5)
            continue;
        portENTER_CRITICAL(&simMux);
        const bool ok = tapePushLocked(lat, lon, spd, crs, epoch);
        if (ok) { added++; simLoaded++; }
        portEXIT_CRITICAL(&simMux);
        if (!ok) break;                          // full: page the rest later
    }
    link.http.end();

    if (changed) {
        bufferedSerialPrintln(String("[MOCK] feed ") + rid +
                              (restart ? " (reloading from 0)" : " (loaded)"));
    } else if (added || from == 0) {
        bufferedSerialPrintln(String("[MOCK] tape +") + added +
                              " from=" + from + " queued=" + simCount +
                              " total=" + simTotal +
                              " fetch=" + (millis() - t0) + "ms");
    }
    return true;
}

// Backend task: refill the tape, and keep an eye on the feed's identity.
// Blocking (HTTP, up to ~20s for a whole journey), but it never touches the
// release path: a slow fetch costs the START of the next mock race, never a
// hole in the middle of one.
//
// Three reasons to fetch, in priority order:
//   empty                  nothing in RAM — load the journey (or the walk)
//   page                   more of a journey left: read the next slice
//   check                  a journey is fully loaded: ask for the header
//                          alone every 20s, so a NEW session arriving
//                          mid-feed is picked up without a reboot
void gpsMockFeed(TinyGPSPlus& gps)
{
    (void)gps;
    if (!gpsMockActive()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    // NOTE: no mid-race hold here. Mock means testing: freezing the fix
    // for the whole run strands the engine (and the diagnostics Age).
    // A true dropout (no server answer) still holds the last fix.
    if (simFetching) return;
    if ((long)(millis() - simNextAttemptAt) < 0) return;

    int count, loaded, total;
    bool wander;
    portENTER_CRITICAL(&simMux);
    count = simCount; loaded = simLoaded; total = simTotal; wander = simWander;
    portEXIT_CRITICAL(&simMux);

    const bool empty = (count == 0);
    const bool page = !wander && total > 0 && loaded < total && count < SIM_TAPE_LOW;
    const bool check = !wander && !empty && !page &&
                       (millis() - simLastCheck) >= SIM_RUN_CHECK_MS;
    if (!empty && !page && !check) return;

    simFetching = true;
    const bool ok = simFetchTape((page || check) ? loaded : 0);
    simFetching = false;
    // A 404 is a real answer (no run and no seed to walk from): it is not
    // free, so back off harder on it than on a success.
    simNextAttemptAt = millis() + (ok ? 1000 : 5000);
    if (check) simLastCheck = millis();
}

// UI loop: release ONE fix per second out of RAM. No I/O, no heap, no
// stamps to compare against the wall clock — a slow render pass can DELAY
// a fix, but it can never SKIP one, and nothing here can block on a socket.
//
// This is where the old feed lost races: the queue was a 30-fix window of
// the server's clock, so a stall (or a late start) meant fixes already
// behind were dropped. The tape has no behind: it is the journey itself,
// played in order, and it runs out only when the journey does.
void gpsMockTick(TinyGPSPlus& gps)
{
    if (!gpsMockActive()) return;

    int count;
    long wantSession;
    bool wander;
    portENTER_CRITICAL(&simMux);
    count = simCount; wantSession = simSessionId; wander = simWander;
    portEXIT_CRITICAL(&simMux);
    if (count == 0) return;  // nothing loaded (yet): hold the last fix

    // A scripted journey belongs to ONE session and is scored only on the race
    // screen, so it must not sail before both of those are true. That is how
    // race 182 lost its start — the boat sailed its first 130s with the screen
    // on MAIN, so the engine began mid-course and stayed one mark behind, then
    // crossed the finish line still waiting for a mark the route had already
    // given it. Holding the tape costs a delay; sailing it unwatched costs the
    // truth. (The WANDER is not a journey and keeps walking everywhere.)
    const char* hold = nullptr;
    if (!wander) {
        if (page != PageRace) hold = (page == PageMain) ? "main screen" : "not on race";
        else if (!raceSession.valid) hold = "no session yet";
        else if (wantSession && raceSession.sessionId != wantSession) hold = "another session";
    }
    if (hold) {
        if (simGateState != 0) {
            simGateState = 0;
            bufferedSerialPrintln(String("[MOCK] journey held: ") + hold);
        }
        return;
    }
    if (!wander && simGateState != 1) {
        simGateState = 1;
        // Align the tape to the WALL CLOCK, not to the moment we happened to
        // find the session. The script puts the line cross at gun+5 in ITS
        // clock (the run's creation), but a session made on the web only
        // reaches the boat on the next heartbeat — up to 30 s idle — and
        // playing from the tape's first fix then shifts the whole journey by
        // that lag: the start lands ~30 s after the gun and the course runs
        // late for the rest of the race. Drop the fixes already in the past so
        // the journey resumes where the run's clock says it is.
        //
        // Capped at the script's gun: the crossing is gun+5, so the start
        // always stays ahead of us (a boat that only hears about the session
        // AFTER the gun still sails the last five seconds of the approach and
        // crosses late — that is the honest limit of a scripted feed).
        const long nowEpoch = raceWallEpoch();
        int dropped = 0;
        if (nowEpoch > 0) {
            portENTER_CRITICAL(&simMux);
            const long maxDrop = simGunSec;
            while (simCount > 1 && dropped < maxDrop &&
                   (long)simTape[simHead].epoch < nowEpoch - 1) {
                simHead = (simHead + 1) % SIM_TAPE_MAX;
                simCount--;
                dropped++;
            }
            portEXIT_CRITICAL(&simMux);
        }
        if (dropped > 0) {
            bufferedSerialPrintln(String("[MOCK] journey aligned: skipped ") +
                                  dropped + " stale fix(es)");
        }
        bufferedSerialPrintln(String("[MOCK] journey sailing (session ") + wantSession + ")");
    }

    MockFix due;
    bool have = false;
    const unsigned long now = millis();
    portENTER_CRITICAL(&simMux);
    if (simCount > 0 && (long)(now - simNextFixAt) >= 0) {
        due = simTape[simHead];
        simHead = (simHead + 1) % SIM_TAPE_MAX;
        simCount--;
        have = true;
    }
    portEXIT_CRITICAL(&simMux);

    if (!have) return;      // nothing loaded (yet): hold the last fix
    simNextFixAt = now + SIM_FIX_MS;

    char st[24];
    isoFromEpoch(due.epoch, st, sizeof(st));
    if (!feedServerFix(gps, due.latE7 / 1e7, due.lonE7 / 1e7,
                       due.spd10 / 10.0, due.crs10 / 10.0, st))
        return;

    // One line a minute: proof the tape really is playing at 1Hz, and how
    // much runway is left. The old cadence line cost a third of the
    // 200-line serial ring for the same information.
    static int played = 0;
    if (++played % 60 == 0) {
        bufferedSerialPrintln(String("[MOCK] played ") + played +
                              ", left=" + simCount +
                              (simWander ? " (walk)" : ""));
    }
}
