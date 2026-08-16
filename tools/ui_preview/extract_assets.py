#!/usr/bin/env python3
"""Extract the real font and status-bar icons the firmware draws with, into assets.js.

The UI preview renderer (index.html) mirrors firmware/src/display.cpp so that the
screenshots handed to a designer are pixel-accurate rather than an approximation. That
only holds if it uses the *same* glyph bitmaps and the *same* icon data as the device:

  * GFX_FONT  - Adafruit GFX's classic 5x7 font (glcdfont.c), 256 glyphs x 5 column bytes.
  * ICONS12   - the 12x12 RGB565 status-bar icons (Bluetooth on/off, battery levels) from
                the generated firmware/include/icons_tft.h.

Run this again if either source changes:
    python3 tools/ui_preview/extract_assets.py
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent / "assets.js"

FONT_CANDIDATES = [
    ROOT / "firmware/.pio/libdeps/usb/Adafruit GFX Library/glcdfont.c",
    ROOT / "firmware/.pio/libdeps/ota/Adafruit GFX Library/glcdfont.c",
    ROOT / "firmware/.pio/libdeps/esp32dev/Adafruit GFX Library/glcdfont.c",
]
ICONS_H = ROOT / "firmware/include/icons_tft.h"


def load_font() -> list:
    path = next((p for p in FONT_CANDIDATES if p.exists()), None)
    if path is None:
        sys.exit(
            "glcdfont.c not found -- run a PlatformIO build first so the Adafruit GFX "
            "library is downloaded into firmware/.pio/libdeps/."
        )
    body = path.read_text()
    body = body[body.index("{") + 1 : body.rindex("}")]
    font = [int(v, 16) for v in re.findall(r"0[xX][0-9a-fA-F]{2}", body)]
    if len(font) != 1280:  # 256 glyphs * 5 columns
        sys.exit(f"unexpected glcdfont size: {len(font)} bytes (expected 1280)")
    return font


def load_icons() -> dict:
    if not ICONS_H.exists():
        sys.exit(f"{ICONS_H} not found")
    text = ICONS_H.read_text()
    icons = {}
    for m in re.finditer(r"static const uint16_t (ic12_\w+)\[144\] = \{(.*?)\};", text, re.S):
        name = m.group(1)[len("ic12_") :]
        px = [int(x, 16) for x in re.findall(r"0[xX][0-9a-fA-F]{4}", m.group(2))]
        if len(px) != 144:  # 12 * 12
            sys.exit(f"icon {name}: {len(px)} pixels (expected 144)")
        icons[name] = px
    if not icons:
        sys.exit("no 12x12 icons found in icons_tft.h")
    return icons


def main() -> None:
    font, icons = load_font(), load_icons()
    OUT.write_text(
        "// Auto-extracted from Adafruit GFX glcdfont.c and firmware/include/icons_tft.h.\n"
        "// Regenerate with tools/ui_preview/extract_assets.py after changing either source.\n"
        f"const GFX_FONT = {json.dumps(font)};\n"
        f"const ICONS12 = {json.dumps(icons)};\n"
    )
    print(f"wrote {OUT.relative_to(ROOT)}  ({len(font)} font bytes, {len(icons)} icons)")


if __name__ == "__main__":
    main()
