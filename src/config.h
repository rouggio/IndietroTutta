// Backend configuration
#define BASE_URL        "https://indietrotutta.onrender.com"

// OTA configuration
//
// The channel is /ota/v2 and MUST STAY: the board in the field is flashed with
// this path in its NVS-less OTA logic, and prod serves the image there. It
// exists because a shared /ota once meant a v2 board could pull the v1
// (classic-ESP32) image - a wrong-chip image it will not boot, with no USB left
// once the enclosure is closed. v1 is retired, but the channel stays.
#define OTA_CHANNEL     "/v2"
#define DEVICE_VARIANT_NAME "v2"

#define OTA_BASE_URL    "https://indietrotutta.onrender.com/ota" OTA_CHANNEL
#define BUILD_VERSION   "1.0.390"
