#pragma once
// Drawing-target dispatcher: every screen draws through `tft`, which is this
// object, not a TFT_eSPI reference.
//
// Why not `TFT_eSPI* gCanvas` any more: that indirection only redirected the
// methods TFT_eSPI declares virtual. Everything else ran on the *panel*
// implementation even while the pointer was aimed at an off-screen sprite —
// pushImage did exactly that, which is why canvasPushImage() existed.
//
// A /screen band also needs something a sprite cannot do: a **y translation**.
// A sprite has no viewport (Sprite.h has no _vpX/_vpY) and simply rejects
// coordinates outside its own box, so a 320x40 band could never receive page
// coordinates. With an explicit dispatcher the band gets `y - gCaptureY`, and
// — the real point — a drawing method that is not forwarded here is a COMPILE
// error, not a quietly incomplete grab.
//
// Targets: the hardware panel always, plus (during a capture) one band sprite
// that receives the same drawing shifted up by gCaptureY.
#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>

extern TFT_eSPI*    gPanel;    // the hardware panel
// True while a screenshot is being rendered into a band sprite: screens use it
// to skip side effects (e.g. resetting the race run) that must not fire on a
// capture.
extern bool         gGrabbing;
extern TFT_eSprite* gCapture;  // band sprite being captured into, else nullptr
extern int32_t      gCaptureY; // page y of the band's first row

struct Canvas {
    // ---- geometry: the PAGE, never the band (page layout must not change) ----
    int16_t  width()  const { return gPanel->width(); }
    int16_t  height() const { return gPanel->height(); }
    uint16_t color565(uint8_t r, uint8_t g, uint8_t b) const {
        return gPanel->color565(r, g, b);
    }

    // ---- panel lifecycle (boot only, never during a capture) ----
    void init()                const { gPanel->init(); }
    void setRotation(uint8_t r) const { gPanel->setRotation(r); }
    void invertDisplay(bool i) const { gPanel->invertDisplay(i); }

    // ---- text state: mirrored onto BOTH targets ----
    // The screens set datum/colour, then draw; each target must hold the same
    // state or the band renders its text in the wrong place.
    void setTextDatum(uint8_t d) {
        mDatum = d;
        gPanel->setTextDatum(d);
        if (gCapture) gCapture->setTextDatum(d);
    }
    uint8_t getTextDatum() const { return mDatum; }
    void setTextFont(uint8_t f) {
        gPanel->setTextFont(f);
        if (gCapture) gCapture->setTextFont(f);
    }
    void setTextColor(uint32_t fg) {
        gPanel->setTextColor(fg);
        if (gCapture) gCapture->setTextColor(fg);
    }
    void setTextColor(uint32_t fg, uint32_t bg) {
        gPanel->setTextColor(fg, bg);
        if (gCapture) gCapture->setTextColor(fg, bg);
    }

    int16_t textWidth(const char* s, uint8_t font) const {
        if (!s) return 0;
        if (gCapture) { gCapture->setTextFont(font); return gCapture->textWidth(s); }
        return gPanel->textWidth(s, font);
    }
    int16_t textWidth(const String& s, uint8_t font) const {
        return textWidth(s.c_str(), font);
    }
    int16_t textWidth(const char* s) const {
        if (!s) return 0;
        return gCapture ? gCapture->textWidth(s) : gPanel->textWidth(s);
    }

    // drawString returns the printed width here, and the race pane uses it for
    // its erase bookkeeping — so the value must come from the target that drew.
    int16_t drawString(const char* s, int32_t x, int32_t y, uint8_t font) const {
        if (!s) return 0;
        if (gCapture) {
            gCapture->setTextFont(font);
            return gCapture->drawString(s, x, y - gCaptureY);
        }
        return gPanel->drawString(s, x, y, font);
    }
    int16_t drawString(const String& s, int32_t x, int32_t y, uint8_t font) const {
        return drawString(s.c_str(), x, y, font);
    }
    int16_t drawString(const char* s, int32_t x, int32_t y) const {
        if (!s) return 0;
        if (gCapture) return gCapture->drawString(s, x, y - gCaptureY);
        return gPanel->drawString(s, x, y);
    }
    int16_t drawString(const String& s, int32_t x, int32_t y) const {
        return drawString(s.c_str(), x, y);
    }

    // ---- primitives ----
    // The band sprite clips per pixel against its own box, so translated
    // coordinates (negative y included) are safe for the rasterising calls.
    void drawPixel(int32_t x, int32_t y, uint32_t c) const {
        if (gCapture) gCapture->drawPixel(x, y - gCaptureY, c);
        else          gPanel->drawPixel(x, y, c);
    }
    void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) const {
        if (gCapture) gCapture->drawFastHLine(x, y - gCaptureY, w, c);
        else          gPanel->drawFastHLine(x, y, w, c);
    }
    void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) const {
        if (gCapture) gCapture->drawFastVLine(x, y - gCaptureY, h, c);
        else          gPanel->drawFastVLine(x, y, h, c);
    }
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) const {
        if (gCapture) gCapture->drawLine(x0, y0 - gCaptureY, x1, y1 - gCaptureY, c);
        else          gPanel->drawLine(x0, y0, x1, y1, c);
    }
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) const {
        if (gCapture) gCapture->drawRect(x, y - gCaptureY, w, h, c);
        else          gPanel->drawRect(x, y, w, h, c);
    }
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) const {
        if (!gCapture) { gPanel->fillRect(x, y, w, h, c); return; }
        // rectangles are the one shape worth clipping here, so a band boundary
        // through a big wipe cannot depend on the sprite's own clamping
        int32_t y0 = y, y1 = y + h;
        const int32_t bandTop = gCaptureY, bandBot = gCaptureY + gCapture->height();
        if (y0 < bandTop) y0 = bandTop;
        if (y1 > bandBot) y1 = bandBot;
        if (y1 <= y0) return;
        gCapture->fillRect(x, y0 - bandTop, w, y1 - y0, c);
    }
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) const {
        if (gCapture) gCapture->drawRoundRect(x, y - gCaptureY, w, h, r, c);
        else          gPanel->drawRoundRect(x, y, w, h, r, c);
    }
    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) const {
        if (gCapture) gCapture->fillRoundRect(x, y - gCaptureY, w, h, r, c);
        else          gPanel->fillRoundRect(x, y, w, h, r, c);
    }
    void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) const {
        if (gCapture) gCapture->drawCircle(x, y - gCaptureY, r, c);
        else          gPanel->drawCircle(x, y, r, c);
    }
    void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) const {
        if (gCapture) gCapture->fillCircle(x, y - gCaptureY, r, c);
        else          gPanel->fillCircle(x, y, r, c);
    }
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint32_t c) const {
        if (gCapture) gCapture->fillTriangle(x0, y0 - gCaptureY, x1, y1 - gCaptureY,
                                             x2, y2 - gCaptureY, c);
        else          gPanel->fillTriangle(x0, y0, x1, y1, x2, y2, c);
    }
    // A sprite has no drawWideLine (the library implements it with read-back
    // and image pushes), so the band rasterises it as parallel 1px lines. Only
    // map legs use it; a grab is a debug picture, not an instrument.
    void drawWideLine(float ax, float ay, float bx, float by, float wd,
                      uint32_t fg, uint32_t bgc = 0x00FFFFFF) const {
        if (!gCapture) { gPanel->drawWideLine(ax, ay, bx, by, wd, fg, bgc); return; }
        const float dx = bx - ax, dy = by - ay;
        const float len = sqrtf(dx * dx + dy * dy);
        const int   n   = (int)(wd + 0.5f);
        if (len < 0.5f || n < 1) { drawPixel((int32_t)ax, (int32_t)ay, fg); return; }
        const float nx = -dy / len, ny = dx / len;
        for (int i = 0; i < n; i++) {
            const float off = (float)i - (n - 1) * 0.5f;
            gCapture->drawLine((int32_t)(ax + nx * off), (int32_t)(ay + ny * off) - gCaptureY,
                               (int32_t)(bx + nx * off), (int32_t)(by + ny * off) - gCaptureY, fg);
        }
    }
    // fillScreen is a page-level clear (boot / page switch / mode toggle).
    // During a capture it clears the band's own rows only.
    void fillScreen(uint32_t c) const {
        if (gCapture) gCapture->fillRect(0, 0, gCapture->width(), gCapture->height(), c);
        else          gPanel->fillScreen(c);
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bmp, int16_t w, int16_t h,
                    uint16_t fg) const {
        bitmap(x, y, bmp, w, h, fg, 0, false);
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t* bmp, int16_t w, int16_t h,
                    uint16_t fg, uint16_t bg) const {
        bitmap(x, y, bmp, w, h, fg, bg, true);
    }
    // pushImage() is NOT virtual in TFT_eSPI, so through a TFT_eSPI reference it
    // always wrote to the panel (and left the SPI transaction open -> hang).
    // Here both targets are explicit.
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) const {
        if (!gCapture) { gPanel->pushImage(x, y, w, h, const_cast<uint16_t*>(data)); return; }
        for (int32_t j = 0; j < h; j++)
            for (int32_t i = 0; i < w; i++)
                gCapture->drawPixel(x + i, y + j - gCaptureY, data[j * w + i]);
    }
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data,
                   uint16_t transparent) const {
        if (!gCapture) {
            gPanel->pushImage(x, y, w, h, const_cast<uint16_t*>(data), transparent);
            return;
        }
        for (int32_t j = 0; j < h; j++)
            for (int32_t i = 0; i < w; i++) {
                const uint16_t c = data[j * w + i];
                if (c == transparent) continue;
                gCapture->drawPixel(x + i, y + j - gCaptureY, c);
            }
    }

private:
    uint8_t mDatum = TL_DATUM;

    // 1bpp MSB-first rows padded to whole bytes, like TFT_eSPI's drawBitmap.
    void bitmap(int16_t x, int16_t y, const uint8_t* bmp, int16_t w, int16_t h,
                uint16_t fg, uint16_t bg, bool useBg) const {
        if (!bmp) return;
        const bool cap = (gCapture != nullptr);
        if (!cap) {
            if (useBg) gPanel->drawBitmap(x, y, bmp, w, h, fg, bg);
            else       gPanel->drawBitmap(x, y, bmp, w, h, fg);
            return;
        }
        const int bytesPerRow = (w + 7) / 8;
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                const uint8_t byte = bmp[j * bytesPerRow + (i >> 3)];
                const bool on = (byte & (0x80 >> (i & 7))) != 0;
                if (on)          gCapture->drawPixel(x + i, y + j - gCaptureY, fg);
                else if (useBg)  gCapture->drawPixel(x + i, y + j - gCaptureY, bg);
            }
        }
    }
};

extern Canvas tftCanvas;
// The screens draw on `tft`; it is the dispatcher, never a raw TFT_eSPI.
#define tft tftCanvas

// The screens call this in five places. It used to work around the non-virtual
// pushImage(); the dispatcher covers both targets now, so it is a plain forward
// kept for the call sites (and for the name in the docs).
inline void canvasPushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                            const uint16_t* data) {
    tft.pushImage(x, y, w, h, data);
}

inline void canvasPushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                            const uint16_t* data, uint16_t transparent) {
    tft.pushImage(x, y, w, h, data, transparent);
}

// Minimal GLCD (font 1, 5x8) string renderer. TFT_eSPI's font-1 draw path
// faults intermittently when the target is a TFT_eSprite, so small labels
// are drawn here through drawPixel() (works on panel + sprite).
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
