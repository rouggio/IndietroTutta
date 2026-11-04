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
// box is 16 px wide (one font-2 glyph height) and as long as the word's own
// ADVANCE, not a fixed number of characters.
//
// The glyph pixels come from a throwaway 16-bit sprite (font 2, one glyph at a
// time) which we re-map ourselves, so the result is a plain image and goes
// out through pushImage — the path the /screen capture already understands.
// Drawing rotated text directly is not an option: TFT_eSPI rotates per display,
// not per string, and pushRotated() is non-virtual, i.e. it would bypass the
// capture target and write to the panel mid-grab.
//
// Each glyph advances by its OWN width (font 2 is proportional, ~5..13 px). A
// fixed 8 px step was clipping the wide letters: the sprite was 8 px wide, so
// everything past its edge was dropped and "MENU" came out with its stems
// shaved. A 16 px sprite holds the widest font-2 glyph plus its bearings.
static int spineCharW(char ch)
{
  char s[2] = { ch, 0 };
  return tft.textWidth(s, 2);
}

// Length of a word along the spine, in px (the same measure the drawing uses).
static int spineLen(const char *word)
{
  int total = 0;
  for (const char *p = word; p && *p; ++p) total += spineCharW(*p);
  return total;
}

static void drawSpineWord(int x, int y, const char *word, uint16_t fg)
{
  static const int BOX_W = 16;          // = one font-2 glyph height
  static const int MAX_CH = 12;         // longest label we will ever print
  static const int MAX_ROWS = 120;      // ...and the longest advance it needs
  static uint16_t buf[BOX_W * MAX_ROWS];
  static TFT_eSprite glyph(gPanel);
  static bool glyphReady = false;
  if (!glyphReady) {
    glyph.setColorDepth(16);
    glyphReady = (glyph.createSprite(16, 16) != nullptr);
  }
  if (!glyphReady || !word) return;
  const int n = (int)strlen(word);
  if (n < 1 || n > MAX_CH) return;

  // Measure first: the box is exactly the word's advance, so the caller can
  // centre it on the cell (and stack a second word) with no padding guesswork.
  int w[MAX_CH];
  int rows = 0;
  for (int k = 0; k < n; k++) {
    w[k] = spineCharW(word[k]);
    if (w[k] < 1) w[k] = 1;
    rows += w[k];
  }
  if (rows > MAX_ROWS) return;

  for (int i = 0; i < BOX_W * rows; i++) buf[i] = BG;

  int cursor = 0;
  for (int k = 0; k < n; k++) {
    char ch[2] = { word[k], 0 };
    glyph.fillSprite(BG);
    glyph.setTextDatum(TL_DATUM);      // ink anchored at the sprite's origin
    glyph.setTextColor(fg, BG);
    glyph.drawString(ch, 0, 0, 2);
    for (int sy = 0; sy < 16; sy++)
      for (int sx = 0; sx < w[k]; sx++) {
        // The glyph comes back as ordinary RGB565 from readPixel(); the swap
        // that the panel needs happens once, on the whole box, before the push.
        const uint16_t c = glyph.readPixel(sx, sy);
        if (c != BG) {
          // 90 deg clockwise: the glyph's top-left corner lands top-right.
          buf[(cursor + sx) * BOX_W + (BOX_W - 1 - sy)] = c;
        }
      }
    cursor += w[k];
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
    for (int i = 0; i < BOX_W * rows; i++)
      buf[i] = (uint16_t)((buf[i] >> 8) | (buf[i] << 8));
  }
  tft.pushImage(x, y, BOX_W, rows, buf);
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

  const int n2 = (int)strlen(c.bottom.c_str());
  // Measured, not 8 px per character: the glyphs are proportional, and the
  // block has to be centred on the cell as a whole.
  const int len1 = spineLen(topWord);
  const int len2 = n2 ? spineLen(c.bottom.c_str()) : 0;
  // Air between the two words: a visible space, not a seam. The glyph boxes are
  // tight (a letter's advance is its ink plus a pixel), so 3 px read as one word
  // run together ("VIEWBEAR"). 8 px is about a font-2 space and separates them.
  // A pair too tall for the cell shrinks the gap first (never below 3), so
  // TARGET/WIDE still fits instead of bleeding into the neighbouring cell.
  static const int SPINE_GAP = 8;
  const int cellH = cellBot - cellTop - 2;
  int gap = SPINE_GAP;
  if (n2 && len1 + gap + len2 > cellH) {
    gap = cellH - len1 - len2;
    if (gap < 3) gap = 3;
  }
  const int total = n2 ? (len1 + gap + len2) : len1;
  const int cy = (cellTop + cellBot) / 2;
  const int y0 = cy - total / 2;                   // whole block, centred
  const int x = HINT_BAR_X + (HINT_BAR_W - 16) / 2;
  drawSpineWord(x, y0, topWord, topColor);
  if (n2) drawSpineWord(x, y0 + len1 + gap, bottomWord, bottomColor);
}

void hintBarReset()
{
  for (int i = 0; i < HINT_CELLS; i++) cells[i].valid = false;
}
