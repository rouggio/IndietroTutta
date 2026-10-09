#include "config.h"
#include "backend.h"
#include "config_store.h"
#include "serial_buffer.h"
#include "race_session.h"
#include "race_run.h"
#include "race_courses.h"
#include "gps_mock.h"
#include "server_link.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ---------------------------------------------------------
// Background server interactions.
//
// The UI thread NEVER performs blocking network calls: flagged
// positions and periodic telemetry are enqueued here as tiny
// snapshots, and a dedicated FreeRTOS task drains the queue and
// runs the health poll. The main loop therefore never stalls
// waiting for DNS/TLS/server round-trips.
// ---------------------------------------------------------

constexpr unsigned long HEALTH_CHECK_INTERVAL = 30000;
constexpr unsigned long HEALTH_LIVE_INTERVAL = 5000; // session live: signals fast
// GPS throttling: baseline 30s at 0 knots, 2s at >=5 knots, linear in between
constexpr unsigned long GPS_BASE_INTERVAL_MS = 30000;
constexpr unsigned long GPS_FAST_INTERVAL_MS = 2000;
constexpr double GPS_FAST_SPEED_KNOTS = 5.0;
constexpr unsigned long TASK_TICK_MS = 250;

static unsigned long gpsIntervalForSpeed(double speedKnots) {
    if (speedKnots <= 0) return GPS_BASE_INTERVAL_MS;
    if (speedKnots >= GPS_FAST_SPEED_KNOTS) return GPS_FAST_INTERVAL_MS;
    double ratio = speedKnots / GPS_FAST_SPEED_KNOTS; // 0..1
    double interval = GPS_BASE_INTERVAL_MS - (GPS_BASE_INTERVAL_MS - GPS_FAST_INTERVAL_MS) * ratio;
    return (unsigned long)(interval + 0.5);
}

struct BackendWork {
    double lat;
    double lon;
    double speed;
    double course;
    double altitude;
    int sats;
    bool simulated; // true when the fix came from mock GPS (indoor testing)
};

static QueueHandle_t workQueue = nullptr;
static volatile bool online = false;
static volatile bool healthNow = false;
static volatile bool courseWant = false;
static TinyGPSPlus* mainGps = nullptr;

bool backendOnline()
{
    return online;
}

void backendPollHealthNow()
{
    healthNow = true;
}

void backendFetchCourses()
{
    courseWant = true; // drained by the task loop (blocking fetch there)
}

// ---------------------------------------------------------

static bool enqueueWork(const BackendWork &w)
{
    if (!workQueue) {
        return false;
    }

    // Never let the queue fill up: drop the oldest entry instead
    if (uxQueueSpacesAvailable(workQueue) == 0) {
        BackendWork dropped;
        xQueueReceive(workQueue, &dropped, 0);
        bufferedSerialPrintln("[BACKEND] Queue full, dropped oldest");
    }

    return xQueueSend(workQueue, &w, 0) == pdTRUE;
}

// ---------------------------------------------------------
// Worker task side: actual network I/O
// ---------------------------------------------------------

static void healthCheck()
{
    if (WiFi.status() != WL_CONNECTED) {
        online = false;
        return;
    }

    ServerLink link;

    if (link.begin(serverBaseUrl() + "/health")) {
        link.http.addHeader("DeviceId", String(WiFi.macAddress()));
        link.http.addHeader("Firmware-Version", BUILD_VERSION);

        if (config.username[0] != '\0') {
            link.http.addHeader("Username", String(config.username));
        }

        int code = link.http.GET();
        if (code == HTTP_CODE_OK) {
            online = true;
            // Race push rides the heartbeat: parse the session (if any).
            // ~2KB body, heap-backed JSON — safe on the task stack.
            String body = link.http.getString();
            if (body.length() > 0 && body.length() < 8192) {
                raceSessionParse(body.c_str());
            }
        } else {
            online = false;
        }
        link.http.end();
    }
    else {
        online = false;
    }
}

// Returns the HTTP status (<=0 network error, >=500 retryable server
// error, 200 stored, 4xx poison — caller decides retry vs drop).
static int sendPosition(const BackendWork &w)
{
    if (WiFi.status() != WL_CONNECTED) {
        online = false;
        return -1;
    }

    ServerLink link;

    if (!link.begin(serverBaseUrl() + "/gps")) {
        online = false;
        return -1;
    }

    link.http.addHeader("Content-Type", "application/json");
    link.http.addHeader("DeviceId", String(WiFi.macAddress()));
    link.http.addHeader("Firmware-Version", BUILD_VERSION);

    String body = "{";
    body += "\"lat\":" + String(w.lat, 7);
    body += ",\"lon\":" + String(w.lon, 7);
    body += ",\"speed\":" + String(w.speed, 1);
    body += ",\"course\":" + String(w.course, 1);
    body += ",\"altitude\":" + String(w.altitude, 1);
    body += ",\"sats\":" + String(w.sats);
    body += ",\"flagged\":false";
    body += ",\"simulated\":" + String(w.simulated ? "true" : "false");
    body += ",\"fw\":\"" BUILD_VERSION "\"";

    if (config.username[0] != '\0') {
        body += ",\"username\":\"" + String(config.username) + "\"";
    }

    body += "}";

    int code = link.http.POST(body);

    link.http.end();

    online = (code == HTTP_CODE_OK);
    return code;
}

// Step 5: upload the finished run (splits + event log). Ack on accept
// (201) or reject (4xx: retrying a bad payload is pointless); network
// failures (<=0/5xx) keep the pending flag for the next health cycle.
static void sendRunResult()
{
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    if (!raceSession.valid || raceSession.sessionId <= 0) {
        raceUploadAck();
        return;
    }

    ServerLink link;

    String url = serverBaseUrl() + "/sessions/" + String(raceSession.sessionId) + "/runs";
    if (!link.begin(url)) {
        return;
    }

    link.http.addHeader("Content-Type", "application/json");
    link.http.addHeader("DeviceId", String(WiFi.macAddress()));

    char body[2048];
    raceUploadBody(WiFi.macAddress().c_str(), body, sizeof(body));

    const int code = link.http.POST((uint8_t*)body, strlen(body));
    link.http.end();

    if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED ||
        (code > 0 && code < 500)) {
        raceUploadAck();
        bufferedSerialPrintln("[BACKEND] run uploaded");
    }
}

static void backendTask(void *param){
    (void)param;

    BackendWork w;

    for (;;) {
        bool haveItem =
            xQueueReceive(workQueue, &w, pdMS_TO_TICKS(TASK_TICK_MS)) == pdTRUE;

        unsigned long now = millis();
        const unsigned long passStart = now;
        unsigned long mockMs = 0, sendMs = 0;

        // One-shot course library fetch for instant practice setup.
        if (courseWant) {
            courseWant = false;
            courseFetch();
        }

        // Mock GPS source (indoor testing): scripted fixes in, tagged out.
        // The UART is drained unparsed while mocked (see gpsLoop), so the
        // mock owns the fix exclusively — no blending with flaky real fixes.
        // Uploads carry simulated:true so the map can show them as such.
        const bool mock = gpsMockActive();
        if (mock && mainGps) {
            const unsigned long t0 = millis();
            gpsMockPoll(*mainGps);
            mockMs = millis() - t0;
        }

        if (haveItem) {
            // Queued items carry their own simulated tag (set at enqueue
            // time), so real fixes queued before mock-on stay real.
            // Transient failures requeue to the front (same as deletes);
            // 4xx is a poison payload — drop it, don't wedge the queue.
            const unsigned long t0 = millis();
            const int code = sendPosition(w);
            sendMs = millis() - t0;
            if (code == HTTP_CODE_OK) {
                bufferedSerialPrintln("[BACKEND] Position sent");
            } else if (code <= 0 || code >= 500) {
                bufferedSerialPrintln("[BACKEND] Position send failed, retrying");
                xQueueSendToFront(workQueue, &w, 0);
            } else {
                bufferedSerialPrintln("[BACKEND] Position rejected, dropping");
            }
        }

        // Slow-pass telemetry: a stalled pass starves the GPS feed, so the
        // diagnostics Age spikes. Log the split to find the hog.
        // (Heartbeat runs on its own task now — health no longer appears here.)
        const unsigned long passMs = millis() - passStart;
        if (passMs > 2000) {
            bufferedSerialPrintln(String("[BACKEND] slow pass ") + passMs +
                "ms (mock " + mockMs + " send " + sendMs + ")");
        }
    }
}

// Heartbeat on its own task: a slow /health round trip (session geometry
// + signals + Turso writes) must never starve the GPS feed. Same rand
// intervals as before — 30s idle, 5s when the session is live.
static void healthTask(void *param)
{
    (void)param;
    unsigned long lastHealthCheck = 0;
    for (;;) {
        const unsigned long now = millis();
        const unsigned long healthInterval =
            raceSessionLive() ? HEALTH_LIVE_INTERVAL : HEALTH_CHECK_INTERVAL;
        if (healthNow || now - lastHealthCheck >= healthInterval) {
            healthNow = false;
            lastHealthCheck = now;
            healthCheck();
            if (online && raceUploadPending()) {
                sendRunResult();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TASK_TICK_MS));
    }
}

// ---------------------------------------------------------
// UI-thread side: cheap, never blocking
// ---------------------------------------------------------

void backendInit(TinyGPSPlus* gps)
{
    mainGps = gps;
    if (workQueue) {
        return;
    }

    if (!workQueue) {
        workQueue = xQueueCreate(16, sizeof(BackendWork));
    }

    xTaskCreatePinnedToCore(
        backendTask,
        "backend",
        12288,
        nullptr,
        1,
        nullptr,
        0
    );

    xTaskCreatePinnedToCore(
        healthTask,
        "health",
        12288,
        nullptr,
        1,
        nullptr,
        0
    );
}

void backendLoop(TinyGPSPlus &gps)
{
    static unsigned long gpsTransmissionLastCheck = 0;

    // Mock mode no longer suppresses uploads: fixes are tagged simulated
    // at enqueue time and the map shows them as such.
    const bool mock = gpsMockActive();

    double speedKnots = gps.speed.isValid() ? gps.speed.knots() : 0;
    if (speedKnots < 0) speedKnots = 0;
    unsigned long curInterval = gpsIntervalForSpeed(speedKnots);

    if (millis() - gpsTransmissionLastCheck < curInterval) {
        return;
    }

    gpsTransmissionLastCheck = millis();

    if (!gps.location.isValid()) {
        return;
    }

    BackendWork w = {
        gps.location.lat(),
        gps.location.lng(),
        gps.speed.knots(),
        gps.course.deg(),
        gps.altitude.meters(),
        gps.satellites.value()
    };
    w.simulated = mock;

    enqueueWork(w);
}
