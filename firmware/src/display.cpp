// display.cpp — ST7735 graphical UI per docs/07_ui_kit.md.
//
// The complete 160x128 frame is composed off-screen and sent to the ST7735 in one SPI
// transaction.  This is the Adafruit_GFX equivalent of the sprite-buffer required by the
// UI kit: intermediate clears are never visible on the physical panel.
// No "sleep breathe" animation screen: this hardware cuts backlight power entirely during
// STATE_SLEEP (see sleep.cpp's backlightOff()), so nothing would be visible regardless of
// what's drawn -- matches the old OLED's setPowerSave(1) blanking behavior.
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "display.h"
#include "pins.h"
#include "globals.h"
#include "state.h"
#include "menu.h"
#include "power.h"
#include "wifi_module.h"
#include "input.h"

static const int SCREEN_W = 160;
static const int SCREEN_H = 128;
static const int HEADER_H = 14;
static const int FOOTER_H = 16;
static const int CONTENT_BOTTOM = SCREEN_H - FOOTER_H;

static Adafruit_ST7735 panel(TFT_CS, TFT_DC, TFT_RST);
static GFXcanvas16 tft(SCREEN_W, SCREEN_H);

static void flushFrame() {
  uint16_t* pixels = tft.getBuffer();
  if (!pixels) return;
  panel.startWrite();
  panel.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
  panel.writePixels(pixels, SCREEN_W * SCREEN_H, true, false);
  panel.endWrite();
}

// ── Palette (RGB565) — runtime-selectable (Settings > System > Theme), not #defines, so
// applyTheme() can repoint them without touching any of the render*() functions below.
// COL_SEL is the color used for the selected-row/tile text+border specifically -- kept
// distinct from COL_CYAN (the general motion/accent color used elsewhere) because the
// High Contrast theme inverts the selection block (white bg, black text) while every
// other accent stays a vivid color on black; Dark/Light just set COL_SEL == COL_CYAN so
// their look is unchanged from before themes existed.
static uint16_t BG_BASE, BG_PANEL, BG_CARD, BG_HIGHLIGHT;
static uint16_t COL_CYAN, COL_GREEN, COL_AMBER, COL_RED, COL_PURPLE, COL_SEL;
static uint16_t TEXT_PRI, TEXT_SEC, TEXT_DIM, DIVIDER;

static bool forceFullRepaint = true;  // set by displaySetTheme(); consumed next update

struct ThemePalette {
  uint16_t bgBase, bgPanel, bgCard, bgHighlight;
  uint16_t cyan, green, amber, red, purple, sel;
  uint16_t textPri, textSec, textDim, divider;
};

static const ThemePalette THEMES[3] = {
  // Dark (docs/07_ui_kit.md original values)
  { 0x0862, 0x10C4, 0x1928, 0x298A,
    0x06BF, 0x074F, 0xFD04, 0xF948, 0x931F, 0x06BF,
    0xFFFF, 0x7C97, 0x3A4C, 0x1969 },
  // Light
  { 0xF7BE, 0xE75E, 0xFFFF, 0xD71F,
    0x0456, 0x1449, 0xB340, 0xC0E7, 0x6A19, 0x0456,
    0x10A3, 0x4AAE, 0x8CB5, 0xCEBC },
  // High Contrast (pure black/white + saturated primaries; selection is inverted)
  { 0x0000, 0x0000, 0x0000, 0xFFFF,
    0x07FF, 0x07E0, 0xFF40, 0xF800, 0xF81F, 0x0000,
    0xFFFF, 0xE73C, 0xAD55, 0x632C },
};

void displaySetTheme(uint8_t theme) {
  if (theme > 2) theme = THEME_DARK;
  const ThemePalette& p = THEMES[theme];
  BG_BASE = p.bgBase; BG_PANEL = p.bgPanel; BG_CARD = p.bgCard; BG_HIGHLIGHT = p.bgHighlight;
  COL_CYAN = p.cyan; COL_GREEN = p.green; COL_AMBER = p.amber; COL_RED = p.red;
  COL_PURPLE = p.purple; COL_SEL = p.sel;
  TEXT_PRI = p.textPri; TEXT_SEC = p.textSec; TEXT_DIM = p.textDim; DIVIDER = p.divider;
  forceFullRepaint = true;  // every pixel's color just changed meaning
  displayDirty = true;
}

// The retained canvas makes partial same-screen updates cheap while flushFrame() keeps the
// physical update atomic. Numeric fields remain padded so their canvas pixels self-erase.
static bool fullRepaint = true;

static void clearContentBand() {
  if (fullRepaint) tft.fillRect(0, HEADER_H, SCREEN_W, CONTENT_BOTTOM - HEADER_H, BG_BASE);
}
static void clearFullBand() {  // screens with no status bar
  if (fullRepaint) tft.fillRect(0, 0, SCREEN_W, CONTENT_BOTTOM, BG_BASE);
}

// Opaque, fixed-width text: pads to `width` chars so shrinking values self-erase.
static void textOpaque(int x, int y, uint16_t fg, const char* s, int width = 0) {
  tft.setTextColor(fg, BG_BASE);
  tft.setCursor(x, y);
  tft.print(s);
  for (int i = (int)strlen(s); i < width; i++) tft.print(' ');
}

static void formatSteps(int32_t value, char* out, size_t outSize) {
  char raw[16];
  snprintf(raw, sizeof(raw), "%ld", (long)value);
  int len = strlen(raw);
  bool negative = raw[0] == '-';
  int digits = len - (negative ? 1 : 0);
  int spaces = digits > 0 ? (digits - 1) / 3 : 0;
  if ((size_t)(len + spaces + 1) > outSize) {
    snprintf(out, outSize, "%s", raw);
    return;
  }
  int src = len - 1;
  int dst = len + spaces;
  out[dst--] = '\0';
  int group = 0;
  while (src >= (negative ? 1 : 0)) {
    if (group == 3) {
      out[dst--] = ' ';
      group = 0;
    }
    out[dst--] = raw[src--];
    group++;
  }
  if (negative) out[0] = '-';
}

// ── State -> accent color (per docs §1.3) ──
static uint16_t stateColor() {
  switch (sliderState) {
    case STATE_MANUAL_MOVING:
    case STATE_MOVING_TO_POS: return COL_CYAN;
    case STATE_HOMING:        return COL_AMBER;
    case STATE_PARKING:       return COL_AMBER;
    case STATE_ERROR:         return COL_RED;
    case STATE_SLEEP:         return TEXT_DIM;
    case STATE_PING_PONG:     return COL_CYAN;
    default:                  return COL_GREEN;  // IDLE
  }
}

// Small hand-drawn bolt (no bitmap asset for this one) in an ~8x12 box at (x,y). Always
// clears its own cell first since it's drawn with a couple of triangles, not a solid
// bitmap like the other status-bar icons -- otherwise turning charging off would leave the
// old bolt shape behind.
static void drawChargeIcon(int x, int y, bool on, uint16_t bg) {
  tft.fillRect(x, y, 8, 12, bg);
  if (!on) return;
  tft.fillTriangle(x + 5, y + 0, x + 1, y + 6, x + 4, y + 6, COL_AMBER);
  tft.fillTriangle(x + 3, y + 5, x + 7, y + 5, x + 2, y + 11, COL_AMBER);
}

// Battery glyph per design handoff: 18x9 body + 1px border, 3 segments, 2x5 nub -- replaces
// the old 12x12 bitmap (bat_0..bat_4). Drawn procedurally instead of as a bitmap so the
// segment count/fill color can vary without needing five more baked icon assets.
static const int BATT_W = 18, BATT_H = 9;

static void drawBatteryIcon(int x, int y) {
  int pct = batteryPercent();
  int segLevel = (pct >= 66) ? 3 : (pct >= 33) ? 2 : (pct > 0) ? 1 : 0;
  uint16_t fillColor = (pct <= 0) ? COL_RED : (pct < 25) ? COL_AMBER : COL_GREEN;

  tft.drawRect(x, y, BATT_W, BATT_H, TEXT_SEC);
  tft.fillRect(x + 18, y + 2, 2, 5, TEXT_SEC);  // nub
  for (int i = 0; i < 3; i++) {
    int segX = x + 2 + i * 5;
    tft.fillRect(segX, y + 2, 4, 5, (i < segLevel) ? fillColor : BG_CARD);
  }
}

// Battery icon (+ charge bolt), right-aligned to the header's right margin -- shared by
// drawStatusBar() (MAIN) and drawScreenHeader() (every other screen) so both stay pixel-
// identical without duplicating the layout math. The numeric voltage readout that used to
// sit to its right was dropped from the header; the exact figure still lives on the System
// and Diagnostics screens.
static void drawBatteryGroup(uint16_t panelBg) {
  const int battX = SCREEN_W - 3 - (BATT_W + 2);  // +2 for the nub past BATT_W
  const int chargeX = battX - 2 - 8;

  // Wipe the whole group cell every call, not just on a full repaint: this runs on partial
  // header redraws too, and without it the old voltage digits (which used to self-erase by
  // being reprinted over an opaque bg) would be left stranded to the right of the icon.
  tft.fillRect(chargeX, 0, SCREEN_W - chargeX, HEADER_H - 1, panelBg);

  drawChargeIcon(chargeX, 0, isCharging, panelBg);
  drawBatteryIcon(battX, 2);
}

// ── Status bar (top 14px): title, battery icon ──
static void drawStatusBar() {
  if (fullRepaint) tft.fillRect(0, 0, SCREEN_W, HEADER_H, BG_PANEL);

  tft.setTextSize(1);
  tft.setTextColor(COL_CYAN, BG_PANEL);
  tft.setCursor(3, 3);
  tft.print("SLIDER");

  drawBatteryGroup(BG_PANEL);

  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, DIVIDER);
}

static void drawScreenHeader(const char* title, uint16_t color = 0) {
  if (color == 0) color = TEXT_PRI;
  if (fullRepaint) {
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, BG_PANEL);
    tft.setTextSize(1);
    tft.setTextColor(color, BG_PANEL);
    tft.setCursor(3, 3);
    for (const char* p = title; *p; p++) {
      char c = *p;
      tft.print((c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c);
    }
    tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, DIVIDER);
  }
  drawBatteryGroup(BG_PANEL);
}

// ── Position progress bar with lerp animation ──
static float displayedFrac = 0;

// Returns true while the bar is still visibly sliding toward its target -- lets
// displayUpdate() know an extra redraw is needed even with no discrete state change.
static bool updatePositionLerp() {
  float target = 0;
  if (isCalibrated && travelDistance > 0) {
    target = (float)currentPosition / (float)travelDistance;
    if (target < 0) target = 0;
    if (target > 1) target = 1;
  }
  displayedFrac += (target - displayedFrac) * 0.25f;
  float remaining = target - displayedFrac;
  if (remaining < 0) remaining = -remaining;
  return remaining > 0.002f;
}

static void drawPositionBar(int x, int y, int w) {
  const int h = 10;
  tft.setTextSize(1);
  textOpaque(x, y + 1, endstop1 ? COL_RED : TEXT_SEC, "E1");
  textOpaque(x + w - 12, y + 1, endstop2 ? COL_RED : TEXT_SEC, "E2");

  // Paint filled and unfilled halves directly instead of clearing the whole bar first --
  // every pixel gets written exactly once, so the bar never blanks between frames.
  int barX = x + 16, barW = w - 32;
  uint16_t fillColor = motorRunning ? COL_CYAN : COL_GREEN;
  int fillW = (int)(barW * displayedFrac);
  if (fillW < 0) fillW = 0;
  if (fillW > barW) fillW = barW;
  if (fillW > 0)      tft.fillRect(barX, y, fillW, h, fillColor);
  if (fillW < barW)   tft.fillRect(barX + fillW, y, barW - fillW, h, BG_CARD);
  int tickX = barX + fillW;
  if (tickX > barX + barW - 1) tickX = barX + barW - 1;
  tft.drawFastVLine(tickX, y, h, TEXT_PRI);
}

// ── Button hint footer ──
// Physical layout: BTN1 and BTN2 are stacked on the left flank and the encoder is on the
// right. The footer mirrors that arrangement with icons only -- no more text labels next to
// them (BTN1/BTN2 are icons now, not words) -- while the encoder action stays right-aligned
// text (unchanged; only the two button hints lost their labels).
// BTN2's hint icon: fixed hamburger/menu glyph, same on every screen that shows it.
static void drawMenuGlyph(int x, int y, uint16_t color) {
  tft.drawFastHLine(x, y + 1, 6, color);
  tft.drawFastHLine(x, y + 3, 6, color);
  tft.drawFastHLine(x, y + 5, 6, color);
}

// BTN1's hint icon: play (start) or pause (stop) -- the only hint icon with two states,
// since BTN1 is the actual start/stop toggle on MAIN and PING PONG. Every other screen just
// leaves it as play (its historical fixed "primary action" triangle).
static void drawPlayPauseGlyph(int x, int y, bool paused, uint16_t color) {
  if (paused) {
    tft.fillRect(x, y, 2, 8, color);
    tft.fillRect(x + 4, y, 2, 8, color);
  } else {
    tft.fillTriangle(x, y, x, y + 7, x + 5, y + 3, color);
  }
}

static void drawHints(const char* btn1, const char* btn2, const char* enc,
                      bool btn1Dim = false, bool btn2Dim = false,
                      uint16_t footerBg = 0xFFFF, bool btn1Paused = false,
                      uint16_t btn1Color = 0, uint16_t btn2Color = 0) {
  uint16_t bg = footerBg == 0xFFFF ? BG_PANEL : footerBg;
  const int glyphY = CONTENT_BOTTOM + 4;
  tft.fillRect(0, CONTENT_BOTTOM, SCREEN_W, FOOTER_H, bg);
  tft.drawFastHLine(0, CONTENT_BOTTOM, SCREEN_W, DIVIDER);
  tft.setTextSize(1);

  int leftX = 3;
  if (btn1 && btn1[0]) {
    uint16_t c = btn1Color ? btn1Color : (btn1Dim ? TEXT_DIM : TEXT_SEC);
    drawPlayPauseGlyph(leftX, glyphY, btn1Paused, c);
    tft.setTextColor(c, bg);
    tft.setCursor(leftX + 8, glyphY);
    tft.print(btn1);
    leftX += 8 + strlen(btn1) * 6 + 6;
  }

  if (btn2 && btn2[0]) {
    uint16_t c = btn2Color ? btn2Color : (btn2Dim ? TEXT_DIM : TEXT_SEC);
    int hintW = 8 + strlen(btn2) * 6;
    int x = (enc && enc[0]) ? leftX : SCREEN_W - 3 - hintW;
    drawMenuGlyph(x, glyphY + 1, c);
    tft.setTextColor(c, bg);
    tft.setCursor(x + 8, glyphY);
    tft.print(btn2);
  }

  if (enc && enc[0]) {
    char right[24];
    snprintf(right, sizeof(right), "(o) %s", enc);
    int rightW = strlen(right) * 6;
    tft.setTextColor(COL_CYAN, bg);
    tft.setCursor(SCREEN_W - 3 - rightW, glyphY);
    tft.print(right);
  }
}

// ── Generic vertical list (Menu tiles fallback list, Settings/Motion/Sleep/System) ──
static void drawList(MenuScreen screen) {
  int count = menuItemCount(screen);
  const int rowH = 20;
  const int startY = HEADER_H;
  int visible = (CONTENT_BOTTOM - startY) / rowH;

  if (menuIndex < menuOffset) menuOffset = menuIndex;
  if (menuIndex >= menuOffset + visible) menuOffset = menuIndex - visible + 1;

  for (int i = 0; i < visible && (menuOffset + i) < count; i++) {
    int idx = menuOffset + i;
    int y = startY + i * rowH;
    bool sel = (idx == menuIndex);
    tft.fillRect(0, y, SCREEN_W, rowH, sel ? BG_HIGHLIGHT : BG_BASE);
    if (i > 0) tft.drawFastHLine(0, y, SCREEN_W, DIVIDER);
    // Full-row highlight + border + a leading ">" marker, per the design handoff -- the
    // label/value text itself stays plain (white/gray) on selection, not tinted, unlike
    // the old left-accent-bar style.
    if (sel) {
      tft.drawRect(0, y, SCREEN_W, rowH, COL_SEL);
      tft.setTextColor(COL_SEL);
      tft.setCursor(2, y + 6);
      tft.print(">");
    }

    tft.setTextSize(1);
    tft.setTextColor(TEXT_PRI);
    tft.setCursor(sel ? 14 : 8, y + 6);
    tft.print(menuItemLabel(screen, idx));

    char valBuf[24];
    menuItemValueText(screen, idx, valBuf, sizeof(valBuf));
    if (valBuf[0]) {
      int vw = strlen(valBuf) * 6;
      tft.setTextColor(TEXT_SEC);
      tft.setCursor(SCREEN_W - 6 - vw, y + 6);
      tft.print(valBuf);
    }
  }
}

// ── MAIN screen ──
// Big position readout: fixed field width + opaque self-erasing text instead of a
// fillRect-then-redraw. At 10Hz while the motor runs, blanking this row every frame was
// the single biggest source of visible flicker (see the anti-flicker note above) --
// widths/centering that changed with the digit count were the reason it used to need a
// blank first. A cached last-drawn string also skips the SPI write entirely on ticks
// where the position didn't actually change (position bar can still be mid-lerp).
static const int MAIN_VALUE_WIDTH = 8;  // chars; covers travel up to ~6 digits grouped + "*"
static char lastMainValue[20] = "";

static void renderMain() {
  clearContentBand();
  drawStatusBar();
  const int barX = 6, barY = 27, barW = SCREEN_W - 12;
  tft.fillRect(barX, barY, barW, 8, BG_CARD);
  tft.drawRect(barX, barY, barW, 8, DIVIDER);
  int fillW = (int)((barW - 2) * displayedFrac);
  uint16_t fillColor = motorRunning ? 0x0559 : COL_GREEN;
  if (fillW > 0) tft.fillRect(barX + 1, barY + 1, fillW, 6, fillColor);
  int tickX = constrain(barX + 1 + fillW, barX + 1, barX + barW - 2);
  tft.fillRect(tickX, barY - 1, 2, 10, TEXT_PRI);

  char value[20], total[20], buf[28];
  snprintf(value, sizeof(value), "%ld", (long)currentPosition);
  if (!isCalibrated) strncat(value, "*", sizeof(value) - strlen(value) - 1);
  snprintf(total, sizeof(total), "%ld", (long)travelDistance);
  snprintf(buf, sizeof(buf), "/%s", total);
  int suffixW = isCalibrated ? strlen(buf) * 6 : 0;
  int valueW = strlen(value) * 18;
  int valueX = (SCREEN_W - valueW - suffixW - (suffixW ? 3 : 0)) / 2;
  tft.fillRect(0, 42, SCREEN_W, 27, BG_BASE);
  tft.setTextSize(3);
  tft.setTextColor(TEXT_PRI, BG_BASE);
  tft.setCursor(valueX, 43);
  tft.print(value);
  if (isCalibrated) {
    tft.setTextSize(1);
    tft.setTextColor(TEXT_SEC, BG_BASE);
    tft.setCursor(valueX + valueW + 3, 58);
    tft.print(buf);
  }

  tft.setTextSize(1);
  snprintf(buf, sizeof(buf), "%u%%", cfg.speed);
  textOpaque(6, 84, TEXT_PRI, buf, 4);
  const char* state = motorRunning ? "MOVE" : stateToString(sliderState);
  char motion[20];
  snprintf(motion, sizeof(motion), "%s %s", motorRunning ? (motorDirection ? "<<" : ">>") : "", state);
  int motionW = strlen(motion) * 6;
  textOpaque(SCREEN_W - 6 - motionW, 84, stateColor(), motion);

  drawHints(motorRunning ? "STOP" : "GO", "MENU", "", false, false,
            0xFFFF, motorRunning, COL_CYAN, COL_AMBER);
}

// ── Menu carousel icons -- one hand-drawn vector glyph per item, no text-in-a-box badges.
// Shapes are simplified approximations of the design handoff's SVGs (turn 4), not literal
// path replicas -- there's no vector-path renderer here, just line/rect/circle primitives.
static void drawIconManualMove(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 24, cy + 15, 48, 4, TEXT_DIM);
  tft.drawRoundRect(cx - 10, cy - 9, 20, 20, 3, color);
  tft.drawRoundRect(cx - 9, cy - 8, 18, 18, 3, color);
  tft.drawFastHLine(cx - 22, cy, 11, color);
  tft.fillTriangle(cx - 24, cy, cx - 17, cy - 7, cx - 17, cy + 7, color);
  tft.drawFastHLine(cx + 11, cy, 11, color);
  tft.fillTriangle(cx + 24, cy, cx + 17, cy - 7, cx + 17, cy + 7, color);
}
static void drawIconPosition(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 24, cy + 17, 48, 4, TEXT_DIM);
  for (int x = cx - 18; x <= cx + 18; x += 9) tft.fillRect(x, cy + 12, 3, 9, TEXT_DIM);
  tft.fillRect(cx - 8, cy - 4, 4, 21, color);
  tft.fillTriangle(cx - 13, cy - 4, cx - 1, cy - 4, cx - 7, cy - 12, color);
  for (int y = cy - 18; y < cy + 13; y += 5) tft.fillRect(cx + 9, y, 2, 3, COL_PURPLE);
  tft.drawCircle(cx + 10, cy - 20, 6, COL_PURPLE);
  tft.drawCircle(cx + 10, cy - 20, 5, COL_PURPLE);
}
static void drawIconPingPong(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 22, cy - 20, 3, 42, TEXT_DIM);
  tft.fillRect(cx + 19, cy - 20, 3, 42, TEXT_DIM);
  tft.fillRect(cx - 15, cy - 9, 30, 3, color);
  tft.fillTriangle(cx + 19, cy - 8, cx + 11, cy - 15, cx + 11, cy - 1, color);
  tft.fillRect(cx - 15, cy + 9, 30, 3, color);
  tft.fillTriangle(cx - 19, cy + 10, cx - 11, cy + 3, cx - 11, cy + 17, color);
}
static void drawIconCalibration(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 24, cy - 17, 3, 38, color);
  tft.fillRect(cx + 21, cy - 17, 3, 38, color);
  tft.fillRect(cx - 22, cy + 8, 44, 4, TEXT_DIM);
  for (int x = cx - 15; x <= cx + 15; x += 10) tft.fillRect(x, cy + 2, 3, 7, TEXT_DIM);
  tft.drawRoundRect(cx - 8, cy + 10, 16, 13, 3, COL_CYAN);
  tft.drawRoundRect(cx - 7, cy + 11, 14, 11, 3, COL_CYAN);
}
static void drawIconSettings(int cx, int cy, uint16_t color) {
  tft.drawCircle(cx, cy, 21, color); tft.drawCircle(cx, cy, 20, color);
  tft.fillCircle(cx, cy, 8, COL_PURPLE); tft.fillCircle(cx, cy, 4, BG_BASE);
  tft.fillRect(cx - 3, cy - 27, 6, 7, color); tft.fillRect(cx - 3, cy + 20, 6, 7, color);
  tft.fillRect(cx - 27, cy - 3, 7, 6, color); tft.fillRect(cx + 20, cy - 3, 7, 6, color);
  tft.fillRect(cx - 21, cy - 21, 6, 6, color); tft.fillRect(cx + 15, cy - 21, 6, 6, color);
  tft.fillRect(cx - 21, cy + 15, 6, 6, color); tft.fillRect(cx + 15, cy + 15, 6, 6, color);
}

// ── MENU (one item at a time, full content area; rotate to page through, dots show which) ──
static void renderMenu() {
  clearContentBand();
  drawScreenHeader("MENU", COL_PURPLE);

  int count = menuItemCount(SCREEN_MENU);
  int i = menuIndex;
  // Order: Manual Move, Position, Ping-Pong, Calibration, Settings (matches handoff turn 4).
  uint16_t iconColor = (i == 2) ? COL_CYAN : (i == 3) ? COL_AMBER : (i == 4) ? COL_PURPLE : COL_CYAN;

  const int cx = SCREEN_W / 2, cy = HEADER_H + 39;
  tft.fillRect(0, HEADER_H, SCREEN_W, CONTENT_BOTTOM - HEADER_H, BG_BASE);
  switch (i) {
    case 0: drawIconManualMove(cx, cy, iconColor); break;
    case 1: drawIconPosition(cx, cy, iconColor); break;
    case 2: drawIconPingPong(cx, cy, iconColor); break;
    case 3: drawIconCalibration(cx, cy, iconColor); break;
    case 4: drawIconSettings(cx, cy, iconColor); break;
  }

  // Chevrons at the content edges, dimmed at the ends of the list (nothing to page to).
  tft.setTextSize(2);
  tft.setTextColor(i > 0 ? TEXT_SEC : TEXT_DIM, BG_BASE);
  tft.setCursor(4, cy - 7);
  tft.print("<");
  tft.setTextColor(i < count - 1 ? TEXT_SEC : TEXT_DIM, BG_BASE);
  tft.setCursor(SCREEN_W - 16, cy - 7);
  tft.print(">");

  tft.setTextSize(1);
  static const char* labels[] = { "MANUAL MOVE", "POSITION", "PING-PONG", "CALIBRATION", "SETTINGS" };
  const char* label = labels[constrain(i, 0, 4)];
  int lw = strlen(label) * 6;
  textOpaque((SCREEN_W - lw) / 2, 91, TEXT_PRI, label, 16);

  // Page dots -- redrawn every call (cheap, count is tiny), so the filled dot always
  // tracks menuIndex without needing a fullRepaint.
  tft.fillRect(0, CONTENT_BOTTOM, SCREEN_W, FOOTER_H, BG_PANEL);
  tft.drawFastHLine(0, CONTENT_BOTTOM, SCREEN_W, DIVIDER);
  const int dotY = CONTENT_BOTTOM + FOOTER_H / 2;
  const int dotSpacing = 11;
  int startX = (SCREEN_W - (count - 1) * dotSpacing) / 2;
  for (int d = 0; d < count; d++) {
    int dcx = startX + d * dotSpacing;
    if (d == i) tft.fillCircle(dcx, dotY, 3, COL_SEL);
    else        tft.fillCircle(dcx, dotY, 2, BG_HIGHLIGHT);
  }
}

// ── MANUAL_MOVE ──
static void renderManualMove() {
  clearContentBand();
  drawScreenHeader("MANUAL MOVE");

  uint16_t arrowColor = motorRunning ? COL_CYAN : TEXT_SEC;
  bool backward = motorDirection;
  tft.fillRect(62, 34, 48, 15, BG_BASE);
  for (int i = 0; i < 3; i++) {
    int cx = 66 + i * 14;
    if (backward) tft.fillTriangle(cx + 8, 35, cx + 8, 47, cx, 41, arrowColor);
    else          tft.fillTriangle(cx, 35, cx, 47, cx + 8, 41, arrowColor);
  }

  const char* direction = motorRunning ? (backward ? "REVERSE" : "FORWARD") : "STOPPED";
  int dirW = strlen(direction) * 6;
  tft.setTextSize(1);
  textOpaque((SCREEN_W - dirW) / 2, 56, TEXT_SEC, direction);

  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", cfg.speed);
  textOpaque(34, 77, TEXT_PRI, buf, 4);
  int fillW = 72 * cfg.speed / 100;
  tft.fillRect(58, 79, 72, 5, BG_CARD);
  if (fillW > 0) tft.fillRect(58, 79, fillW, 5, COL_CYAN);

  drawHints("BACK", "MAIN", "REVERSE");
}

// ── PING PONG ──
static void renderPingPong() {
  clearContentBand();
  drawScreenHeader("PING-PONG", TEXT_PRI);

  if (!isCalibrated) {
    textOpaque(34, 49, COL_AMBER, "NOT CALIBRATED", 15);
    drawHints("", "MENU", "");
    return;
  }

  static const char* startNames[] = { "CTR", "E1", "E2" };
  char buf[24], value[20];
  bool running = (sliderState == STATE_PING_PONG);
  float frac = travelDistance > 0 ? (float)currentPosition / (float)travelDistance : 0;
  frac = constrain(frac, 0.0f, 1.0f);
  const int barX = 6, barY = 27, barW = SCREEN_W - 12;
  tft.fillRect(barX, barY, barW, 8, BG_CARD);
  tft.drawRect(barX, barY, barW, 8, DIVIDER);
  tft.fillRect(barX + 1, barY + 1, barW - 2, 6, 0x19CA);
  tft.fillRect(barX + 1, barY - 1, 2, 10, COL_GREEN);
  tft.fillRect(barX + barW - 3, barY - 1, 2, 10, COL_GREEN);
  int tickX = constrain(barX + 1 + (int)((barW - 3) * frac), barX + 1, barX + barW - 3);
  tft.fillRect(tickX, barY - 1, 2, 10, TEXT_PRI);

  snprintf(value, sizeof(value), "%ld", (long)currentPosition);
  tft.fillRect(0, 42, SCREEN_W, 27, BG_BASE);
  tft.setTextSize(3);
  tft.setTextColor(TEXT_PRI, BG_BASE);
  tft.setCursor(6, 43);
  tft.print(value);
  const char* direction = !running ? "STOP" : pingPongApproaching ? "->START" : motorDirection ? "E2>E1" : "E1>E2";
  tft.setTextSize(1);
  int directionW = strlen(direction) * 6;
  textOpaque(SCREEN_W - 6 - directionW, 57, running ? COL_CYAN : TEXT_SEC, direction);

  snprintf(buf, sizeof(buf), "%u%%", cfg.speed);
  textOpaque(6, 84, TEXT_SEC, buf, 4);
  const char* start = startNames[constrain((int)cfg.pingPongStart, 0, 2)];
  int startW = strlen(start) * 6;
  textOpaque((SCREEN_W - startW) / 2, 84, TEXT_SEC, start);
  const char* runText = running ? "RUN" : "READY";
  int runW = strlen(runText) * 6;
  textOpaque(SCREEN_W - 6 - runW, 84, running ? COL_CYAN : TEXT_SEC, runText);

  drawHints(running ? "PAUSE" : "START", "BACK", "", false, false,
            0xFFFF, running, COL_CYAN, TEXT_SEC);
}

// ── GO_TO_POS ──
static void renderGoToPos() {
  clearContentBand();
  drawScreenHeader("GO TO POSITION");
  tft.setTextSize(1);

  if (!isCalibrated || travelDistance <= 0) {
    textOpaque(34, 49, COL_AMBER, "NOT CALIBRATED", 15);
    drawHints("BACK", "MAIN", "");
    return;
  }

  const int barX = 24, barY = 29, barW = 112;
  float currentFrac = (float)currentPosition / (float)travelDistance;
  float targetFrac = (float)cmdTargetPos / (float)travelDistance;
  currentFrac = constrain(currentFrac, 0.0f, 1.0f);
  targetFrac = constrain(targetFrac, 0.0f, 1.0f);
  textOpaque(4, 28, TEXT_SEC, "E1");
  textOpaque(142, 28, TEXT_SEC, "E2");
  tft.fillRect(barX, barY, barW, 8, BG_CARD);
  int currentX = barX + (int)(barW * currentFrac);
  int targetX = barX + (int)(barW * targetFrac);
  tft.fillRect(barX, barY, currentX - barX, 8, COL_GREEN);
  tft.drawFastVLine(currentX, barY - 1, 10, TEXT_PRI);
  for (int y = barY - 2; y < barY + 10; y += 3) tft.drawFastVLine(targetX, y, 2, COL_PURPLE);

  char value[20];
  formatSteps(cmdTargetPos, value, sizeof(value));
  tft.setTextSize(3);
  static char lastGoToValue[20] = "";
  if (fullRepaint || strcmp(value, lastGoToValue) != 0) {
    strncpy(lastGoToValue, value, sizeof(lastGoToValue) - 1);
    lastGoToValue[sizeof(lastGoToValue) - 1] = 0;
    const int valueX = (SCREEN_W - MAIN_VALUE_WIDTH * 18) / 2;
    textOpaque(valueX, 49, COL_PURPLE, value, MAIN_VALUE_WIDTH);
  }
  tft.setTextSize(1);
  textOpaque(46, 79, TEXT_SEC, "TARGET STEPS");

  drawHints("BACK", "MAIN", "GO");
}

// ── CALIBRATION / active HOMING ──
static const char* homingPhaseText() {
  switch (homingPhase) {
    case HOME_SEEK_END1:  return "Seeking END1...";
    case HOME_BACKOFF1:   return "Backing off...";
    case HOME_SEEK_END2:  return "Seeking END2...";
    case HOME_BACKOFF2:   return "Backing off...";
    case HOME_GO_CENTER:  return "Centering...";
    case HOME_DONE:       return "Done";
    default:               return "";
  }
}
static int homingPhaseIndex() {
  switch (homingPhase) {
    case HOME_SEEK_END1: return 1;
    case HOME_BACKOFF1:  return 2;
    case HOME_SEEK_END2: return 3;
    case HOME_BACKOFF2:  return 4;
    case HOME_GO_CENTER: return 5;
    case HOME_DONE:      return 6;
    default:              return 0;
  }
}

static void renderCalibration() {
  clearContentBand();

  tft.setTextSize(1);
  char buf[28], value[20];

  if (sliderState == STATE_HOMING) {
    drawScreenHeader("HOMING", COL_AMBER);
    const char* phaseText = homingPhaseText();
    int phaseTextW = strlen(phaseText) * 6;
    tft.fillRect(0, 26, SCREEN_W, 12, BG_BASE);
    textOpaque((SCREEN_W - phaseTextW) / 2, 27, TEXT_PRI, phaseText);

    int phase = homingPhaseIndex();
    snprintf(buf, sizeof(buf), "Phase %d / 6", phase);
    int phaseW = strlen(buf) * 6;
    textOpaque((SCREEN_W - phaseW) / 2, 62, TEXT_SEC, buf, 11);

    const int dotY = 48;
    for (int i = 0; i < 6; i++) {
      uint16_t c = i < phase ? COL_AMBER : TEXT_DIM;
      tft.fillCircle(52 + i * 11, dotY, 3, c);
    }
    bool backward = homingPhase == HOME_SEEK_END1 || homingPhase == HOME_BACKOFF2;
    tft.fillRect(43, 80, 78, 13, BG_BASE);
    for (int i = 0; i < 5; i++) {
      int cx = 47 + i * 14;
      if (backward) tft.fillTriangle(cx + 8, 81, cx + 8, 91, cx, 86, COL_AMBER);
      else          tft.fillTriangle(cx, 81, cx, 91, cx + 8, 86, COL_AMBER);
    }

    drawHints("", "ABORT", "", true, false);
  } else {
    drawScreenHeader("CALIBRATION", COL_AMBER);
    if (isCalibrated) {
      formatSteps(travelDistance, value, sizeof(value));
      snprintf(buf, sizeof(buf), "Travel: %s steps", value);
      textOpaque(22, 38, TEXT_SEC, buf, 22);
      formatSteps(centerPosition, value, sizeof(value));
      snprintf(buf, sizeof(buf), "Center: %s steps", value);
      textOpaque(22, 55, TEXT_SEC, buf, 22);
      textOpaque(43, 78, COL_GREEN, "+ CALIBRATED", 12);
    } else {
      textOpaque(34, 49, COL_AMBER, "NOT CALIBRATED", 15);
    }
    drawHints("BACK", "MAIN", isCalibrated ? "RE-HOME" : "START");
  }
}

// ── Settings-family lists (Menu-level Settings, and the 3 sub-lists) ──
static void renderSettingsList(MenuScreen screen, const char* title) {
  clearContentBand();
  char header[24];
  snprintf(header, sizeof(header), "< %s", title);
  drawScreenHeader(header);

  drawList(screen);
  drawHints("BACK", "MAIN", "OPEN");
}

// ── VALUE_EDIT ──
static void renderValueEdit() {
  clearContentBand();
  drawScreenHeader(editLabel ? editLabel : "", COL_CYAN);

  char buf[16];
  if (editValueNames) {
    tft.setTextSize(2);
    const char* name = editValueNames[editValue];
    int nameW = strlen(name) * 12;
    int x = (SCREEN_W - nameW) / 2;
    if (x < 2) x = 2;
    tft.fillRect(0, 38, SCREEN_W, 31, BG_BASE);
    textOpaque(x, 44, COL_CYAN, name);
    tft.setTextSize(1);
    textOpaque(52, 76, TEXT_SEC, "SELECT VALUE");
  } else {
    snprintf(buf, sizeof(buf), "%ld", (long)editValue);
    tft.setTextSize(3);
    int w = (int)strlen(buf) * 18;
    int unitW = editUnit ? (4 + (int)strlen(editUnit) * 6) : 0;
    int x = (SCREEN_W - w - unitW) / 2;
    if (x < 0) x = 0;
    tft.fillRect(0, 38, SCREEN_W, 31, BG_BASE);
    tft.setTextColor(COL_CYAN, BG_BASE);
    tft.setCursor(x, 43);
    tft.print(buf);
    int right = x + w;
    if (editUnit) {
      tft.setTextSize(1);
      tft.setTextColor(TEXT_SEC, BG_BASE);
      tft.setCursor(right + 4, 58);
      tft.print(editUnit);
      right += unitW;
    }
  }

  if (!editValueNames) {
    int barX = 20, barY = 82, barW = 120;
    tft.fillRect(barX, barY, barW, 6, BG_CARD);
    float frac = (editMax > editMin) ? (float)(editValue - editMin) / (float)(editMax - editMin) : 0;
    tft.fillRect(barX, barY, (int)(barW * frac), 6, COL_CYAN);
    tft.setTextSize(1);
    snprintf(buf, sizeof(buf), "%ld", (long)editMin);
    textOpaque(barX, 91, TEXT_SEC, buf, 8);
    snprintf(buf, sizeof(buf), "%ld", (long)editMax);
    int maxW = strlen(buf) * 6;
    textOpaque(barX + barW - maxW, 91, TEXT_SEC, buf);
  }

  drawHints("CANCEL", "", "SAVE", false, true);
}

// ── ERROR ──
static void renderError() {
  bool pulse = (millis() / 200) % 2 == 0;
  static int8_t lastPulse = -1;
  if (!fullRepaint && lastPulse == (int8_t)pulse) return;
  lastPulse = pulse;
  uint16_t errorBg = pulse ? 0x1802 : 0x1001;
  tft.fillRect(0, HEADER_H, SCREEN_W, CONTENT_BOTTOM - HEADER_H, errorBg);
  if (fullRepaint) {
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, 0x2802);
    tft.setTextSize(1);
    tft.setTextColor(COL_RED, 0x2802);
    tft.setCursor(4, 2);
    tft.print("! SYSTEM ERROR");
    drawBatteryGroup(0x2802);  // battery stays visible on every screen, Error included
  }

  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI, errorBg);
  tft.setCursor(12, 35);
  tft.print(errorToString(errorCode));
  tft.setTextColor(0xC18F, errorBg);
  tft.setCursor(12, 62);
  tft.print("Motor stopped");
  tft.setCursor(12, 73);
  tft.print("for safety.");

  drawHints("RESET", "HOME", "", false, false, 0x2802, false, 0, COL_RED);
}

// A decision dialog reads as more deliberate with two bordered "chip" buttons in the footer
// than with the usual plain icon+text hint row -- this is the one screen that asks a real
// yes/no question, so it gets a visually distinct footer instead of drawHints().
static void drawConfirmFooter(const char* leftLabel, const char* rightLabel, uint16_t rightColor) {
  if (!fullRepaint) return;
  tft.fillRect(0, CONTENT_BOTTOM, SCREEN_W, FOOTER_H, BG_PANEL);
  tft.drawFastHLine(0, CONTENT_BOTTOM, SCREEN_W, DIVIDER);

  const int chipY = CONTENT_BOTTOM + 2, chipH = FOOTER_H - 4;
  const int chipW = (SCREEN_W - 12) / 2;
  tft.setTextSize(1);

  tft.drawRoundRect(4, chipY, chipW, chipH, 2, TEXT_SEC);
  tft.setTextColor(TEXT_SEC, BG_PANEL);
  int lw = strlen(leftLabel) * 6;
  tft.setCursor(4 + (chipW - lw) / 2, chipY + (chipH - 8) / 2);
  tft.print(leftLabel);

  int rx = 4 + chipW + 4;
  tft.drawRoundRect(rx, chipY, chipW, chipH, 2, rightColor);
  tft.setTextColor(rightColor, BG_PANEL);
  int rw = strlen(rightLabel) * 6;
  tft.setCursor(rx + (chipW - rw) / 2, chipY + (chipH - 8) / 2);
  tft.print(rightLabel);
}

// ── HOMING_CONFIRM ──
static void renderHomingConfirm() {
  clearFullBand();
  drawScreenHeader("! START HOMING?", COL_AMBER);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(14, 35);
  tft.print("Carriage will move");
  tft.setCursor(14, 48);
  tft.print("to both ends.");
  tft.setTextColor(TEXT_SEC);
  tft.setCursor(14, 68);
  tft.print("Takes about 20-30s.");

  drawConfirmFooter("CANCEL", "START", COL_AMBER);
}

// ── WIFI_SCAN (reuses drawList(), swaps in a "Scanning..." message mid-scan) ──
static void renderWifiScan() {
  clearContentBand();
  drawScreenHeader("< NETWORKS");

  // Scanning->results is a layout change, not just a content change, so the "Scanning..."
  // line has to be wiped explicitly -- clearContentBand() only fires on a screen change
  // and would otherwise leave it showing underneath the results list.
  static bool wasScanning = false;
  bool scanning = wifiScanInProgress();
  if (scanning != wasScanning) {
    tft.fillRect(0, HEADER_H, SCREEN_W, CONTENT_BOTTOM - HEADER_H, BG_BASE);
    wasScanning = scanning;
  }

  if (scanning) {
    textOpaque(30, 56, TEXT_SEC, "Scanning...", 12);
  } else {
    drawList(SCREEN_WIFI_SCAN);
  }
  drawHints("BACK", "MAIN", "OPEN");
}

// ── TEXT_EDIT (character wheel for STA/AP password entry) ──
static void renderTextEdit() {
  clearContentBand();
  drawScreenHeader(editTextLabel, COL_CYAN);

  // The text entered so far, followed by the pending character drawn inverted so it reads
  // as a cursor sitting at the insertion point rather than as a stray glyph elsewhere on
  // screen. 26 chars is what fits at size 1 (26*6 = 156px) -- longer entries scroll so the
  // cursor stays visible.
  tft.setTextSize(1);
  const int VISIBLE = 25;
  int from = (editTextLen > VISIBLE) ? editTextLen - VISIBLE : 0;
  tft.setTextColor(TEXT_PRI, BG_BASE);
  tft.setCursor(4, 24);
  tft.print(editText + from);

  int cursorX = 4 + (editTextLen - from) * 6;
  char pending[2] = { TEXT_CHARSET[editTextCharIdx], 0 };
  tft.fillRect(cursorX, 22, 7, 11, COL_CYAN);
  tft.setTextColor(BG_BASE, COL_CYAN);
  tft.setCursor(cursorX + 1, 24);
  tft.print(pending);

  // Clear any glyphs left behind when the text shrinks (backspace).
  int tailX = cursorX + 8;
  if (tailX < SCREEN_W) tft.fillRect(tailX, 22, SCREEN_W - tailX, 11, BG_BASE);

  tft.setTextColor(TEXT_SEC, BG_BASE);
  tft.setCursor(4, 44);
  tft.print("turn=pick  press=add");

  // Spell out the commit action: with a saved password pre-filled, pressing BTN2 straight
  // away is the whole interaction, and that was not discoverable from the hint bar alone.
  tft.setTextColor(COL_GREEN, BG_BASE);
  tft.setCursor(4, 58);
  tft.print("BTN2 = ");
  tft.print(editTextSubmitLabel);

  tft.setTextColor(TEXT_DIM, BG_BASE);
  tft.setCursor(4, 72);
  tft.print("BTN1 = delete char");

  char lenBuf[20];
  snprintf(lenBuf, sizeof(lenBuf), "%d chars", (int)editTextLen);
  textOpaque(4, 86, TEXT_DIM, lenBuf, 12);

  drawHints("DEL", editTextSubmitLabel, "NEXT");
}

// ── WIFI_CONNECTING ──
static void renderWifiConnecting() {
  clearFullBand();
  bool failed = (millis() - wifiConnectStartMs > 15000);

  // This screen repaints continuously (spinner), and the two status strings differ in
  // length, so both must be padded or "Connecting..." leaves debris under "Failed...".
  tft.setTextSize(1);
  textOpaque(10, 30, failed ? COL_RED : TEXT_PRI,
             failed ? "Failed to connect" : "Connecting...", 18);

  static const char spin[] = { '|', '/', '-', '\\' };
  char c[2] = { failed ? ' ' : spin[(millis() / 150) % 4], 0 };
  tft.setTextSize(2);
  textOpaque(SCREEN_W / 2 - 6, 60, COL_CYAN, c, 1);

  drawHints(failed ? "BACK" : "", failed ? "BACK" : "", "");
}

// ── DIAGNOSTICS (service screen) ──
// Deliberately reads the GPIOs directly rather than the debounced/edge-detected state the
// rest of the firmware consumes: the point is to see what actually arrives at the chip, so
// a dead switch or broken wire can be told apart from an input-handling bug. Idle level is
// 1 for the buttons (pull-ups) and 0 for the endstops (active high).
static void renderDiagnostics() {
  clearContentBand();
  drawScreenHeader("DIAGNOSTICS");

  char buf[28];
  int sw = digitalRead(ENC_SW), b1 = digitalRead(BTN1), b2 = digitalRead(BTN2);

  snprintf(buf, sizeof(buf), "ENC SW:%d  (gpio19)", sw);
  textOpaque(4, 16, sw ? TEXT_SEC : COL_GREEN, buf, 20);
  snprintf(buf, sizeof(buf), "BTN1:%d  BTN2:%d", b1, b2);
  textOpaque(4, 28, (b1 && b2) ? TEXT_SEC : COL_GREEN, buf, 20);
  snprintf(buf, sizeof(buf), "END1:%d  END2:%d",
           digitalRead(ENDSTOP_1), digitalRead(ENDSTOP_2));
  textOpaque(4, 40, TEXT_SEC, buf, 20);
  snprintf(buf, sizeof(buf), "ENC RAW:%ld", (long)inputEncoderRawCount());
  textOpaque(4, 52, COL_CYAN, buf, 20);

  snprintf(buf, sizeof(buf), "X%+.2f Y%+.2f Z%+.2f", adxlX, adxlY, adxlZ);
  textOpaque(4, 68, adxlFound ? TEXT_SEC : COL_RED, adxlFound ? buf : "ADXL not found", 22);
  snprintf(buf, sizeof(buf), "%.2fV  %.0fmA", busVoltage_V, current_mA);
  textOpaque(4, 80, ina226Found ? TEXT_SEC : COL_RED, ina226Found ? buf : "INA226 not found", 22);

  textOpaque(4, 100, TEXT_DIM, "hold BTN1+BTN2 3s", 20);
  drawHints("", "", "");
}

// ── MOTOR TEST (service screen) ──
static void renderMotorTest() {
  clearContentBand();
  drawScreenHeader("MOTOR TEST");

  char buf[24];

  tft.setTextSize(2);
  const char* dir = !motorRunning ? "STOPPED" : (motorDirection ? "<< BACK" : "FWD >>");
  textOpaque(4, 18, motorRunning ? COL_CYAN : TEXT_SEC, dir, 7);

  tft.setTextSize(1);
  snprintf(buf, sizeof(buf), "Speed: %u%%", cfg.speed);
  textOpaque(4, 40, TEXT_PRI, buf, 14);
  snprintf(buf, sizeof(buf), "Pos: %ld", (long)currentPosition);
  textOpaque(4, 52, TEXT_SEC, buf, 16);
  snprintf(buf, sizeof(buf), "E1:%d  E2:%d", endstop1 ? 1 : 0, endstop2 ? 1 : 0);
  textOpaque(4, 64, (endstop1 || endstop2) ? COL_RED : TEXT_SEC, buf, 14);
  snprintf(buf, sizeof(buf), "%.2fV %.0fmA", busVoltage_V, current_mA);
  textOpaque(4, 76, TEXT_SEC, buf, 16);

  textOpaque(4, 88, TEXT_DIM, "buttons=direction", 20);
  textOpaque(4, 98, TEXT_DIM, "turn=speed press=stop", 23);
  drawHints("", "", "");
}

void displayForceRepaint() {
  forceFullRepaint = true;
  displayDirty = true;
}

// ── OTA progress ──
static int otaLastPct = -1;

void displayOtaBegin() {
  otaLastPct = -1;
  tft.fillScreen(BG_BASE);
  tft.setTextSize(2);
  tft.setTextColor(COL_CYAN, BG_BASE);
  tft.setCursor(14, 22);
  tft.print("Updating");
  tft.setTextSize(1);
  tft.setTextColor(TEXT_SEC, BG_BASE);
  tft.setCursor(14, 46);
  tft.print("Do not power off");
  tft.drawRect(10, 66, SCREEN_W - 20, 12, TEXT_DIM);
  flushFrame();
}

void displayOtaProgress(unsigned int pct) {
  if (pct > 100) pct = 100;
  // Only touch the panel when the number actually moves: the transfer is blocking, so
  // every redundant SPI write here directly slows the update down.
  if ((int)pct == otaLastPct) return;
  otaLastPct = (int)pct;

  int barW = SCREEN_W - 24;
  int done = (int)((barW * pct) / 100);
  if (done > 0) tft.fillRect(12, 68, done, 8, COL_CYAN);

  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", pct);
  tft.setTextSize(2);
  textOpaque(60, 90, TEXT_PRI, buf, 4);
  flushFrame();
}

void displayOtaEnd(const char* msg, bool failed) {
  tft.setTextSize(1);
  textOpaque(14, 110, failed ? COL_RED : COL_GREEN, msg, 24);
  flushFrame();
}

void displayInit() {
  displaySetTheme(cfg.theme);
  panel.initR(INITR_BLACKTAB);
  panel.setRotation(1);
  tft.fillScreen(BG_BASE);
  flushFrame();
}

void displayUpdate() {
  static unsigned long lastTick = 0;
  if (millis() - lastTick < 100) return;
  lastTick = millis();

  bool lerping = updatePositionLerp();

  // The physical flush is atomic, but rendering and sending an unchanged 40 KB frame still
  // wastes CPU and SPI bandwidth. Only redraw when something is actually different:
  // screen navigation, an explicit
  // displayDirty (menu/value-edit/state-machine changes), or a live animation in progress
  // (position bar still sliding, motor turning, homing/parking running, error blink).
  static MenuScreen lastScreen = (MenuScreen)-1;
  bool screenChanged = (currentScreen != lastScreen);
  bool animating = motorRunning || lerping ||
                   sliderState == STATE_HOMING || sliderState == STATE_PARKING ||
                   sliderState == STATE_ERROR ||
                   // These two screens have live content driven by the WiFi stack rather
                   // than by any state/menu change, so they must keep redrawing: the scan
                   // list fills in asynchronously, and the connecting screen animates a
                   // spinner and flips to "Failed" purely on a timeout.
                   currentScreen == SCREEN_WIFI_SCAN ||
                   currentScreen == SCREEN_WIFI_CONNECTING ||
                   // Diagnostics mirrors live pin/sensor levels; it has to repaint on its
                   // own or it would freeze on the values captured when it opened.
                   currentScreen == SCREEN_DIAGNOSTICS ||
                   currentScreen == SCREEN_MOTOR_TEST;
  if (!screenChanged && !displayDirty && !animating) return;
  lastScreen = currentScreen;

  // Only a screen change needs the destructive clear; content-only updates repaint in
  // place via opaque text (see clearContentBand()/textOpaque()).
  fullRepaint = screenChanged || forceFullRepaint;
  forceFullRepaint = false;

  switch (currentScreen) {
    case SCREEN_MAIN:              renderMain(); break;
    case SCREEN_MENU:               renderMenu(); break;
    case SCREEN_MANUAL_MOVE:        renderManualMove(); break;
    case SCREEN_GO_TO_POS:          renderGoToPos(); break;
    case SCREEN_PING_PONG:          renderPingPong(); break;
    case SCREEN_CALIBRATION:        renderCalibration(); break;
    case SCREEN_SETTINGS:           renderSettingsList(SCREEN_SETTINGS, "Settings"); break;
    case SCREEN_MOTION_SETTINGS:    renderSettingsList(SCREEN_MOTION_SETTINGS, "Motion"); break;
    case SCREEN_SLEEP_SETTINGS:     renderSettingsList(SCREEN_SLEEP_SETTINGS, "Sleep"); break;
    case SCREEN_SYSTEM_SETTINGS:    renderSettingsList(SCREEN_SYSTEM_SETTINGS, "System"); break;
    case SCREEN_VALUE_EDIT:         renderValueEdit(); break;
    case SCREEN_ERROR:              renderError(); break;
    case SCREEN_HOMING_CONFIRM:     renderHomingConfirm(); break;
    case SCREEN_WIRELESS_SETTINGS:  renderSettingsList(SCREEN_WIRELESS_SETTINGS, "Wireless"); break;
    case SCREEN_WIFI_MODE:          renderSettingsList(SCREEN_WIFI_MODE, "WiFi"); break;
    case SCREEN_WIFI_SCAN:          renderWifiScan(); break;
    case SCREEN_TEXT_EDIT:          renderTextEdit(); break;
    case SCREEN_WIFI_CONNECTING:    renderWifiConnecting(); break;
    case SCREEN_DIAGNOSTICS:        renderDiagnostics(); break;
    case SCREEN_MOTOR_TEST:         renderMotorTest(); break;
  }

  flushFrame();
  displayDirty = false;
}
