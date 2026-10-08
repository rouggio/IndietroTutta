// Backend configuration
#ifdef WOKWI_SIM
// Simulator: the virtual WiFi (Wokwi-GUEST) has full internet access, so
// prod just works. For a local backend instead, run the Private Wokwi IoT
// Gateway and point these at "http://host.wokwi.internal:3000".
#define BASE_URL        "https://indietrotutta.onrender.com"

// OTA configuration (never triggered in the sim — nothing to flash into)
#define OTA_BASE_URL    "https://indietrotutta.onrender.com/ota"
#else
#define BASE_URL        "https://indietrotutta.onrender.com"

// OTA configuration
#define OTA_BASE_URL    "https://indietrotutta.onrender.com/ota"
#endif
#define BUILD_VERSION   "1.0.133"
