#pragma once

#include <stddef.h>

struct Config
{
    char endpoint[128];

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