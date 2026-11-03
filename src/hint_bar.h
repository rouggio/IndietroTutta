#pragma once
#include <Arduino.h>

// ---- Right-edge hint spine -------------------------------------------------
// Shared by every page that has one. A 29 px (4.5 mm) column against the right
// edge, running the FULL panel height, cut into three cells at the PANEL
// THIRDS — one cell per physical button, at the button.
//
// The text is rotated 90 degrees (a spine label: letters on their side, read
// top to bottom) because rotation is what lets a real word fit 4.5 mm: 8 glyphs
// are 64 px down a 16 px column, where upright stacked letters would cost
// 128 px AND a second 8 px column beside them.
//
// A cell carries up to two words: the SHORT press on top in `topColor`, the
// LONG press beneath in `bottomColor`. A cell with no long press passes nullptr
// and the single word is centred on its own.
constexpr int HINT_BAR_W = 29;
constexpr int HINT_BAR_X = 480 - HINT_BAR_W;   // 451: the column's left edge
constexpr int HINT_BAR_H = 320;
constexpr int HINT_CELLS = 3;

// The long-press colour, one step back from WHITE but still readable in
// sunlight (olive disappeared on the panel).
constexpr uint16_t HINT_LONG = 0xFD20;         // #FFA500 orange

// The column's rule and the two cell dividers. Structural: draw it with the
// page's own grid, every pass, idempotent and cheap.
void hintBarGrid();

// One cell, repainted only when its words or colours actually change, so a
// caller may invoke it on the 200 ms path (the strip is otherwise static).
void hintBarCell(int index, const char *topWord, const char *bottomWord,
                 uint16_t topColor, uint16_t bottomColor);

// Forget what is painted: page switch, a /screen capture, or a full-screen
// takeover that painted over the column.
void hintBarReset();
