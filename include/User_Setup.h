// --------------------------------------------------
// Display config — ESP32-S3 N16R8 + 3.5" ST7796S (480x320 landscape).
//
// Force-included by the (single) build env. There is one board now, so this is
// just "the display header"; when a second variant appears it gets its own file
// and its own [env:] block.
//
// PIN CHOICE — the S3 has no GPIO 22-25, so the old v1 MOSI 23 does not exist
// here (TFT_eSPI drove a non-existent pin: dark panel + "Invalid pin selected"
// at boot). These are the SPI2/FSPI native pins, so no GPIO-matrix gymnastics:
//   CS   10
//   MOSI 11
//   SCLK 12
// with DC 2 and RST 4 (both valid on the S3). Unusable anywhere on this board:
// 19/20 (native USB), 22-25 (absent), 26-37 (QSPI flash + OPI PSRAM), 43/44
// (UART0 to the CH343 bridge); avoid 0/3/45/46 (strapping).
//
// Backlight: no TFT_BL on purpose — the panel's LED/BL pin is tied to 3.3 V and
// the firmware cannot dim or switch it. Give it a GPIO + MOSFET if that ever
// matters, and wire it before the lid closes.
//
// PENDING: the layout is still written against a 320x240 frame (screen_race.cpp
// MAP_W = 320, CELL_EDGE {0,107,213,320}; screen_speed.cpp has its own cells),
// so on this 480x320 panel the UI paints into the top-left corner until the
// port lands. Driver, geometry and clock are correct so the panel can be seen.
// --------------------------------------------------

#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS    10   // Chip select control pin
#define TFT_DC    2    // Data Command control pin
#define TFT_RST   4    // Reset pin (could connect to RST pin)

// Landscape: 0/2 are portrait (320x480), 1/3 landscape (480x320). This panel
// comes up upside down at 3, so 1 it is.
#define TFT_ROTATION 1

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts
#define SMOOTH_FONT

// 40 MHz, verified clean on the bench (single stray pixels or a torn band are
// the failure mode of a marginal clock, not a black screen). 80 is available
// later: a full 480x320 frame is 307 KB, ~61 ms here vs ~31 ms there.
#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
