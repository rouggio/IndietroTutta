#pragma once

struct Config {
    char ssid[33];
    char password[65];
    char endpoint[128];
    int timezoneOffsetHours;
};

bool loadConfig(Config& config);
bool saveConfig(const Config& config);