#include "config.h"
#include "backend.h"
#include "config_store.h"
#include "serial_buffer.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#define HEALTH_URL BASE_URL "/health"
#define GPS_URL    BASE_URL "/gps"

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
constexpr unsigned long GPS_TRANSMISSION_INTERVAL = 40000;
constexpr unsigned long TASK_TICK_MS = 250;

struct BackendWork {
    double lat;
    double lon;
    double speed;
    double course;
    double altitude;
    int sats;
    bool flagged;
};

static QueueHandle_t workQueue = nullptr;
static volatile bool online = false;

bool backendOnline()
{
    return online;
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

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (http.begin(client, HEALTH_URL)) {
        http.addHeader("DeviceId", String(WiFi.macAddress()));

        if (config.username[0] != '\0') {
            http.addHeader("Username", String(config.username));
        }

        online = (http.GET() == HTTP_CODE_OK);
        http.end();
    }
    else {
        online = false;
    }
}

static bool sendPosition(const BackendWork &w)
{
    if (WiFi.status() != WL_CONNECTED) {
        online = false;
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, GPS_URL)) {
        online = false;
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("DeviceId", String(WiFi.macAddress()));

    String body = "{";
    body += "\"lat\":" + String(w.lat, 7);
    body += ",\"lon\":" + String(w.lon, 7);
    body += ",\"speed\":" + String(w.speed, 1);
    body += ",\"course\":" + String(w.course, 1);
    body += ",\"altitude\":" + String(w.altitude, 1);
    body += ",\"sats\":" + String(w.sats);
    body += ",\"flagged\":" + String(w.flagged ? "true" : "false");

    if (config.username[0] != '\0') {
        body += ",\"username\":\"" + String(config.username) + "\"";
    }

    body += "}";

    int code = http.POST(body);

    http.end();

    if (code == HTTP_CODE_OK) {
        online = true;
        return true;
    }

    online = false;
    return false;
}

static void backendTask(void *param)
{
    (void)param;

    unsigned long lastHealthCheck = 0;
    BackendWork w;

    for (;;) {
        bool haveItem =
            xQueueReceive(workQueue, &w, pdMS_TO_TICKS(TASK_TICK_MS)) == pdTRUE;

        unsigned long now = millis();

        if (now - lastHealthCheck >= HEALTH_CHECK_INTERVAL) {
            lastHealthCheck = now;
            healthCheck();
        }

        if (haveItem) {
            const bool sent = sendPosition(w);

            bufferedSerialPrintln(
                sent ? "[BACKEND] Position sent" :
                       "[BACKEND] Position send failed"
            );
        }
    }
}

// ---------------------------------------------------------
// UI-thread side: cheap, never blocking
// ---------------------------------------------------------

void backendInit()
{
    if (workQueue) {
        return;
    }

    workQueue = xQueueCreate(4, sizeof(BackendWork));

    xTaskCreatePinnedToCore(
        backendTask,
        "backend",
        12288,
        nullptr,
        1,
        nullptr,
        0
    );
}

bool backendSendFlaggedPosition(TinyGPSPlus &gps)
{
    if (!gps.location.isValid()) {
        bufferedSerialPrintln("[GPS] Cannot flag position: no valid GPS fix");
        return false;
    }

    BackendWork w = {
        gps.location.lat(),
        gps.location.lng(),
        gps.speed.knots(),
        gps.course.deg(),
        gps.altitude.meters(),
        gps.satellites.value(),
        true
    };

    const bool queued = enqueueWork(w);

    bufferedSerialPrintln(
        queued ? "[BACKEND] Flagged position queued" :
                 "[BACKEND] Cannot flag position: queue unavailable"
    );

    return queued;
}

void backendLoop(TinyGPSPlus &gps)
{
    static unsigned long gpsTransmissionLastCheck = 0;

    if (millis() - gpsTransmissionLastCheck < GPS_TRANSMISSION_INTERVAL) {
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
        gps.satellites.value(),
        false
    };

    enqueueWork(w);
}
