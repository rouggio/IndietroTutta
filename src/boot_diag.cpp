#include "boot_diag.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#include "serial_buffer.h"

static const char* gResetName = "unknown";
static int gBoots = 0;
static int gPanics = 0;

// esp_reset_reason() must be read BEFORE anything can restart the board again,
// which is why bootDiagInit() is the first thing after Serial.begin().
static const char* reasonName(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_EXT:       return "ext-pin";
        case ESP_RST_SW:        return "software";   // our own ESP.restart()
        case ESP_RST_PANIC:     return "panic";      // abort, assert, stack canary
        case ESP_RST_INT_WDT:   return "int-wdt";
        case ESP_RST_TASK_WDT:  return "task-wdt";
        case ESP_RST_WDT:       return "wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        default:                return "unknown";
    }
}

static bool isCrash(esp_reset_reason_t r)
{
    // A panic and either watchdog mean the firmware died on its own; a
    // brownout does not count here (it is tracked by its own reason string)
    // because it is a power fault, not a crash.
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
           r == ESP_RST_WDT;
}

void bootDiagInit()
{
    const esp_reset_reason_t reason = esp_reset_reason();
    gResetName = reasonName(reason);

    Preferences prefs;
    if (prefs.begin("diag", false)) {
        gBoots = (int)prefs.getUInt("boots", 0) + 1;
        gPanics = (int)prefs.getUInt("panics", 0) + (isCrash(reason) ? 1 : 0);
        prefs.putUInt("boots", (uint32_t)gBoots);
        prefs.putUInt("panics", (uint32_t)gPanics);
        prefs.end();
    }

    // Previous boot's tail first, then our own header: /serial reads as a
    // timeline across the reset, which is the whole point.
    const int prev = serialBufferReplayRtcLog();
    if (prev > 0) {
        bufferedSerialPrintln(String("[BOOT] previous boot tail: ") + prev + " line(s)");
    }
    bufferedSerialPrintln(String("[BOOT] reset=") + gResetName + " boots=" + gBoots +
                          " panics=" + gPanics);
}

const char* bootResetName() { return gResetName; }
int bootCount() { return gBoots; }
int bootPanicCount() { return gPanics; }
