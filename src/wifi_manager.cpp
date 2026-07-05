#include "wifi_manager.h"
#include "config_store.h"
#include "http_server.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <TinyGPSPlus.h>

DNSServer dns;
static Config config;
static bool wifiConfigured = false;
static bool wifiConnectedFlag = false;
static unsigned long lastRetry = 0;
static const unsigned int maxConnectionAttempts = 5;
static unsigned int connectionAttempts = 0;
static bool connectionRetryExhausted = false;

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

    dns.start(53, "*", WiFi.softAPIP());
}

static void startStation()
{
    if (!wifiConfigured)
        return;

    Serial.print("[WiFi] Connecting to ");
    Serial.println(config.ssid);

    WiFi.begin(config.ssid, config.password);
}

void wifiInit(TinyGPSPlus &gps)
{
    Serial.println("[WiFi] Initializing");

    memset(&config, 0, sizeof(config));
    wifiConfigured = loadConfig(config);
    wifiConfigured = wifiConfigured && config.ssid[0] != '\0' && config.password[0] != '\0';
    connectionAttempts = 0;
    connectionRetryExhausted = false;

    startAccessPoint();

    if (wifiConfigured && config.ssid[0] != '\0')
    {
        startStation();
    }

    httpServerInit(gps);
}

void wifiLoop()
{
    dns.processNextRequest();
    httpServerLoop();

    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED)
    {
        if (!wifiConnectedFlag)
        {
            wifiConnectedFlag = true;
            connectionAttempts = 0;
            connectionRetryExhausted = false;

            Serial.println("[WiFi] Connected");
            Serial.print("[WiFi] STA IP: ");
            Serial.println(WiFi.localIP());
        }

        return;
    }

    wifiConnectedFlag = false;

    if (!wifiConfigured || connectionRetryExhausted)
        return;

    if (millis() - lastRetry > 10000)
    {
        lastRetry = millis();
        connectionAttempts++;

        Serial.print("[WiFi] Retry attempt ");
        Serial.print(connectionAttempts);
        Serial.print(" of ");
        Serial.println(maxConnectionAttempts);

        if (connectionAttempts >= maxConnectionAttempts)
        {
            Serial.println("[WiFi] Max retries reached; clearing saved SSID and password");
            memset(config.ssid, 0, sizeof(config.ssid));
            memset(config.password, 0, sizeof(config.password));

            if (!saveConfig(config))
            {
                Serial.println("[WiFi] Failed to persist cleared credentials");
            }
            else
            {
                Serial.println("[WiFi] Saved SSID and password cleared");
            }

            wifiConfigured = false;
            connectionRetryExhausted = true;
            return;
        }

        WiFi.disconnect();
        WiFi.begin(config.ssid, config.password);
    }
}

bool wifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}