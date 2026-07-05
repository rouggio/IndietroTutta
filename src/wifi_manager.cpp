#include "wifi_manager.h"
#include "config_store.h"
#include "http_server.h"

#include <WiFi.h>

static Config config;
static bool wifiConfigured = false;
static bool wifiConnectedFlag = false;
static unsigned long lastRetry = 0;

static void startAccessPoint()
{
    WiFi.mode(WIFI_AP_STA);

    if (!WiFi.softAP("IndietroTutta"))
    {
        Serial.println("[WiFi] Failed to start AP");
        return;
    }

    Serial.print("[WiFi] AP IP: ");
    Serial.println(WiFi.softAPIP());
}

static void startStation()
{
    if (!wifiConfigured)
        return;

    Serial.print("[WiFi] Connecting to ");
    Serial.println(config.ssid);

    WiFi.begin(config.ssid, config.password);
}

void wifiInit()
{
    Serial.println("[WiFi] Initializing");

    memset(&config, 0, sizeof(config));
    wifiConfigured = loadConfig(config);

    startAccessPoint();

    if (wifiConfigured && config.ssid[0] != '\0')
    {
        startStation();
    }

    httpServerInit();
}

void wifiLoop()
{
    httpServerLoop();

    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED)
    {
        if (!wifiConnectedFlag)
        {
            wifiConnectedFlag = true;

            Serial.println("[WiFi] Connected");
            Serial.print("[WiFi] STA IP: ");
            Serial.println(WiFi.localIP());
        }

        return;
    }

    wifiConnectedFlag = false;

    if (!wifiConfigured)
        return;

    if (millis() - lastRetry > 10000)
    {
        lastRetry = millis();

        Serial.println("[WiFi] Retrying...");

        WiFi.disconnect();
        WiFi.begin(config.ssid, config.password);
    }
}

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}