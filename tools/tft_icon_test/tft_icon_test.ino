/*
 * tft_icon_test.ino
 * Отображает все иконки слайдера на ST7735 160×128
 *
 * Подключение:
 *   TFT CS  → GPIO 5
 *   TFT RST → GPIO 4
 *   TFT DC  → GPIO 2
 *   SPI MOSI/SCK — стандартные ESP32 (MOSI=23, SCK=18)
 *
 * Страница 1: иконки 32×32 (Series A)
 * Страница 2: иконки 16×16 (Series B)
 * Страница 3: иконки 12×12 (Series C)  — авто-смена каждые PAGE_MS мс
 *
 * Перед прошивкой запустить:
 *   python3 tools/icons_to_h.py
 * чтобы сгенерировать icons_tft.h
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include "icons_tft.h"

#define TFT_CS   5
#define TFT_RST  4
#define TFT_DC   2

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// ── Цвета (RGB565) ──────────────────────────────────────────
#define COL_BG     0x0000   // чёрный фон
#define COL_TITLE  0xFFFF   // белый заголовок
#define COL_LABEL  0x7BEF   // светло-серый подпись

// ── Параметры ────────────────────────────────────────────────
static const unsigned long PAGE_MS = 6000;  // смена страниц

// ── Вспомогательные функции ───────────────────────────────────

// Заголовок страницы
void drawHeader(const char* text) {
  tft.setTextColor(COL_TITLE);
  tft.setTextSize(1);
  tft.setCursor(2, 2);
  tft.print(text);
  // линия-разделитель
  tft.drawFastHLine(0, 11, 160, 0x4208);
}

// Рисует иконку + подпись по центру иконки снизу
void drawIcon(int x, int y, const uint16_t* data, int w, int h, const char* label) {
  tft.drawRGBBitmap(x, y, data, w, h);
  tft.setTextColor(COL_LABEL);
  tft.setTextSize(1);
  // центрируем подпись
  int labelW = strlen(label) * 6;
  int lx = x + (w - labelW) / 2;
  if (lx < 0) lx = 0;
  tft.setCursor(lx, y + h + 1);
  tft.print(label);
}

// ── Страница 1: 32×32 ─────────────────────────────────────────
void drawPage32() {
  tft.fillScreen(COL_BG);
  drawHeader("32 x 32  (Series A)");

  // 5 колонок × 3 строки, каждая ячейка 32×41 (иконка + 7px для подписи + 2px отступ)
  const int COLS    = 5;
  const int CELL_W  = 32;
  const int CELL_H  = 41;
  const int START_Y = 13;

  for (int i = 0; i < N_32; i++) {
    int col = i % COLS;
    int row = i / COLS;
    int x   = col * CELL_W;
    int y   = START_Y + row * CELL_H;
    drawIcon(x, y, ICONS_32[i].data, 32, 32, ICONS_32[i].name);
  }
}

// ── Страница 2: 16×16 ─────────────────────────────────────────
void drawPage16() {
  tft.fillScreen(COL_BG);
  drawHeader("16 x 16  (Series B)");

  // 8 колонок × 2 строки, ячейка 20×30 (иконка 16 + 8 подпись + 6 отступ)
  const int COLS    = 8;
  const int CELL_W  = 20;
  const int CELL_H  = 30;
  const int START_Y = 13;

  for (int i = 0; i < N_16; i++) {
    int col = i % COLS;
    int row = i / COLS;
    int x   = col * CELL_W;
    int y   = START_Y + row * CELL_H;
    drawIcon(x, y, ICONS_16[i].data, 16, 16, ICONS_16[i].name);
  }
}

// ── Страница 3: 12×12 ─────────────────────────────────────────
void drawPage12() {
  tft.fillScreen(COL_BG);
  drawHeader("12 x 12  (Series C)");

  // Все 7 иконок в одну строку, центрируем вертикально
  const int CELL_W  = 22;
  const int TOTAL_W = N_12 * CELL_W;
  const int START_X = (160 - TOTAL_W) / 2;
  const int START_Y = 50;  // вертикально по центру

  for (int i = 0; i < N_12; i++) {
    int x = START_X + i * CELL_W;
    drawIcon(x, START_Y, ICONS_12[i].data, 12, 12, ICONS_12[i].name);
  }
}

// ── Страница 4: одна иконка крупно (4× zoom) ─────────────────
// Проходит по всем 32×32 иконкам по одной
static int zoomIdx = 0;

void drawPageZoom() {
  tft.fillScreen(COL_BG);

  const uint16_t* src = ICONS_32[zoomIdx].data;
  const char*     nm  = ICONS_32[zoomIdx].name;

  char hdr[32];
  snprintf(hdr, sizeof(hdr), "zoom: %s (4x)", nm);
  drawHeader(hdr);

  // Рисуем 32×32 → 128×128, каждый пиксель 4×4
  const int SCALE = 4;
  const int OX    = (160 - 32 * SCALE) / 2;  // центрировать по X
  const int OY    = 14;

  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      uint16_t color = src[y * 32 + x];
      if (color == 0x0000) continue;  // пропустить прозрачные
      tft.fillRect(OX + x * SCALE, OY + y * SCALE, SCALE, SCALE, color);
    }
  }

  zoomIdx = (zoomIdx + 1) % N_32;
}

// ── setup / loop ──────────────────────────────────────────────

static int      currentPage  = 0;
static unsigned long lastSwitch = 0;

// Всего страниц: 3 обзора + по одной zoom на каждую 32×32 иконку
static const int TOTAL_OVERVIEW = 3;

void showPage(int page) {
  if (page == 0) {
    drawPage32();
  } else if (page == 1) {
    drawPage16();
  } else if (page == 2) {
    drawPage12();
  } else {
    drawPageZoom();
  }
}

static int totalPages() {
  return TOTAL_OVERVIEW + N_32;  // 3 обзора + N zoom-страниц
}

void setup() {
  Serial.begin(115200);
  Serial.println("TFT icon test start");

  tft.initR(INITR_BLACKTAB);  // попробуй INITR_GREENTAB / INITR_REDTAB если цвета неверные
  tft.setRotation(1);         // landscape 160×128
  tft.fillScreen(COL_BG);

  showPage(currentPage);
  lastSwitch = millis();
}

void loop() {
  if (millis() - lastSwitch >= PAGE_MS) {
    currentPage = (currentPage + 1) % totalPages();
    showPage(currentPage);
    lastSwitch = millis();
    Serial.printf("Page %d / %d\n", currentPage + 1, totalPages());
  }
}
