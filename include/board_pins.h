#pragma once

// --------------------------------------------------
// Per-variant pin map.
//
// One shared src/ for two hardware variants; the buildchain picks the map:
//   v1 = classic ESP32  ("esp32dev" env, the device in the field)
//   v2 = ESP32-S3 N16R8 ("esp32s3" env, -DDEVICE_VARIANT_V2)
//
// ESP32-S3 N16R8 pins that must stay FREE (no conflict allowed):
//   19, 20        native USB D-/D+ pads: driving them kills the USB
//                 console/OTA link (the v1 buzzer on 19 did exactly that)
//   22..25        do not exist on the S3 (S3 = 0..21, 26..48)
//   26..37        QSPI flash + OPI PSRAM bus
//   0, 3, 45, 46  strapping pins
//   43, 44        UART0 (ROM console)
//
// The TFT pins are NOT here: they live in include/User_Setup.h, which the
// buildchain force-includes (-include). v2 keeps the v1 display wiring
// (CS5 DC2 RST4 MOSI23 SCLK18 - all valid and conflict-free on the S3)
// until the 3.5" 320x480 ST7796S panel is wired and gets its own setup
// header.
// --------------------------------------------------

#if defined(DEVICE_VARIANT_V2)

  #define PIN_BTN_LEFT   21   // front button 1
  #define PIN_BTN_RIGHT  38   // front button 2
  #define PIN_BTN_THIRD  39   // front button 3 (v2 adds one; no UI action bound yet)
  // front buttons 4/5, should the panel ever need them: 40, 41

  #define PIN_GPS_RX     16
  #define PIN_GPS_TX     17

  #define PIN_SOUND      47   // passive buzzer: NOT 19/20 (USB pads)

#else   // v1 - classic ESP32, leave the field wiring untouched

  #define PIN_BTN_LEFT   21
  #define PIN_BTN_RIGHT  22
  #define PIN_BTN_THIRD  (-1) // v1 has two buttons; -1 = not present

  #define PIN_GPS_RX     16
  #define PIN_GPS_TX     17

  #define PIN_SOUND      19

#endif
