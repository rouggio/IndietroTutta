#include <Preferences.h>
#include <cstring>

#include "config_store.h"

Config config;

// ---------------------------------------------------------
// General configuration
// ---------------------------------------------------------

bool loadConfig(Config& config)
{
    Preferences prefs;

    if (!prefs.begin("wifi", false))
    {
        config = {};
        return false;
    }

    size_t storedSize = prefs.getBytesLength("cfg");

    // No configuration or incompatible old configuration
    if (storedSize != sizeof(config))
    {
        if (storedSize > 0)
        {
            prefs.remove("cfg");
        }

        config = {};

        prefs.end();
        return false;
    }

    size_t len = prefs.getBytes(
        "cfg",
        &config,
        sizeof(config)
    );

    prefs.end();

    return len == sizeof(config);
}

bool saveConfig(const Config& config)
{
    Preferences prefs;

    if (!prefs.begin("wifi", false))
    {
        return false;
    }

    size_t len = prefs.putBytes(
        "cfg",
        &config,
        sizeof(config)
    );

    prefs.end();

    return len == sizeof(config);
}

// ---------------------------------------------------------
// WiFi network storage
// ---------------------------------------------------------

int wifiNetworkCount()
{
    Preferences prefs;

    if (!prefs.begin("wifi", true))
    {
        return 0;
    }

    int count = prefs.getInt("wifi_count", 0);

    prefs.end();

    if (count < 0)
        count = 0;

    if (count > MAX_WIFI_NETWORKS)
        count = MAX_WIFI_NETWORKS;

    return count;
}

// ---------------------------------------------------------

bool loadWiFiNetwork(
    int index,
    char* ssid,
    size_t ssidSize,
    char* password,
    size_t passwordSize)
{
    if (index < 0 || index >= MAX_WIFI_NETWORKS)
        return false;

    if (!ssid || !password || ssidSize == 0 || passwordSize == 0)
        return false;

    ssid[0] = '\0';
    password[0] = '\0';

    Preferences prefs;

    if (!prefs.begin("wifi", true))
        return false;

    int count = prefs.getInt("wifi_count", 0);

    if (index >= count)
    {
        prefs.end();
        return false;
    }

    char ssidKey[16];
    char passKey[16];

    snprintf(
        ssidKey,
        sizeof(ssidKey),
        "wifi_%d_ssid",
        index
    );

    snprintf(
        passKey,
        sizeof(passKey),
        "wifi_%d_pass",
        index
    );

    size_t ssidLen = prefs.getString(
        ssidKey,
        ssid,
        ssidSize
    );

    size_t passLen = prefs.getString(
        passKey,
        password,
        passwordSize
    );

    prefs.end();

    if (ssidLen == 0)
    {
        ssid[0] = '\0';
        password[0] = '\0';
        return false;
    }

    return true;
}

// ---------------------------------------------------------

bool saveWiFiNetwork(
    int index,
    const char* ssid,
    const char* password)
{
    if (index < 0 || index >= MAX_WIFI_NETWORKS)
        return false;

    if (!ssid || !password || ssid[0] == '\0')
        return false;

    Preferences prefs;

    if (!prefs.begin("wifi", false))
        return false;

    char ssidKey[16];
    char passKey[16];

    snprintf(
        ssidKey,
        sizeof(ssidKey),
        "wifi_%d_ssid",
        index
    );

    snprintf(
        passKey,
        sizeof(passKey),
        "wifi_%d_pass",
        index
    );

    bool ok = true;

    if (prefs.putString(ssidKey, ssid) == 0)
        ok = false;

    if (prefs.putString(passKey, password) == 0)
        ok = false;

    int count = prefs.getInt("wifi_count", 0);

    if (index >= count)
    {
        prefs.putInt("wifi_count", index + 1);
    }

    prefs.end();

    return ok;
}

// ---------------------------------------------------------

bool deleteWiFiNetwork(int index)
{
    int count = wifiNetworkCount();

    if (index < 0 || index >= count)
        return false;

    Preferences prefs;

    if (!prefs.begin("wifi", false))
        return false;

    // Shift subsequent networks down
    for (int i = index; i < count - 1; i++)
    {
        char currentSsidKey[16];
        char currentPassKey[16];
        char nextSsidKey[16];
        char nextPassKey[16];

        snprintf(
            currentSsidKey,
            sizeof(currentSsidKey),
            "wifi_%d_ssid",
            i
        );

        snprintf(
            currentPassKey,
            sizeof(currentPassKey),
            "wifi_%d_pass",
            i
        );

        snprintf(
            nextSsidKey,
            sizeof(nextSsidKey),
            "wifi_%d_ssid",
            i + 1
        );

        snprintf(
            nextPassKey,
            sizeof(nextPassKey),
            "wifi_%d_pass",
            i + 1
        );

        String nextSSID = prefs.getString(nextSsidKey, "");
        String nextPassword = prefs.getString(nextPassKey, "");

        prefs.putString(currentSsidKey, nextSSID);
        prefs.putString(currentPassKey, nextPassword);
    }

    char lastSsidKey[16];
    char lastPassKey[16];

    snprintf(
        lastSsidKey,
        sizeof(lastSsidKey),
        "wifi_%d_ssid",
        count - 1
    );

    snprintf(
        lastPassKey,
        sizeof(lastPassKey),
        "wifi_%d_pass",
        count - 1
    );

    prefs.remove(lastSsidKey);
    prefs.remove(lastPassKey);

    prefs.putInt("wifi_count", count - 1);

    prefs.end();

    return true;
}

// ---------------------------------------------------------

void clearWiFiNetworks()
{
    Preferences prefs;

    if (!prefs.begin("wifi", false))
        return;

    int count = prefs.getInt("wifi_count", 0);

    if (count < 0)
        count = 0;

    if (count > MAX_WIFI_NETWORKS)
        count = MAX_WIFI_NETWORKS;

    for (int i = 0; i < count; i++)
    {
        char ssidKey[16];
        char passKey[16];

        snprintf(
            ssidKey,
            sizeof(ssidKey),
            "wifi_%d_ssid",
            i
        );

        snprintf(
            passKey,
            sizeof(passKey),
            "wifi_%d_pass",
            i
        );

        prefs.remove(ssidKey);
        prefs.remove(passKey);
    }

    prefs.putInt("wifi_count", 0);

    prefs.end();
}