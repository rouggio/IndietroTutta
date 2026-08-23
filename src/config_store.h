#pragma once

#include <stddef.h>

constexpr size_t MAX_USERNAME_LEN = 32;

struct Config
{
    // Display name shown on the map, transmitted to the backend
    // alongside the DeviceId (MAC address)
    char username[MAX_USERNAME_LEN + 1];

    int timezoneOffsetHours;

    bool otaCheckOnStart;
};

extern Config config;

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