# Icon Generation Prompts — Camera Slider UI

Документ содержит промпты для AI-генерации иконок. Структура:
- **System block** — вставляется в начало каждого промпта (стиль, палитра, ограничения)
- **Per-size templates** — шаблон для каждого из трёх размеров
- **Icon catalog** — готовые промпты для каждой иконки

Рекомендуемые инструменты: **Midjourney** (флаг `--style raw --ar 1:1`), **DALL-E 3**, **Stable Diffusion** с pixel-art LoRA, или любой инструмент с поддержкой pixel art стиля. Для финальной доводки до точных пикселей — **Aseprite**.

---

## System Block (вставлять в начало каждого промпта)

```
Pixel art icon for embedded 160x128 color LCD display (ST7735).
Style: clean technical pixel art, sharp edges, no anti-aliasing, no gradients, no glow effects.
Background: transparent (checkerboard).
Limited palette: exactly 2-3 colors from the provided list + transparent.
Rendered as flat opaque pixels only.
No shadow, no outline stroke unless it IS one of the colors.
Suitable for direct use as a bitmap sprite on a microcontroller display.
```

---

## Цветовая палитра (ссылочные значения для промптов)

```
CYAN     #00D4FF  — движение, активное, прогресс
GREEN    #00E87A  — OK, idle, готово
AMBER    #FFA020  — предупреждение, разгон
RED      #FF2840  — ошибка, стоп, эндстоп
PURPLE   #9060FF  — настройки
WHITE    #FFFFFF  — основной элемент, деталь
TEXT_SEC #7A90BB  — корпус, неактивные части
BG_CARD  #1C2540  — если нужен темный фон плитки
```

---

## Серия A — 32×32, главное меню (2–3 цвета)

### Шаблон

```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: [COLOR_1] for body/silhouette, [COLOR_2] for accent/highlight, [optional COLOR_3] for detail.
Subject: [ICON DESCRIPTION]
The icon should be centered, filling roughly 24x24 pixels of the 32x32 canvas (4px padding).
No text, no letters, no numbers inside the icon.
```

### Промпты серии A

**ic32_speed — спидометр**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for gauge body and tick marks, #00D4FF for the needle/pointer, #FFFFFF for the center dot.
Subject: speedometer gauge icon. Semicircular dial with evenly spaced tick marks along the arc. A sharp triangular needle pointing to the upper-right (high speed position). Small circle at needle pivot. Minimalist, technical, no labels.
The icon should be centered, filling roughly 26x26 pixels of the 32x32 canvas.
```

**ic32_current — ток мотора**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #FFA020 for the outer circle, #FFFFFF for the lightning bolt shape.
Subject: electric current icon. Filled circle with a clean lightning bolt (zigzag arrow, top to bottom) cut out or drawn inside it. The bolt should be thick enough to read at small size — 3px minimum stroke.
The icon should be centered, filling roughly 24x24 pixels of the 32x32 canvas.
```

**ic32_microstep — деление шага**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for bracket lines and grid dots, #00D4FF for the highlighted center dot.
Subject: microstep grid icon. Two square brackets [ ] on left and right sides. Inside: a 3x3 or 4x4 regular grid of small square dots. The center dot is a different color (accent). Represents fine subdivision/resolution.
The icon should be centered, filling roughly 24x24 pixels of the 32x32 canvas.
```

**ic32_ramp — разгон/торможение**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #00D4FF for the ramp/trapezoid shape, #7A90BB for baseline and optional axis lines.
Subject: acceleration ramp icon. A trapezoid shape viewed from the front — flat top, slanted left side (ramp up), vertical right side, flat bottom baseline. Represents motor acceleration profile. Clean geometric shape.
The icon should be centered, filling roughly 26x20 pixels of the 32x32 canvas, vertically centered.
```

**ic32_home — хоминг**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #00E87A for the roof triangle, #FFFFFF for walls and door opening.
Subject: simple house/home icon. Equilateral triangle roof on top, square body below, small centered rectangular door cutout at the bottom of the body. Classic minimal house silhouette. No windows.
The icon should be centered, filling roughly 24x26 pixels of the 32x32 canvas.
```

**ic32_sleep — таймаут сна**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #9060FF for the crescent moon, #FFFFFF for three small star dots.
Subject: sleep/night icon. A crescent moon (right-facing, thick, smooth arc shape in pixel art). Three small 2x2 pixel star dots scattered in the upper-right area around the moon. Minimal, clear silhouette.
The icon should be centered, filling roughly 24x24 pixels of the 32x32 canvas.
```

**ic32_endstop — режим эндстопов**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for the horizontal rail/track, #FF2840 for the two end stops/bumpers.
Subject: rail with end stops icon. A horizontal line (rail/track) spanning the full width of the icon. At each end, a thick vertical bar (end stop) — like the letter H rotated 90 degrees, or railroad bumpers. Arrow heads pointing inward from both stops toward center (optional, if readable at this size).
The icon should be centered vertically, filling roughly 28x12 pixels centered in the 32x32 canvas.
```

**ic32_adxl — акселерометр / детектор наклона**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for the cube body faces, #00D4FF for the three axis lines (X, Y, Z arrows).
Subject: 3D accelerometer icon. A small isometric cube in the lower-left area of the icon. Three axis arrows (X rightward, Y upward, Z toward viewer at isometric angle) extending from the cube's corner. Each arrow is a line with a small arrowhead. Represents 3-axis measurement.
The icon should be centered, filling roughly 26x26 pixels of the 32x32 canvas.
```

**ic32_settings — системные настройки**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for the gear body/teeth, #FFFFFF for the center circle cutout.
Subject: settings gear/cog icon. A single gear with 6-8 evenly spaced rectangular teeth around the perimeter. A circle cutout in the center. Symmetrical, clean, classic settings icon. Teeth should be distinct and readable at 32px.
The icon should be centered, filling roughly 26x26 pixels of the 32x32 canvas.
```

**ic32_ble — Bluetooth / соединение**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #00D4FF for all elements.
Subject: Bluetooth symbol icon. Classic Bluetooth logo: vertical line with two diagonal lines forming a shape like a stylized 'B' — upper-right diagonal and lower-right diagonal crossing the vertical. The symbol should be thick enough (2-3px strokes) to read at small size.
The icon should be centered, filling roughly 14x24 pixels of the 32x32 canvas.
```

**ic32_calib — калибровка / измерение**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for the ruler body, #FFA020 for the position marker/arrow.
Subject: calibration ruler icon. A horizontal ruler bar with 5-7 evenly spaced tick marks of alternating heights (major and minor ticks). Below the ruler, a downward-pointing triangle marker (like a position caret) positioned at roughly 2/3 along the ruler. Represents measured travel distance.
The icon should be centered, filling roughly 28x16 pixels of the 32x32 canvas, vertically centered.
```

**ic32_goto — переход к позиции**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #00D4FF for the arrow and dashed path, #FFFFFF for the destination flag/marker.
Subject: go-to position icon. A dashed horizontal line (path) going left to right. At the right end, a small flag or vertical marker (like a finish line post). An arrow head pointing right at the end of the path. Represents moving to a target position.
The icon should be centered, filling roughly 28x14 pixels of the 32x32 canvas, vertically centered.
```

**ic32_display — настройки экрана**
```
[SYSTEM BLOCK]
Canvas: 32x32 pixels.
Colors: #7A90BB for the monitor/screen frame, #FFA020 for the brightness symbol.
Subject: display settings icon. A small rounded rectangle (monitor/screen frame) taking up left 2/3 of icon. Inside the screen: a simplified sun symbol (small circle with 4 short rays at cardinal directions) representing brightness. Clean, readable.
The icon should be centered, filling roughly 26x22 pixels of the 32x32 canvas.
```

---

## Серия B — 16×16, подменю (2 цвета)

### Шаблон

```
[SYSTEM BLOCK]
Canvas: 16x16 pixels.
Colors: [COLOR_1] for main shape, [COLOR_2] for accent (if needed). Maximum 2 colors + transparent.
Subject: [SIMPLIFIED VERSION OF 32x32 ICON — remove small details, keep silhouette only]
Pixel art, very simplified, readable silhouette. At 16px: keep only the essential shape, drop all fine details (no tick marks, no minor elements). The silhouette alone must be immediately recognizable.
The icon should fill roughly 12x12 pixels centered in the 16x16 canvas.
```

**Промпты серии B** — для каждой иконки добавить в шаблон:

| Иконка | Subject для 16×16 | Цвет 1 | Цвет 2 |
|---|---|---|---|
| `ic16_speed` | Semicircular dial arc with a single needle, no ticks | #7A90BB | #00D4FF |
| `ic16_current` | Circle with lightning bolt inside | #FFA020 | #FFFFFF |
| `ic16_microstep` | 3x3 grid of dots, center dot different | #7A90BB | #00D4FF |
| `ic16_ramp` | Trapezoid shape, flat top, slanted left side | #00D4FF | — |
| `ic16_home` | Triangle roof + square body + door, minimal | #00E87A | #FFFFFF |
| `ic16_sleep` | Crescent moon only, no stars | #9060FF | — |
| `ic16_endstop` | Short horizontal line with thick bars at each end | #7A90BB | #FF2840 |
| `ic16_adxl` | Three axis arrows from a point, no cube | #7A90BB | #00D4FF |
| `ic16_settings` | 6-tooth gear with center hole | #7A90BB | — |
| `ic16_ble` | Bluetooth symbol, thick strokes | #00D4FF | — |
| `ic16_calib` | Ruler with 3 ticks + triangle marker | #7A90BB | #FFA020 |
| `ic16_goto` | Arrow pointing right with small flag at tip | #00D4FF | #FFFFFF |
| `ic16_display` | Rectangle frame with sun symbol inside | #7A90BB | #FFA020 |
| `ic16_high_contrast` | Half-black half-white circle (yin-yang style, simplified) | #FFFFFF | — |

---

## Серия C — 12×12, статусбар (1–2 цвета)

### Шаблон

```
[SYSTEM BLOCK]
Canvas: 12x12 pixels.
Colors: [COLOR] only + transparent. Absolute maximum 2 colors.
Subject: [ICON DESCRIPTION — extreme silhouette only]
At 12px everything is a silhouette. 1-2px strokes maximum. The entire icon must be readable in a 12x12 box at arm's length on a small screen. Reduce to pure geometric primitive.
```

**Промпты серии C:**

| Иконка | Subject | Цвет |
|---|---|---|
| `ic12_bt_on` | Bluetooth symbol, 2px stroke | #00D4FF |
| `ic12_bt_off` | Bluetooth symbol with diagonal strike-through line | #3A4A66 |
| `ic12_bat_0` | Battery outline (rectangle + small nub), empty inside | #FF2840 |
| `ic12_bat_1` | Battery outline with 1 filled segment on left | #FFA020 |
| `ic12_bat_2` | Battery outline with 2 filled segments | #FFA020 |
| `ic12_bat_3` | Battery outline with 3 filled segments | #00E87A |
| `ic12_bat_4` | Battery outline fully filled | #00E87A |
| `ic12_hc_on` | Small "HC" text replaced by: a square half-filled diagonally (contrast symbol) | #FFFFFF |

---

## Серия D — 10×10, кнопки-подсказки (монохром)

Для этой серии AI-генерация избыточна — проще нарисовать вручную в Aseprite или использовать встроенные глифы шрифта. Если всё же нужен промпт:

```
[SYSTEM BLOCK]
Canvas: 10x10 pixels.
Colors: #FFFFFF only + transparent. Pure monochrome.
Subject: [ICON] as a minimal 1-bit icon. Maximum 2px stroke width. Must be readable at 10x10.
```

| Иконка | Subject |
|---|---|
| `ic10_fwd` | Right-pointing filled triangle (play button) |
| `ic10_bwd` | Left-pointing filled triangle |
| `ic10_stop` | Filled square |
| `ic10_ok` | Checkmark, L-shape rotated, 2px stroke |
| `ic10_cancel` | X cross, 2px stroke, diagonal lines |
| `ic10_back` | Left arrow with tail curving up (return arrow) |
| `ic10_menu` | Three horizontal lines, equal spacing |
| `ic10_home` | Triangle on top of square, minimal |
| `ic10_reset` | Circular arrow (arc with arrowhead, open circle) |
| `ic10_error` | Exclamation mark centered |

---

## Добавление новой иконки — чеклист

1. Определить серию (A/B/C/D) по контексту использования
2. Скопировать шаблон нужной серии
3. Заполнить поля:
   - `Canvas` — из шаблона (не менять)
   - `Colors` — выбрать из палитры выше, не более 3 цветов для A, 2 для B/C
   - `Subject` — описать форму, начиная с самого крупного элемента к мелкому
4. Добавить строку в таблицу иконок в `docs/07_ui_kit.md`
5. Сгенерировать, при необходимости довести в Aseprite до точной сетки

---

## Советы по доводке в Aseprite

После генерации AI-изображения:

1. Открыть в Aseprite, масштаб 800%
2. `Sprite → Color Mode → Indexed` → задать палитру из 2–3 нужных цветов
3. `Edit → Replace Color` — убрать все оттенки, которых нет в палитре
4. Вручную почистить пиксели на границах (избавиться от антиалиасинга)
5. Экспортировать как PNG с прозрачностью
6. Конвертировать в C-массив PROGMEM через утилиту (см. следующий раздел)

### Конвертация PNG → C-массив

Для LovyanGFX/TFT_eSPI иконки хранятся как `const uint8_t icon_name[] PROGMEM`.

Инструменты:
- **image2cpp** (онлайн, javl.github.io/image2cpp) — для 1-bit и RGB565
- **LVGL Image Converter** (онлайн) — для indexed и RGB565
- Кастомный Python-скрипт (см. ниже)

```python
# tools/png_to_progmem.py — базовый скрипт конвертации
# Использование: python png_to_progmem.py icon_speed_32.png ic32_speed
from PIL import Image
import sys

def convert(filename, varname):
    img = Image.open(filename).convert('RGBA')
    w, h = img.size
    pixels = list(img.getdata())

    print(f"// {w}x{h} px")
    print(f"const uint16_t {varname}[{w*h}] PROGMEM = {{")
    row = []
    for r, g, b, a in pixels:
        if a < 128:
            rgb = 0x0000  # transparent → black (use colorkey)
        else:
            rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        row.append(f"0x{rgb:04X}")
        if len(row) == w:
            print("  " + ", ".join(row) + ",")
            row = []
    print("};")

if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
```
