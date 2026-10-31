// Backend configuration
#define BASE_URL        "https://indietrotutta.onrender.com"

// OTA configuration
//
// CHANNEL SPLIT (2026-10-31): v1 boards publish/pull from /ota, v2 (ESP32-S3)
// from /ota/v2. One shared channel is a brick risk: a v2 board would pull the
// v1 (esp32dev) image, which the S3 refuses to boot - and once the box is
// sealed there is no USB to recover it with. The variant is the buildchain's
// -DDEVICE_VARIANT_V2 (see include/board_pins.h).
#if defined(DEVICE_VARIANT_V2)
#define OTA_CHANNEL     "/v2"
#define DEVICE_VARIANT_NAME "v2"
#else
#define OTA_CHANNEL     ""
#define DEVICE_VARIANT_NAME "v1"
#endif

#define OTA_BASE_URL    "https://indietrotutta.onrender.com/ota" OTA_CHANNEL
#define BUILD_VERSION   "1.0.378"
