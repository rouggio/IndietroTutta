#include "gps_mock.h"
#include "config.h"
#include "race_session.h"
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

// ---------------------------------------------------------------------
// Batched feed, split across two callers.
//
// This used to fetch ONE fix per HTTP round trip at 1Hz, so every fix
// cost a request on the critical mock path. Now one request buys 30
// fixes: the device buffers them and emits from RAM.
//
// The split matters more than the batch. Fetch and emit used to share
// one function on the backend task, which blocks ~2s per POST /gps, so
// the shared call ran ~3x below 1Hz: the queue head drifted past the
// staleness guard and ~7 fixes per cycle were dropped. Measured, not
// assumed. So:
//   gpsMockFeed()  backend task — refill only, blocking, once per batch
//   gpsMockTick()  UI loop — release ONE due fix, pure RAM, no I/O
// A fetch in flight can therefore never stall the tick.
//
// Emission is CLOCK-driven, not count-driven: a fix is released when the
// wall clock crosses its own stamped time. If the loop stalls, fixes are
// SKIPPED (a small gap in the mock track) rather than replayed late —
// the mock timestamp is what calibrates raceWallEpoch, and therefore
// what the gun countdown and every log line read, so letting it slip
// would quietly corrupt the countdown rather than just the track.
// ---------------------------------------------------------------------
static const int SIM_BATCH_MAX = 30;
// Read-ahead floor. The effective threshold is simRefetchAt(), which
// scales with the measured fetch cost.
static const int SIM_REFETCH_FLOOR = 6;
// How far behind schedule a fix may be before it is dropped instead of
// emitted late — measured against the PREVIOUS fix's stamp, NOT against
// the wall clock.
//
// An earlier version compared `now - head.epoch >= 3`, which cannot work:
// the queue is deliberately buffered BEHIND real time (a refetch appends
// 30 future points behind the few still queued, so the head legitimately
// sits ~6s in the past). That permanent offset read as staleness and
// dropped ~3-5 fixes every 20s that were never actually late. Emission is
// now paced by the gap between consecutive stamps instead.
static const long SIM_SKIP_BEHIND = 3;

// The queue is written by the backend task (fetch) and read by the UI
// loop (tick), so the head/count mutations live under a short critical
// section. feedServerFix() is deliberately OUTSIDE it: encoding NMEA is
// far too slow to hold a spinlock.
static portMUX_TYPE simMux = portMUX_INITIALIZER_UNLOCKED;

struct MockFix {
    double lat, lon, speed, course;
    long epoch;      // when this fix is due (parsed from st)
    char st[24];     // the same stamp, verbatim: feedServerFix builds NMEA
};

// Ring: 30 arriving + up to 30 still queued while a refetch is in
// flight = 60, rounded to 64.
static MockFix simQueue[64];
static int simHead = 0;  // next fix to emit
static int simCount = 0; // fixes queued
static bool simFetching = false;
static unsigned long simLastFetch = 0;
static unsigned long simLastFetchMs = 0; // measured cost of the last fetch
static unsigned long simLastEmitMs = 0;  // when the last fix was released
static long simLastEmitEpoch = 0;        // stamp of the last released fix
static char simRunId[24] = {0};
static bool simWander = false;

static int simRemaining() { return simCount; }

// Local schedule for emission: the epoch of the queue head when the
// batch was armed, plus elapsed millis. Deliberately NOT raceWallEpoch():
// that clock is calibrated only once a fix has been parsed by the race
// engine, so on the MAIN screen — where no race code runs — it reads -1
// and gating emission on it stalls the whole mock feed.
static long simArmEpoch = 0;
static unsigned long simArmMs = 0;

static long simNow()
{
    if (simArmEpoch > 0)
        return simArmEpoch + (long)((millis() - simArmMs) / 1000);
    return raceWallEpoch();
}

static void simQueueReset()
{
    simHead = 0;
    simCount = 0;
}

static bool simQueuePush(double lat, double lon, double speed, double course, const char* st)
{
    const long epoch = raceIsoEpoch(st);
    if (epoch <= 0) return false;
    if (simCount >= (int)(sizeof(simQueue) / sizeof(simQueue[0]))) return false;
    const int idx = (simHead + simCount) % (int)(sizeof(simQueue) / sizeof(simQueue[0]));
    simQueue[idx].lat = lat;
    simQueue[idx].lon = lon;
    simQueue[idx].speed = speed;
    simQueue[idx].course = course;
    simQueue[idx].epoch = epoch;
    strncpy(simQueue[idx].st, st, sizeof(simQueue[idx].st) - 1);
    simQueue[idx].st[sizeof(simQueue[idx].st) - 1] = '\0';
    simCount++;
    return true;
}

static int simRefetchAt(); // defined below; used in the fetch log line

static void simFetchBatch()
{
    if (simFetching) return;
    simFetching = true;
    const unsigned long t0 = millis();
    simLastFetch = t0;

    ServerLink link;
    if (!link.begin(serverBaseUrl() + "/sim/next?deviceId=" + String(WiFi.macAddress()))) {
        bufferedSerialPrintln("[MOCK] fetch could not start");
        simFetching = false;
        return;
    }
    const int code = link.http.GET();
    if (code != HTTP_CODE_OK) {
        bufferedSerialPrintln(String("[MOCK] fetch HTTP ") + code);
        link.http.end();
        simFetching = false;
        return;
    }
    String payload = link.http.getString();
    link.http.end();
    // 30 fixes with per-point ISO stamps run ~1.8KB after the backend
    // trimmed the coordinates to 6 decimals. The cap is deliberately
    // ABOVE the doc size: an oversize body is logged and dropped rather
    // than silently parsed into a truncated document.
    if (payload.isEmpty() || payload.length() > 8192) {
        bufferedSerialPrintln(String("[MOCK] fetch rejected, bytes=") +
                              payload.length());
        simFetching = false;
        return;
    }

    // The doc must hold the WHOLE document: 30 objects x 5 members, each
    // with a 24-char stamp. ArduinoJson's footprint runs well above the
    // JSON text (a 3072 doc silently failed to parse a 3141-byte reply,
    // and a 6144 doc is still short of a 2868-byte body), so this is
    // sized for the parsed tree, not the wire. It is transient and only
    // allocated once per batch (~30s), which is the price of the batch.
    DynamicJsonDocument doc(12288);
    if (deserializeJson(doc, payload)) {
        bufferedSerialPrintln(String("[MOCK] parse failed, bytes=") +
                              payload.length());
        simFetching = false;
        return;
    }

    JsonArray arr = doc["batch"].as<JsonArray>();
    int added = 0;
    long armEpoch = 0;
    bool freshArm = false;
    // The whole queue mutation happens under one short lock: the UI tick
    // may be mid-loop on the other task while this appends.
    portENTER_CRITICAL(&simMux);
    if (!doc["batch"].isNull() && arr.size() > 0) {
        // Only reset when the queue has drained to nothing: a refetch
        // lands while fixes are still queued, and discarding them would
        // punch a hole in the track.
        if (simCount == 0) {
            simQueueReset();
            // A drained queue means the previous stream is GONE — the run
            // ended, the server switched sources, or we simply fell behind.
            // The pacing rule measures the head against simLastEmitEpoch, so
            // carrying the old stream's epoch across a fresh arm made every
            // point of the new batch look hours ahead and the whole batch
            // was skipped as stale — every batch, forever, so the boat
            // froze. Reset the pacer with the stream.
            simLastEmitEpoch = 0;
            freshArm = true;
        }
        for (JsonObject o : arr) {
            const char* st = o["st"] | "";
            if (simQueuePush(o["lat"] | 0.0, o["lon"] | 0.0,
                             o["speed"] | 0.0, o["course"] | 0.0, st)) {
                // Anchor the local schedule on the FIRST fix of the
                // batch, and only when we started from empty: a refetch
                // landing mid-drain must not re-anchor, because the head
                // has already moved on.
                if (added == 0 && armEpoch == 0) armEpoch = simQueue[simHead].epoch;
                added++;
            }
        }
        strncpy(simRunId, doc["runId"] | "", sizeof(simRunId) - 1);
        simRunId[sizeof(simRunId) - 1] = '\0';
        simWander = doc["wander"] | false;
    } else {
        // Pre-batching server (or a single-sample reply): fall back to
        // the flat fields so an older backend still drives the device.
        const char* st = doc["serverTime"] | "";
        if (simQueuePush(doc["lat"] | 0.0, doc["lon"] | 0.0,
                         doc["speed"] | 0.0, doc["course"] | 0.0, st)) {
            if (armEpoch == 0) armEpoch = simQueue[simHead].epoch;
            added++;
        }
    }
    if (armEpoch > 0) {
        simArmEpoch = armEpoch;
        simArmMs = millis();
        // On a fresh arm the head is due immediately — otherwise simNow()
        // (anchored on this same epoch) would read as not-due for a whole
        // batch, and the pacer would fight it.
        if (freshArm) simLastEmitEpoch = 0;
    }
    const int queued = simCount;
    portEXIT_CRITICAL(&simMux);

    // Anchor the wall clock on the SERVER's clock, not on a queued fix. The
    // batch is a lookahead window, so the head fix's stamp legitimately
    // lags real time by however far we have buffered; calibrating from it
    // would rewind time by exactly that backlog (measured: 5, 6, 7s and
    // growing), which showed up as the gun countdown and pane TIME
    // lurching backwards. serverTime is the authoritative clock.
    raceSetWallEpochFromServer(raceIsoEpoch(doc["serverTime"] | ""));

    simLastFetchMs = millis() - t0;
    if (added) {
        bufferedSerialPrintln(String("[MOCK] batch +") + added +
                              " queued=" + queued +
                              " fetch=" + simLastFetchMs + "ms" +
                              " ahead=" + simRefetchAt() + "s");
    }
    simFetching = false;
}

// Read-ahead: how many fixes must remain before the next fetch starts.
// Adaptive, because no fixed threshold is right for both a fast LAN fetch
// and a slow one. What matters is only that the next fetch lands BEFORE
// the buffer empties, so: two measured fetch durations of runway, with a
// floor (so the very first fetch, with nothing measured yet, still has
// margin) and a cap at half the batch.
static int simRefetchAt()
{
    int ahead = (int)(simLastFetchMs / 1000) * 2 + 2;
    if (ahead < SIM_REFETCH_FLOOR) ahead = SIM_REFETCH_FLOOR;
    if (ahead > SIM_BATCH_MAX / 2) ahead = SIM_BATCH_MAX / 2;
    return ahead;
}

// Backend task: refill only. Blocking (HTTP), but at most once per batch,
// and it never touches the release path — so a slow fetch costs track
// runway, not tick timing.
void gpsMockFeed(TinyGPSPlus& gps)
{
    (void)gps;
    if (!gpsMockActive()) return;
    if (WiFi.status() != WL_CONNECTED) return;
    // NOTE: no mid-race hold here. Mock means testing: freezing the fix
    // for the whole run strands the engine (and the diagnostics Age).
    // A true dropout (no server answer) still holds the last fix.
    if (simFetching) return;
    if ((millis() - simLastFetch) < 1000) return;

    int remaining;
    portENTER_CRITICAL(&simMux);
    remaining = simCount;
    portEXIT_CRITICAL(&simMux);
    if (remaining > simRefetchAt()) return;

    simFetchBatch();
}

// UI loop: release at most ONE due fix. Pure RAM work, no I/O, so a
// fetch in flight cannot stall it. This is what makes the ticks regular:
// the old combined call ran at the backend task's ~0.3Hz.
void gpsMockTick(TinyGPSPlus& gps)
{
    if (!gpsMockActive()) return;

    MockFix due;
    bool have = false;
    int skipped = 0;
    const long now = simNow();

    portENTER_CRITICAL(&simMux);
    if (simCount > 0 && now > 0) {
        // Pacing: release a fix when its own stamp is due, and only ever
        // one per stamp-gap. simLastEmitEpoch is the stamp of the fix we
        // released last, so this walks the batch in stamp order and never
        // replays a backlog — a stall simply pauses the queue.
        while (simLastEmitEpoch > 0 && simCount > 0 &&
               simQueue[simHead].epoch - simLastEmitEpoch > SIM_SKIP_BEHIND) {
            // A stamp far ahead of the last release means real time passed
            // without us emitting (a genuine stall). Skip forward to the
            // newest fix that is still in step, rather than dumping the
            // whole backlog into the GPS at once.
            simHead = (simHead + 1) % (int)(sizeof(simQueue) / sizeof(simQueue[0]));
            simCount--;
            skipped++;
        }
        if (simCount > 0 && now >= simQueue[simHead].epoch) {
            due = simQueue[simHead];
            have = true;
            simLastEmitEpoch = due.epoch;
            simHead = (simHead + 1) % (int)(sizeof(simQueue) / sizeof(simQueue[0]));
            simCount--;
        }
    }
    portEXIT_CRITICAL(&simMux);

    if (skipped) {
        bufferedSerialPrintln(String("[MOCK] skipped ") + skipped + " queued=" +
                              simRemaining());
    }
    if (!have) return; // nothing due: hold the last fix
    if (!feedServerFix(gps, due.lat, due.lon, due.speed, due.course, due.st)) return;
    simLastEmitMs = millis();

    // Release timing, once every 10 fixes: the gap between this fix's
    // stamp and the previous one is the number that proves the feed is
    // actually 1Hz. Sampling keeps the 200-line serial ring usable.
    static long prevEpoch = 0;
    static int cadenceN = 0;
    cadenceN++;
    if (prevEpoch > 0 && (cadenceN % 10) == 0) {
        bufferedSerialPrintln(String("[MOCK] cadence dt=") + (due.epoch - prevEpoch) +
                              "s queued=" + simRemaining());
    }
    prevEpoch = due.epoch;

    // Echo every 30th fix: pipeline proof, one per batch round rather
    // than one per fix, so it costs nothing now that fixes are batched.
    static int fixCount = 0;
    fixCount++;
    if (!simWander && simRunId[0] && (fixCount % 30) == 0) {
        ServerLink echoLink;
        if (echoLink.begin(serverBaseUrl() + "/sim/echo")) {
            echoLink.http.addHeader("Content-Type", "application/json");
            String body = String("{\"runId\":\"") + simRunId +
                          String("\",\"t\":") + (long)due.epoch +
                          String(",\"lat\":") + String(due.lat, 7) +
                          String(",\"lon\":") + String(due.lon, 7) +
                          String(",\"speed\":") + String(due.speed, 1) +
                          String(",\"course\":") + String(due.course, 1) + "}";
            echoLink.http.POST(body);
            echoLink.http.end();
        }
    }
}
