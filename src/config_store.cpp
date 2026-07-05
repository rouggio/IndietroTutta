#include <Preferences.h>

#include "config_store.h"

bool loadConfig(Config& config) {
    Preferences prefs;
    if (!prefs.begin("wifi", true)) {
        return false; // Failed to open preferences
    }

    size_t len = prefs.getBytes("cfg", &config, sizeof(config));
    prefs.end();

    if (len != sizeof(config)) {
        config.timezoneOffsetHours = 0;
    }

    return len == sizeof(config);
}

bool saveConfig(const Config& config) {
    Preferences prefs;
    if (!prefs.begin("wifi", false)) {
        return false; // Failed to open preferences
    }
    size_t len = prefs.putBytes("cfg", &config, sizeof(config));
    prefs.end();
    return len == sizeof(config);
}