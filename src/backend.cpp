#define BASE_URL   "https://indietrotutta.onrender.com"
#define HEALTH_URL BASE_URL "/health"
#define GPS_URL    BASE_URL "/gps"

#include "backend.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>

static bool online = false;
static unsigned long healthLastCheck = 0;
static unsigned long gpsTransmissionLastCheck = 0;

constexpr unsigned long HEALTH_CHECK_INTERVAL = 30000;
constexpr unsigned long GPS_TRANSMISSION_INTERVAL = 40000;

bool backendOnline()
{
    return online;
}

bool backendSendPosition(
    double lat,
    double lon,
    double speed,
    double course,
    double altitude,
    int sats)
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, GPS_URL))
        return false;

    http.addHeader("Content-Type", "application/json");

    String body = "{";
    body += "\"lat\":" + String(lat, 7);
    body += ",\"lon\":" + String(lon, 7);
    body += ",\"speed\":" + String(speed, 1);
    body += ",\"course\":" + String(course, 1);
    body += ",\"altitude\":" + String(altitude, 1);
    body += ",\"sats\":" + String(sats);
    body += "}";

    int code = http.POST(body);

    http.end();

    if (code == HTTP_CODE_OK)
    {
        online = true;
        return true;
    }

    online = false;
    return false;
}

void backendInit()
{
    healthLastCheck = 0;
    gpsTransmissionLastCheck = 0;
}

void healthCheckLoop()
{
    if (millis() - healthLastCheck < HEALTH_CHECK_INTERVAL)
        return;

    healthLastCheck = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
        online = false;
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (http.begin(client, HEALTH_URL))
    {
        online = (http.GET() == HTTP_CODE_OK);
        http.end();
    }
    else
    {
        online = false;
    }
}

void gpsTransmissionLoop(TinyGPSPlus &gps)
{
    if (millis() - gpsTransmissionLastCheck < GPS_TRANSMISSION_INTERVAL)
        return;

    gpsTransmissionLastCheck = millis();

    backendSendPosition(
        gps.location.lat(), 
        gps.location.lng(), 
        gps.speed.knots(), 
        gps.course.deg(), 
        gps.altitude.meters(), 
        gps.satellites.value()
    );
}

void backendLoop(TinyGPSPlus &gps)
{
    healthCheckLoop();
    gpsTransmissionLoop(gps);
}
