#pragma once

// --------------------------------------------------
// Pin map — ESP32-S3 N16R8 (the only board now).
//
// This used to select between two hardware variants (v1 classic ESP32 in the
// field, v2 the S3) with -DDEVICE_VARIANT_V2. v1 is retired: its GPS receiver
// lives in this box and the board is kept only as a spare. The structure is
// deliberately left as ONE map in one header, so a future variant is a new
// [env:...] block plus its own header, not a refactor.
//
// ESP32-S3 pins that must stay FREE (no conflict allowed):
//   19, 20        native USB D-/D+ pads: driving them kills the USB
//                 console/OTA link (the old buzzer on 19 did exactly that)
//   22..25        do not exist on the S3 (S3 = 0..21, 26..48)
//   26..37        QSPI flash + OPI PSRAM bus
//   0, 3, 45, 46  strapping pins
//   43, 44        UART0 (to the CH343 bridge)
//
// The TFT pins are NOT here: they live in include/User_Setup.h, force-included
// by the buildchain.
// --------------------------------------------------

#define PIN_BTN_LEFT   21   // front button 1
#define PIN_BTN_RIGHT  38   // front button 2
#define PIN_BTN_THIRD  39   // front button 3 (no UI action bound yet)
// front buttons 4/5, should the panel ever need them: 40, 41

#define PIN_GPS_RX     16
#define PIN_GPS_TX     17

#define PIN_SOUND      47   // passive buzzer: NOT 19/20 (USB pads)
