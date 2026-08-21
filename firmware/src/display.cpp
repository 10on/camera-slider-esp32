// display.cpp — ST7735 graphical UI per docs/07_ui_kit.md.
//
// Simplifications vs the doc, all deliberate (see plan): no LovyanGFX/PSRAM full-buffer
// (Adafruit_GFX direct-to-display instead, same as the validated selftest firmware); no
// per-widget dirty-rect diffing -- every screen fully redraws on a simple ~100ms tick
// (10Hz, within the doc's own "10-20Hz, update only as needed" budget) which keeps this
// file tractable and avoids the overlapping-redraw-region class of bug hit during bring-up.
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
#include "icons_tft.h"
#include "wifi_module.h"
#include "input.h"

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

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

static const int SCREEN_W = 160;
static const int SCREEN_H = 128;
static const int HEADER_H = 13;
static const int FOOTER_H = 15;
static const int CONTENT_BOTTOM = SCREEN_H - FOOTER_H;

// Clearing the content area and then drawing on top of it is what makes this display
// visibly flash on every update -- on a direct-to-SPI ST7735 there's no back buffer, so
// the blank frame is genuinely shown for the duration of the redraw. So: only clear when
// the screen actually changed (layout differs, stale pixels must go), and for same-screen
// content updates draw text opaquely (setTextColor(fg, bg) makes each glyph paint its own
// background) so values overwrite themselves in place with nothing ever blanked.
// Numeric fields are space-padded to a fixed width so a shrinking number can't leave a
// stale trailing digit behind.
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

// ── Icon lookup helpers (linear search over the generated tables; small N, fine at 10Hz) ──
// Only the 12px status-bar set is looked up by name now; the 32px/16px tables went unused
// once the menu switched to drawn icons, so leaving their lookups out lets the linker drop
// those bitmaps entirely.
static const uint16_t* findIcon12(const char* name) {
  for (int i = 0; i < N_12; i++) if (strcmp(ICONS_12[i].name, name) == 0) return ICONS_12[i].data;
  return NULL;
}

static void drawIcon(int x, int y, const uint16_t* data, int size) {
  if (!data) return;
  tft.drawRGBBitmap(x, y, data, size, size);
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
    default:                  return COL_GREEN;  // IDLE
  }
}

// ── Status bar (top 13px): BT icon, title, battery ──
static void drawStatusBar() {
  if (fullRepaint) tft.fillRect(0, 0, SCREEN_W, HEADER_H, BG_PANEL);

  const uint16_t* bt = findIcon12(bleConnected ? "bt_on" : "bt_off");
  drawIcon(2, 0, bt, 12);

  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI, BG_PANEL);
  tft.setCursor(18, 2);
  tft.print("CAMERA SLIDER");

  int pct = batteryPercent();
  int level = (pct >= 75) ? 4 : (pct >= 50) ? 3 : (pct >= 25) ? 2 : (pct > 0) ? 1 : 0;
  char batName[8];
  snprintf(batName, sizeof(batName), "bat_%d", level);
  drawIcon(SCREEN_W - 14, 0, findIcon12(batName), 12);

  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, DIVIDER);
}

static void drawScreenHeader(const char* title, uint16_t color = 0) {
  if (!fullRepaint) return;
  if (color == 0) color = TEXT_PRI;
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(color, BG_PANEL);
  tft.setCursor(4, 2);
  tft.print(title);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, DIVIDER);
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
// right. The footer mirrors that arrangement with the real play/menu glyphs rather than
// abstract numeric labels, while the encoder action stays right-aligned.
static void drawButtonGlyph(int x, int y, bool primary, uint16_t color) {
  if (primary) {
    tft.fillTriangle(x, y, x, y + 7, x + 5, y + 3, color);
  } else {
    tft.drawFastHLine(x, y + 1, 6, color);
    tft.drawFastHLine(x, y + 3, 6, color);
    tft.drawFastHLine(x, y + 5, 6, color);
  }
}

static void drawHints(const char* btn1, const char* btn2, const char* enc,
                      bool btn1Dim = false, bool btn2Dim = false,
                      uint16_t footerBg = 0xFFFF) {
  // Hint text only ever changes when the screen changes, so on a same-screen content
  // update there is nothing to redraw here at all -- skipping it entirely keeps the
  // footer rock steady instead of repainting it 10x/second.
  if (!fullRepaint) return;
  uint16_t bg = footerBg == 0xFFFF ? BG_PANEL : footerBg;
  tft.fillRect(0, CONTENT_BOTTOM, SCREEN_W, FOOTER_H, bg);
  tft.drawFastHLine(0, CONTENT_BOTTOM, SCREEN_W, DIVIDER);
  tft.setTextSize(1);
  int x = 3;
  const int glyphY = CONTENT_BOTTOM + 4;
  const int textY = CONTENT_BOTTOM + 4;
  if (btn1) {
    uint16_t c = btn1Dim ? TEXT_DIM : TEXT_SEC;
    drawButtonGlyph(x, glyphY, true, c);
    x += 8;
    tft.setTextColor(c, bg);
    tft.setCursor(x, textY);
    tft.print(btn1);
    x += strlen(btn1) * 6 + 5;
  }
  if (btn2) {
    uint16_t c = btn2Dim ? TEXT_DIM : TEXT_SEC;
    drawButtonGlyph(x, glyphY, false, c);
    x += 8;
    tft.setTextColor(c, bg);
    tft.setCursor(x, textY);
    tft.print(btn2);
  }

  if (enc && enc[0]) {
    char right[24];
    snprintf(right, sizeof(right), "(o)%s", enc);
    int rightW = strlen(right) * 6;
    tft.setTextColor(COL_CYAN, bg);
    tft.setCursor(SCREEN_W - 3 - rightW, textY);
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
    if (sel) tft.fillRect(0, y, 2, rowH, COL_SEL);

    tft.setTextSize(1);
    tft.setTextColor(sel ? COL_SEL : TEXT_PRI);
    tft.setCursor(8, y + 6);
    tft.print(menuItemLabel(screen, idx));

    char valBuf[24];
    menuItemValueText(screen, idx, valBuf, sizeof(valBuf));
    if (valBuf[0]) {
      int vw = strlen(valBuf) * 6;
      tft.setTextColor(sel ? COL_SEL : TEXT_SEC);
      tft.setCursor(SCREEN_W - 6 - vw, y + 6);
      tft.print(valBuf);
    }
  }
}

// ── MAIN screen ──
static void renderMain() {
  clearContentBand();
  drawStatusBar();
  drawPositionBar(4, 18, SCREEN_W - 8);

  char value[20], buf[28];
  formatSteps(currentPosition, value, sizeof(value));
  if (!isCalibrated) strncat(value, "*", sizeof(value) - strlen(value) - 1);
  tft.setTextSize(3);
  int valueW = strlen(value) * 18;
  int valueX = (SCREEN_W - valueW) / 2;
  if (valueX < 2) valueX = 2;
  tft.fillRect(0, 34, SCREEN_W, 24, BG_BASE);
  textOpaque(valueX, 34, TEXT_PRI, value);

  tft.setTextSize(1);
  if (isCalibrated) {
    formatSteps(travelDistance, value, sizeof(value));
    snprintf(buf, sizeof(buf), "/ %s TOTAL STEPS", value);
  } else {
    snprintf(buf, sizeof(buf), "POSITION NOT CALIBRATED");
  }
  int labelW = strlen(buf) * 6;
  textOpaque((SCREEN_W - labelW) / 2, 60, TEXT_SEC, buf);

  snprintf(buf, sizeof(buf), "%u%%", cfg.speed);
  textOpaque(4, 77, TEXT_PRI, buf, 4);
  const int speedBarW = SCREEN_W - 58;
  int speedW = speedBarW * cfg.speed / 100;
  tft.fillRect(32, 79, speedBarW, 5, BG_CARD);
  if (speedW > 0) tft.fillRect(32, 79, speedW, 5, COL_CYAN);
  textOpaque(4, 94, stateColor(), stateToString(sliderState), 9);
  textOpaque(104, 94, COL_CYAN, motorRunning ? (motorDirection ? "<<<" : ">>>") : "   ", 3);

  drawHints("GO/STOP", "MENU", "GOTO");
}

// ── MENU (2x2 tiles) ──
static void renderMenu() {
  clearContentBand();
  drawScreenHeader("MENU", COL_PURPLE);

  int count = menuItemCount(SCREEN_MENU);
  const int cellW = SCREEN_W / 2, cellH = (CONTENT_BOTTOM - HEADER_H) / 2;
  static const char* badges[] = { "MOV", "POS", "CAL", "SET" };
  for (int i = 0; i < count && i < 4; i++) {
    int col = i % 2, row = i / 2;
    int x = col * cellW, y = HEADER_H + row * cellH;
    bool sel = (i == menuIndex);
    uint16_t tileBg = sel ? BG_HIGHLIGHT : BG_BASE;
    tft.fillRect(x, y, cellW, cellH, tileBg);
    tft.drawRect(x, y, cellW, cellH, DIVIDER);
    if (sel) {
      tft.drawRect(x + 1, y + 1, cellW - 2, cellH - 2, COL_SEL);
      tft.drawRect(x + 2, y + 2, cellW - 4, cellH - 4, COL_SEL);
    }

    uint16_t iconColor = i == 2 ? COL_AMBER : (i == 3 ? COL_PURPLE : (sel ? COL_SEL : TEXT_SEC));
    tft.fillRect(x + 29, y + 7, 22, 17, BG_CARD);
    tft.setTextSize(1);
    tft.setTextColor(iconColor, BG_CARD);
    tft.setCursor(x + 31, y + 12);
    tft.print(badges[i]);

    tft.setTextSize(1);
    tft.setTextColor(sel ? TEXT_PRI : TEXT_SEC, tileBg);
    const char* label = menuItemLabel(SCREEN_MENU, i);
    int lw = strlen(label) * 6;
    tft.setCursor(x + (cellW - lw) / 2, y + cellH - 13);
    tft.print(label);
  }

  drawHints("", "BACK", "OPEN");
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
  int valueW = strlen(value) * 18;
  tft.fillRect(0, 49, SCREEN_W, 24, BG_BASE);
  textOpaque((SCREEN_W - valueW) / 2, 49, COL_PURPLE, value);
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
    if (!fullRepaint) tft.fillRect(0, 38, SCREEN_W, 28, BG_BASE);
    textOpaque(x, 43, COL_CYAN, name);
    tft.setTextSize(1);
    textOpaque(52, 76, TEXT_SEC, "SELECT VALUE");
  } else {
    snprintf(buf, sizeof(buf), "%ld", (long)editValue);
    tft.setTextSize(3);
    int w = (int)strlen(buf) * 18;
    int x = (SCREEN_W - w) / 2;
    if (x < 0) x = 0;
    // Centred text shifts as the number's width changes, so clear only the margins either
    // side of it -- every pixel still gets written exactly once, no full-row blanking.
    if (x > 0) tft.fillRect(0, 37, x, 24, BG_BASE);
    tft.setTextColor(COL_CYAN, BG_BASE);
    tft.setCursor(x, 37);
    tft.print(buf);
    int right = x + w;
    if (right < SCREEN_W) tft.fillRect(right, 37, SCREEN_W - right, 24, BG_BASE);

    tft.setTextSize(1);
    textOpaque(4, 72, TEXT_DIM, "MIN");
    snprintf(buf, sizeof(buf), "%ld", (long)editMin);
    textOpaque(4, 82, TEXT_SEC, buf, 8);
    textOpaque(138, 72, TEXT_DIM, "MAX");
    snprintf(buf, sizeof(buf), "%ld", (long)editMax);
    int maxW = strlen(buf) * 6;
    textOpaque(SCREEN_W - 4 - maxW, 82, TEXT_SEC, buf);
  }

  if (!editValueNames) {
    int barX = 24, barY = 96, barW = SCREEN_W - 48;
    tft.fillRect(barX, barY, barW, 6, BG_CARD);
    float frac = (editMax > editMin) ? (float)(editValue - editMin) / (float)(editMax - editMin) : 0;
    tft.fillRect(barX, barY, (int)(barW * frac), 6, COL_CYAN);
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

  drawHints("RESET", "HOME", "", false, false, 0x2802);
}

// ── HOMING_CONFIRM ──
static void renderHomingConfirm() {
  clearFullBand();
  drawScreenHeader("! START HOMING?", COL_AMBER);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(20, 35);
  tft.print("Slider will move");
  tft.setCursor(20, 48);
  tft.print("to both endstops.");
  tft.setTextColor(TEXT_SEC);
  tft.setCursor(20, 70);
  tft.print("Keep rail clear.");

  drawHints("CANCEL", "", "START", false, true);
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
}

void displayOtaEnd(const char* msg, bool failed) {
  tft.setTextSize(1);
  textOpaque(14, 110, failed ? COL_RED : COL_GREEN, msg, 24);
}

void displayInit() {
  displaySetTheme(cfg.theme);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(BG_BASE);
}

void displayUpdate() {
  static unsigned long lastTick = 0;
  if (millis() - lastTick < 100) return;
  lastTick = millis();

  bool lerping = updatePositionLerp();

  // Full-screen redraws (every render*() starts with fillScreen()) flicker badly on real
  // SPI hardware if done unconditionally at 10Hz -- confirmed on the actual board: idle
  // screens with nothing changing were flickering just as hard as ones with live data.
  // Only redraw when something is actually different: screen navigation, an explicit
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

  displayDirty = false;
}
