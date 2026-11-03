#include <TFT_eSPI.h>

#include "hint_bar.h"
#include "canvas.h"

#define BG TFT_BLACK
#define WHITE TFT_WHITE

static const int GRID_GRAY = 0x7BEF;

// What each cell currently shows, so hintBarCell() can be called from the
// 200 ms path without repainting static text five times a second.
struct HintCell {
    String   top;
    String   bottom;
    uint16_t topColor;
    uint16_t bottomColor;
    bool     valid;
};
static HintCell cells[HINT_CELLS];

// Draw `word` turned 90 deg clockwise with its top-left corner at (x, y): the
// box is 16 px wide and 8 px per glyph tall.
//
// The glyph pixels come from a throwaway 16-bit sprite (font 2, one 8x16 cell
// at a time) which we re-map ourselves, so the result is a plain image and goes
// out through pushImage — the path the /screen capture already understands.
// Drawing rotated text directly is not an option: TFT_eSPI rotates per display,
// not per string, and pushRotated() is non-virtual, i.e. it would bypass the
// capture target and write to the panel mid-grab.
static void drawSpineWord(int x, int y, const char *word, uint16_t fg)
{
  static const int BOX_W = 16;          // = one font-2 glyph height
  static const int MAX_CH = 12;         // longest label we will ever print
  static uint16_t buf[BOX_W * 8 * MAX_CH];
  static TFT_eSprite glyph(gPanel);
  static bool glyphReady = false;
  if (!glyphReady) {
    glyph.setColorDepth(16);
    glyphReady = (glyph.createSprite(8, 16) != nullptr);
  }
  if (!glyphReady || !word) return;
  const int n = (int)strlen(word);
  if (n < 1 || n > MAX_CH) return;

  const int boxH = n * 8;
  for (int i = 0; i < BOX_W * boxH; i++) buf[i] = BG;

  for (int k = 0; k < n; k++) {
    glyph.fillSprite(BG);
    glyph.setTextDatum(MC_DATUM);
    glyph.setTextColor(fg, BG);
    glyph.drawString(String(word[k]), 4, 8, 2);
    for (int sy = 0; sy < 16; sy++)
      for (int sx = 0; sx < 8; sx++) {
        // The glyph comes back as ordinary RGB565 from readPixel(); the swap
        // that the panel needs happens once, on the whole box, before the push.
        const uint16_t c = glyph.readPixel(sx, sy);
        if (c != BG) {
          // 90 deg clockwise: the glyph's top-left corner lands top-right.
          buf[(k * 8 + sx) * BOX_W + (BOX_W - 1 - sy)] = c;
        }
      }
  }
  // pushImage does NOT take host-order pixels on this panel: a host-order buffer
  // arrives with R and B swapped (0xFD20 orange arrived as 0x20FD blue, and the
  // olive before it as near-black, i.e. "invisible"). So the panel gets a
  // byte-swapped copy.
  //
  // The /screen band is NOT a pushImage target: Canvas::pushImage feeds it one
  // drawPixel at a time, which wants host order. Swapping unconditionally was
  // how this was finally pinned down - the panel came out right while the grab
  // turned blue, and the measured strip colour (255,182,0 vs 32,0,255) said the
  // two paths disagree. Keep them separate: panel swapped, grab as-is.
  if (!gCapture) {
    for (int i = 0; i < BOX_W * boxH; i++)
      buf[i] = (uint16_t)((buf[i] >> 8) | (buf[i] << 8));
  }
  tft.pushImage(x, y, BOX_W, boxH, buf);
}

void hintBarGrid()
{
  // The column's edge runs the FULL panel height: the strip is the instrument's
  // spine, crossing a page's header rather than sitting under it.
  tft.drawFastVLine(HINT_BAR_X, 0, HINT_BAR_H, GRID_GRAY);
  for (int i = 1; i < HINT_CELLS; i++) {
    tft.drawFastHLine(HINT_BAR_X, (HINT_BAR_H * i) / HINT_CELLS, HINT_BAR_W, GRID_GRAY);
  }
}

void hintBarCell(int index, const char *topWord, const char *bottomWord,
                 uint16_t topColor, uint16_t bottomColor)
{
  if (index < 0 || index >= HINT_CELLS || !topWord) return;
  HintCell &c = cells[index];
  const String top(topWord);
  const String bottom(bottomWord ? bottomWord : "");
  if (c.valid && c.top == top && c.bottom == bottom &&
      c.topColor == topColor && c.bottomColor == bottomColor) return;
  c.top = top;
  c.bottom = bottom;
  c.topColor = topColor;
  c.bottomColor = bottomColor;
  c.valid = true;

  // Clear the cell first: the spine image is opaque but its HEIGHT follows the
  // word, so switching from PARTIAL to ALL would leave the tail behind.
  const int cellTop = (HINT_BAR_H * index) / HINT_CELLS;
  const int cellBot = (HINT_BAR_H * (index + 1)) / HINT_CELLS;
  tft.fillRect(HINT_BAR_X + 1, cellTop + 1, HINT_BAR_W - 2, cellBot - cellTop - 2, BG);

  const int n1 = (int)strlen(topWord);
  const int n2 = (int)strlen(c.bottom.c_str());
  const int total = n2 ? (n1 + 1 + n2) : n1;      // one glyph slot between them
  const int cy = (cellTop + cellBot) / 2;
  const int y0 = cy - (total * 8) / 2;            // whole block, centred
  const int x = HINT_BAR_X + (HINT_BAR_W - 16) / 2;
  drawSpineWord(x, y0, topWord, topColor);
  if (n2) drawSpineWord(x, y0 + (n1 + 1) * 8, bottomWord, bottomColor);
}

void hintBarReset()
{
  for (int i = 0; i < HINT_CELLS; i++) cells[i].valid = false;
}
