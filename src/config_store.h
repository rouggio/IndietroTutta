#pragma once

#include <stddef.h>

constexpr size_t MAX_USERNAME_LEN = 32;

// 0 = knots, 1 = km/h, 2 = mph
constexpr int SPEED_UNITS = 3;

struct Config
{
    // Display name shown on the map, transmitted to the backend
    // alongside the DeviceId (MAC address)
    char username[MAX_USERNAME_LEN + 1];

    int timezoneOffsetHours;
    int speedUnit;

    bool otaCheckOnStart;
};

extern Config config;

// Upwind no-go arc width (total degrees) shown on the main-screen ring.
// Own NVS key ("wifi"/"nogo", default 60) — NOT inside the `cfg` blob, so
// old installs never lose their config when this field appears.
int getNoGoDeg();
bool setNoGoDeg(int deg);       // clamps to 10..180, persists, updates the live copy
extern int noGoArcDeg;          // live copy used by the UI

bool loadConfig(Config& config);
bool saveConfig(const Config& config);

// ---------------------------------------------------------
// WiFi networks
// ---------------------------------------------------------

constexpr int MAX_WIFI_NETWORKS = 10;

int wifiNetworkCount();

bool loadWiFiNetwork(
    int index,
    char* ssid,
    size_t ssidSize,
    char* password,
    size_t passwordSize
);

bool saveWiFiNetwork(
    int index,
    const char* ssid,
    const char* password
);

bool deleteWiFiNetwork(int index);

void clearWiFiNetworks();