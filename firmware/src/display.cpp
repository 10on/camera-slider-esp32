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
  if (fullRepaint) tft.fillRect(0, 12, SCREEN_W, SCREEN_H - 24, BG_BASE);
}
static void clearFullBand() {  // screens with no status bar
  if (fullRepaint) tft.fillRect(0, 0, SCREEN_W, SCREEN_H - 12, BG_BASE);
}

// Opaque, fixed-width text: pads to `width` chars so shrinking values self-erase.
static void textOpaque(int x, int y, uint16_t fg, const char* s, int width = 0) {
  tft.setTextColor(fg, BG_BASE);
  tft.setCursor(x, y);
  tft.print(s);
  for (int i = (int)strlen(s); i < width; i++) tft.print(' ');
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

// ── Status bar (top 12px): BT icon, title, battery ──
static void drawStatusBar() {
  if (fullRepaint) tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);

  const uint16_t* bt = findIcon12(bleConnected ? "bt_on" : "bt_off");
  drawIcon(2, 0, bt, 12);

  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI, BG_PANEL);
  tft.setCursor(18, 2);
  tft.print("Camera Slider");

  int pct = batteryPercent();
  int level = (pct >= 75) ? 4 : (pct >= 50) ? 3 : (pct >= 25) ? 2 : (pct > 0) ? 1 : 0;
  char batName[8];
  snprintf(batName, sizeof(batName), "bat_%d", level);
  drawIcon(SCREEN_W - 14, 0, findIcon12(batName), 12);

  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);
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
// Physical layout (confirmed on the actual enclosure): BTN1 and BTN2 are both mounted on
// the LEFT flank of the case, the encoder (rotate+press) is on the RIGHT flank. So the
// footer groups both button hints together on the left, tagged with their silkscreen
// numbers ("1:"/"2:", matching the physical "1"/"2" printed next to the keys), and the
// encoder hint alone on the right behind a small dial glyph -- not left/mid/right
// text slots, which used to silently imply BTN1-left/encoder-center/BTN2-right (wrong).
static void drawHints(const char* btn1, const char* btn2, const char* enc) {
  // Hint text only ever changes when the screen changes, so on a same-screen content
  // update there is nothing to redraw here at all -- skipping it entirely keeps the
  // footer rock steady instead of repainting it 10x/second.
  if (!fullRepaint) return;
  tft.fillRect(0, SCREEN_H - 12, SCREEN_W, 12, BG_PANEL);
  tft.drawFastHLine(0, SCREEN_H - 12, SCREEN_W, DIVIDER);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_SEC, BG_PANEL);

  char left[32];
  left[0] = '\0';
  if (btn1 && btn1[0]) { strcat(left, "1:"); strcat(left, btn1); }
  if (btn2 && btn2[0]) { if (left[0]) strcat(left, "  "); strcat(left, "2:"); strcat(left, btn2); }
  tft.setCursor(2, SCREEN_H - 9);
  tft.print(left);

  if (enc && enc[0]) {
    char right[24];
    snprintf(right, sizeof(right), "(o)%s", enc);
    int rightW = strlen(right) * 6;
    tft.setCursor(SCREEN_W - 2 - rightW, SCREEN_H - 9);
    tft.print(right);
  }
}

// ── Generic vertical list (Menu tiles fallback list, Settings/Motion/Sleep/System) ──
static void drawList(MenuScreen screen) {
  int count = menuItemCount(screen);
  const int rowH = 18;
  const int startY = 14;
  int visible = (SCREEN_H - 14 - 12) / rowH;

  if (menuIndex < menuOffset) menuOffset = menuIndex;
  if (menuIndex >= menuOffset + visible) menuOffset = menuIndex - visible + 1;

  for (int i = 0; i < visible && (menuOffset + i) < count; i++) {
    int idx = menuOffset + i;
    int y = startY + i * rowH;
    bool sel = (idx == menuIndex);
    tft.fillRect(0, y, SCREEN_W, rowH, sel ? BG_HIGHLIGHT : BG_BASE);
    if (sel) tft.drawFastVLine(0, y, rowH, COL_SEL);

    tft.setTextSize(1);
    tft.setTextColor(sel ? COL_SEL : TEXT_PRI);
    tft.setCursor(6, y + 5);
    tft.print(menuItemLabel(screen, idx));

    char valBuf[24];
    menuItemValueText(screen, idx, valBuf, sizeof(valBuf));
    if (valBuf[0]) {
      int vw = strlen(valBuf) * 6;
      tft.setTextColor(sel ? COL_SEL : TEXT_SEC);
      tft.setCursor(SCREEN_W - 6 - vw, y + 5);
      tft.print(valBuf);
    }
  }
}

// ── MAIN screen ──
static void renderMain() {
  clearContentBand();
  drawStatusBar();
  drawPositionBar(4, 18, SCREEN_W - 8);

  char buf[16];
  snprintf(buf, sizeof(buf), isCalibrated ? "%ld" : "%ld*", (long)currentPosition);
  tft.setTextSize(3);
  textOpaque(4, 34, TEXT_PRI, buf, 8);

  tft.setTextSize(1);
  textOpaque(4, 60, TEXT_SEC, isCalibrated ? "steps" : "steps (not calibrated)", 24);

  snprintf(buf, sizeof(buf), "%u%%", cfg.speed);
  textOpaque(4, 76, TEXT_SEC, buf, 6);
  textOpaque(40, 76, stateColor(), stateToString(sliderState), 8);
  textOpaque(96, 76, COL_CYAN, motorRunning ? (motorDirection ? "<<" : ">>") : "  ", 2);

  drawHints("GO/STOP", "MENU", "GOTO");
}

// ── MENU (2x2 tiles) ──
//
// Menu icons are drawn with GFX primitives rather than pulled from the generated 32x32
// RGB565 bitmap set. Two reasons, both practical: the rendered artwork turned to mush at
// 32px on this panel (only the speedometer stayed legible), and a fixed-color bitmap
// can't follow the theme or the selection highlight. Line art at this size is also far
// cheaper -- dropping the 32px table frees the ~28KB of flash those bitmaps occupied.
// Each helper draws centred on (cx, cy) in a single colour.

static void iconMove(int cx, int cy, uint16_t c, uint16_t bg) {
  (void)bg;
  // Double-headed horizontal arrow with a carriage block: "move along the rail".
  for (int t = -1; t <= 1; t++) tft.drawFastHLine(cx - 13, cy + t, 26, c);
  tft.fillTriangle(cx - 14, cy, cx - 7, cy - 6, cx - 7, cy + 6, c);
  tft.fillTriangle(cx + 14, cy, cx + 7, cy - 6, cx + 7, cy + 6, c);
  tft.fillRect(cx - 3, cy - 8, 6, 16, c);
}

static void iconTarget(int cx, int cy, uint16_t c, uint16_t bg) {
  (void)bg;
  // Crosshair over a centre dot: "go to a specific position".
  tft.drawCircle(cx, cy, 10, c);
  tft.drawCircle(cx, cy, 9, c);
  tft.drawFastHLine(cx - 15, cy, 8, c);
  tft.drawFastHLine(cx + 8,  cy, 8, c);
  tft.drawFastVLine(cx, cy - 15, 8, c);
  tft.drawFastVLine(cx, cy + 8,  8, c);
  tft.fillCircle(cx, cy, 3, c);
}

static void iconRuler(int cx, int cy, uint16_t c, uint16_t bg) {
  (void)bg;
  // Ruler with graduations: "measure the travel" (calibration/homing).
  tft.drawRect(cx - 14, cy - 8, 28, 16, c);
  tft.drawRect(cx - 13, cy - 7, 26, 14, c);
  for (int i = 1; i < 6; i++) {
    int x = cx - 14 + i * 5;
    int len = (i % 2 == 0) ? 8 : 5;   // alternating long/short ticks
    tft.drawFastVLine(x, cy - 7, len, c);
  }
}

static void iconGear(int cx, int cy, uint16_t c, uint16_t bg) {
  // Eight-tooth gear: settings. The hub is punched out in the tile's own background so it
  // reads as a hole on both the normal and the highlighted tile.
  for (int i = 0; i < 8; i++) {
    float a = i * (float)PI / 4.0f;
    int tx = cx + (int)(cosf(a) * 12.0f);
    int ty = cy + (int)(sinf(a) * 12.0f);
    tft.fillRect(tx - 3, ty - 3, 6, 6, c);
  }
  tft.fillCircle(cx, cy, 10, c);
  tft.fillCircle(cx, cy, 4, bg);
}

typedef void (*MenuIconFn)(int, int, uint16_t, uint16_t);
static const MenuIconFn MENU_ICON_FNS[4] = { iconMove, iconTarget, iconRuler, iconGear };

static void renderMenu() {
  clearContentBand();
  drawStatusBar();

  int count = menuItemCount(SCREEN_MENU);
  const int cellW = SCREEN_W / 2, cellH = (SCREEN_H - 12 - 12) / 2;
  for (int i = 0; i < count && i < 4; i++) {
    int col = i % 2, row = i / 2;
    int x = col * cellW, y = 12 + row * cellH;
    bool sel = (i == menuIndex);
    tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, sel ? BG_HIGHLIGHT : BG_CARD);
    if (sel) tft.drawRect(x + 1, y + 1, cellW - 2, cellH - 2, COL_SEL);

    MENU_ICON_FNS[i](x + cellW / 2, y + 20, sel ? COL_SEL : TEXT_SEC,
                     sel ? BG_HIGHLIGHT : BG_CARD);

    tft.setTextSize(1);
    tft.setTextColor(sel ? COL_SEL : TEXT_PRI);
    const char* label = menuItemLabel(SCREEN_MENU, i);
    int lw = strlen(label) * 6;
    tft.setCursor(x + (cellW - lw) / 2, y + cellH - 10);
    tft.print(label);
  }

  drawHints("BACK", "MAIN", "SELECT");
}

// ── MANUAL_MOVE ──
static void renderManualMove() {
  clearContentBand();
  drawStatusBar();

  tft.setTextSize(2);
  textOpaque(20, 30, motorRunning ? COL_CYAN : TEXT_SEC,
             motorRunning ? (motorDirection ? "<< REV" : ">> FWD") : "STOPPED", 7);

  char buf[16];
  snprintf(buf, sizeof(buf), "Speed: %u%%", cfg.speed);
  tft.setTextSize(1);
  textOpaque(4, 60, TEXT_SEC, buf, 12);

  drawHints("BACK", "MAIN", "REVERSE");
}

// ── GO_TO_POS ──
static void renderGoToPos() {
  clearContentBand();
  drawStatusBar();
  tft.setTextSize(1);

  if (!isCalibrated) {
    textOpaque(10, 50, COL_AMBER, "Not calibrated", 16);
    drawHints("BACK", "MAIN", "");
    return;
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "Current: %ld", (long)currentPosition);
  textOpaque(4, 20, TEXT_SEC, buf, 18);
  snprintf(buf, sizeof(buf), "Target:  %ld", (long)cmdTargetPos);
  textOpaque(4, 32, TEXT_SEC, buf, 18);
  snprintf(buf, sizeof(buf), "Travel:  %ld", (long)travelDistance);
  textOpaque(4, 44, TEXT_SEC, buf, 18);

  drawPositionBar(4, 60, SCREEN_W - 8);

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
  drawStatusBar();

  tft.setTextSize(1);
  char buf[24];

  if (sliderState == STATE_HOMING) {
    // Phase names differ in length ("Seeking END1..." vs "Done"), so pad -- otherwise the
    // tail of the previous, longer phase name stays on screen.
    textOpaque(10, 30, COL_AMBER, homingPhaseText(), 16);

    int phase = homingPhaseIndex();
    snprintf(buf, sizeof(buf), "Phase %d / 6", phase);
    textOpaque(10, 46, TEXT_SEC, buf, 14);

    int barW = SCREEN_W - 20, barX = 10, barY = 60;
    int done = barW * phase / 6;
    if (done > 0)     tft.fillRect(barX, barY, done, 8, COL_AMBER);
    if (done < barW)  tft.fillRect(barX + done, barY, barW - done, 8, BG_CARD);

    drawHints("", "ABORT", "");
  } else {
    if (isCalibrated) {
      textOpaque(10, 30, TEXT_PRI, "Calibrated", 16);
      snprintf(buf, sizeof(buf), "Travel: %ld", (long)travelDistance);
      textOpaque(10, 44, TEXT_SEC, buf, 18);
      snprintf(buf, sizeof(buf), "Center: %ld", (long)centerPosition);
      textOpaque(10, 56, TEXT_SEC, buf, 18);
    } else {
      textOpaque(10, 30, TEXT_PRI, "Not calibrated", 16);
      textOpaque(10, 44, TEXT_SEC, "", 18);
      textOpaque(10, 56, TEXT_SEC, "", 18);
    }
    drawHints("BACK", "MAIN", "START");
  }
}

// ── Settings-family lists (Menu-level Settings, and the 3 sub-lists) ──
static void renderSettingsList(MenuScreen screen, const char* title) {
  clearContentBand();
  tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(4, 2);
  tft.print(title);
  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);

  drawList(screen);
  drawHints("BACK", "MAIN", "SELECT");
}

// ── VALUE_EDIT ──
static void renderValueEdit() {
  clearContentBand();
  tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(4, 2);
  tft.print(editLabel ? editLabel : "");
  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);

  char buf[16];
  if (editValueNames) {
    // Size 2, not 3: the longest name in use ("High Contrast", 13 chars) is 156px at size
    // 2 and 234px at size 3 -- at size 3 it simply ran off the 160px-wide panel.
    // Padded to the full row width so a shorter name can't leave the tail of a longer one
    // behind (this is what produced "Onf" when On replaced Off).
    tft.setTextSize(2);
    textOpaque(4, 44, COL_CYAN, editValueNames[editValue], 13);
  } else {
    snprintf(buf, sizeof(buf), "%ld", (long)editValue);
    tft.setTextSize(3);
    int w = (int)strlen(buf) * 18;
    int x = (SCREEN_W - w) / 2;
    if (x < 0) x = 0;
    // Centred text shifts as the number's width changes, so clear only the margins either
    // side of it -- every pixel still gets written exactly once, no full-row blanking.
    if (x > 0) tft.fillRect(0, 44, x, 24, BG_BASE);
    tft.setTextColor(COL_CYAN, BG_BASE);
    tft.setCursor(x, 44);
    tft.print(buf);
    int right = x + w;
    if (right < SCREEN_W) tft.fillRect(right, 44, SCREEN_W - right, 24, BG_BASE);
  }

  if (!editValueNames) {
    int barX = 10, barY = 84, barW = SCREEN_W - 20;
    tft.fillRect(barX, barY, barW, 6, BG_CARD);
    float frac = (editMax > editMin) ? (float)(editValue - editMin) / (float)(editMax - editMin) : 0;
    tft.fillRect(barX, barY, (int)(barW * frac), 6, COL_CYAN);
  }

  drawHints("CANCEL", "", "SAVE");
}

// ── ERROR ──
static void renderError() {
  bool pulse = (millis() / 400) % 2 == 0;
  tft.fillScreen(pulse ? BG_BASE : 0x2000);
  tft.setTextSize(2);
  tft.setTextColor(COL_RED);
  tft.setCursor(20, 20);
  tft.print("ERROR");

  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(10, 50);
  tft.print(errorToString(errorCode));

  drawHints("RESET", "RESET+HOME", "");
}

// ── HOMING_CONFIRM ──
static void renderHomingConfirm() {
  clearFullBand();
  tft.drawRect(10, 30, SCREEN_W - 20, 50, COL_AMBER);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(20, 42);
  tft.print("Start homing?");
  tft.setTextColor(TEXT_SEC);
  tft.setCursor(20, 58);
  tft.print("Slider will move");
  tft.setCursor(20, 68);
  tft.print("to both endstops.");

  drawHints("CANCEL", "CANCEL", "CONFIRM");
}

// ── WIFI_SCAN (reuses drawList(), swaps in a "Scanning..." message mid-scan) ──
static void renderWifiScan() {
  clearContentBand();
  tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI);
  tft.setCursor(4, 2);
  tft.print("Networks");
  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);

  // Scanning->results is a layout change, not just a content change, so the "Scanning..."
  // line has to be wiped explicitly -- clearContentBand() only fires on a screen change
  // and would otherwise leave it showing underneath the results list.
  static bool wasScanning = false;
  bool scanning = wifiScanInProgress();
  if (scanning != wasScanning) {
    tft.fillRect(0, 12, SCREEN_W, SCREEN_H - 24, BG_BASE);
    wasScanning = scanning;
  }

  if (scanning) {
    textOpaque(30, 56, TEXT_SEC, "Scanning...", 12);
  } else {
    drawList(SCREEN_WIFI_SCAN);
  }
  drawHints("BACK", "MAIN", "SELECT");
}

// ── TEXT_EDIT (character wheel for STA/AP password entry) ──
static void renderTextEdit() {
  clearContentBand();
  tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI, BG_PANEL);
  tft.setCursor(4, 2);
  tft.print(editTextLabel);
  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);

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
  tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI, BG_PANEL);
  tft.setCursor(4, 2);
  tft.print("Diagnostics");
  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);

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
  tft.fillRect(0, 0, SCREEN_W, 12, BG_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(TEXT_PRI, BG_PANEL);
  tft.setCursor(4, 2);
  tft.print("Motor Test");
  tft.drawFastHLine(0, 12, SCREEN_W, DIVIDER);

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

  textOpaque(4, 90,  TEXT_DIM, "1=back 2=fwd tap", 20);
  textOpaque(4, 100, TEXT_DIM, "turn=speed (o)=stop", 20);
  textOpaque(4, 110, TEXT_DIM, "hold 1+2 3s = exit", 20);
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
