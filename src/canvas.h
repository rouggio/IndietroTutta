#pragma once
// Global drawing target.
//
// Normally points at the hardware panel, in which case `tft` is just the
// real TFT_eSPI instance. The /screen capture temporarily redirects it at
// an off-screen TFT_eSprite so the current page can be re-rendered and read
// back — the ST7789 panel is write-only, so TFT_eSPI cannot read it (the old
// tft.readPixel() grab always returned 0/black). A sprite buffer is the only
// surface TFT_eSPI can read.
#include <TFT_eSPI.h>
#include <string.h>

extern TFT_eSPI* gCanvas;
// True while a screenshot is being rendered into the off-screen sprite:
// screens use it to skip side effects (e.g. resetting the race run) that
// must not fire on a capture.
extern bool gGrabbing;

#define tft (*gCanvas)

// pushImage() is NOT virtual in TFT_eSPI, so a call through the TFT_eSPI
// reference would bypass the sprite and write straight to the panel (leaving
// the SPI transaction open -> hang). During a capture blit pixel-by-pixel
// through the virtual drawPixel() instead. On the panel the library's fast
// path is kept.
inline void canvasPushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                            const uint16_t* data) {
    if (!gGrabbing) { tft.pushImage(x, y, w, h, data); return; }
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++)
            tft.drawPixel(x + i, y + j, data[j * w + i]);
}

inline void canvasPushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                            const uint16_t* data, uint16_t transparent) {
    if (!gGrabbing) { tft.pushImage(x, y, w, h, data, transparent); return; }
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++) {
            const uint16_t c = data[j * w + i];
            if (c == transparent) continue;
            tft.drawPixel(x + i, y + j, c);
        }
}

// Minimal GLCD (font 1, 5x8) string renderer. TFT_eSPI's font-1 draw path
// faults intermittently when the target is a TFT_eSprite, so small labels
// are drawn here through the virtual drawPixel() (works on panel + sprite).
// `font` (the GLCD bitmap) comes from TFT_eSPI.h.
inline void drawLabel1(int32_t x, int32_t y, const char* s, uint16_t color) {
    for (; s && *s; ++s) {
        const uint8_t c = (uint8_t)*s;
        if (c >= 32) {
            for (int8_t i = 0; i < 5; i++) {
                uint8_t line = pgm_read_byte(font + (c * 5) + i);
                for (int8_t j = 0; j < 8; j++) {
                    if (line & 0x1) tft.drawPixel(x + i, y + j, color);
                    line >>= 1;
                }
            }
        }
        x += 6;
    }
}

inline void drawLabel1C(int32_t cx, int32_t y, const char* s, uint16_t color) {
    const int32_t w = 6 * (int32_t)strlen(s ? s : "");
    drawLabel1(cx - w / 2, y, s, color);
}
