#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Interpreter: use SD WebUI venv — ~/stable-diffusion-webui/venv/bin/python
"""
Camera Slider UI — Icon Generator
Generates pixel art icons via Stable Diffusion WebUI API,
then post-processes to exact size + limited palette.

Usage:
  1. Start SD WebUI with API:
       cd ~/stable-diffusion-webui && ./webui.sh --api --nowebui
  2. Run this script:
       python3 tools/generate_icons.py
  3. Optional: generate specific series or icons:
       python3 tools/generate_icons.py --series A
       python3 tools/generate_icons.py --icon ic32_speed ic32_home
       python3 tools/generate_icons.py --dry-run   # print prompts only
"""

import argparse
import base64
import io
import json
import sys
import time
from pathlib import Path

import requests
from PIL import Image, ImageDraw

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

SD_API = "http://127.0.0.1:7860"
OUTPUT_DIR = Path(__file__).parent / "icons"

# SD generation params
SD_PARAMS_BASE = {
    "steps": 35,
    "cfg_scale": 8,
    "sampler_name": "DPM++ 2M Karras",
    "width": 512,
    "height": 512,
    "restore_faces": False,
    "tiling": False,
    "n_iter": 1,
    "batch_size": 1,
}

NEGATIVE_PROMPT = (
    "blurry, anti-aliased, smooth gradients, soft edges, glow, shadow, "
    "photorealistic, 3d render, noise, bokeh, lens flare, text, watermark, "
    "signature, border, frame, multiple icons, collage"
)

STYLE_PREFIX = (
    "pixel art icon, game sprite, flat colors, crisp hard edges, "
    "limited color palette, technical icon, transparent background, "
    "isolated icon on black background, clean sharp pixels, "
    "no anti-aliasing, no gradients, "
)

# ---------------------------------------------------------------------------
# Palette — RGB tuples matching docs/07_ui_kit.md
# ---------------------------------------------------------------------------

PAL = {
    "CYAN":     (0x00, 0xD4, 0xFF),
    "GREEN":    (0x00, 0xE8, 0x7A),
    "AMBER":    (0xFF, 0xA0, 0x20),
    "RED":      (0xFF, 0x28, 0x40),
    "PURPLE":   (0x90, 0x60, 0xFF),
    "WHITE":    (0xFF, 0xFF, 0xFF),
    "TEXT_SEC": (0x7A, 0x90, 0xBB),
    "BG_CARD":  (0x1C, 0x25, 0x40),
    "BLACK":    (0x00, 0x00, 0x00),
}

# ---------------------------------------------------------------------------
# Icon definitions
# ---------------------------------------------------------------------------

ICONS = {
    # ── Series A: 32×32, main menu, 2-3 colors ──────────────────────────────
    "ic32_speed": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "CYAN", "WHITE"],
        "prompt": (
            "speedometer gauge icon, semicircular dial arc, evenly spaced tick marks, "
            "sharp triangular needle pointing upper-right, small pivot circle, "
            "minimalist technical style, no labels no numbers"
        ),
        "colors_desc": "grey body and ticks, cyan needle, white center dot",
    },
    "ic32_current": {
        "series": "A", "size": 32,
        "palette": ["AMBER", "WHITE"],
        "prompt": (
            "electric current icon, filled circle, bold lightning bolt zigzag arrow "
            "inside the circle, thick 3-pixel bolt readable at small size"
        ),
        "colors_desc": "amber circle, white lightning bolt",
    },
    "ic32_microstep": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "microstep resolution icon, two square brackets left and right, "
            "4x4 regular grid of small square dots inside, "
            "center dot highlighted different color, represents fine subdivision"
        ),
        "colors_desc": "grey brackets and grid, cyan center dot",
    },
    "ic32_ramp": {
        "series": "A", "size": 32,
        "palette": ["CYAN", "TEXT_SEC"],
        "prompt": (
            "acceleration ramp icon, trapezoid shape front view, "
            "flat top edge, slanted left side rising, vertical right side, "
            "flat bottom baseline, represents motor acceleration profile, clean geometric"
        ),
        "colors_desc": "cyan trapezoid, grey baseline",
    },
    "ic32_home": {
        "series": "A", "size": 32,
        "palette": ["GREEN", "WHITE"],
        "prompt": (
            "home homing icon, simple house silhouette, equilateral triangle roof, "
            "square body below, small centered rectangular door cutout at bottom, "
            "no windows, classic minimal house"
        ),
        "colors_desc": "green roof, white walls and door",
    },
    "ic32_sleep": {
        "series": "A", "size": 32,
        "palette": ["PURPLE", "WHITE"],
        "prompt": (
            "sleep night icon, crescent moon right-facing thick arc pixel art, "
            "three small 2x2 pixel star dots scattered upper right area, "
            "minimal clear silhouette"
        ),
        "colors_desc": "purple crescent moon, white stars",
    },
    "ic32_endstop": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "RED"],
        "prompt": (
            "rail end stop icon, horizontal line track spanning full width, "
            "thick vertical bar bumper at each end, like railroad end buffers, "
            "arrows pointing inward from stops toward center"
        ),
        "colors_desc": "grey rail track, red end bumpers",
    },
    "ic32_adxl": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "3D accelerometer icon, small isometric cube lower-left area, "
            "three axis arrows extending from cube corner: X rightward, Y upward, "
            "Z diagonal toward viewer, each arrow has small arrowhead"
        ),
        "colors_desc": "grey cube faces, cyan axis arrows",
    },
    "ic32_settings": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "WHITE"],
        "prompt": (
            "settings gear cog icon, single gear with 8 evenly spaced rectangular teeth, "
            "circle cutout in center, symmetrical classic settings icon, "
            "teeth distinct and readable"
        ),
        "colors_desc": "grey gear body and teeth, white center circle",
    },
    "ic32_ble": {
        "series": "A", "size": 32,
        "palette": ["CYAN"],
        "prompt": (
            "Bluetooth symbol icon, classic bluetooth logo, vertical line with "
            "two diagonal lines forming stylized B shape, upper-right and lower-right "
            "diagonals crossing vertical, thick 2-3 pixel strokes"
        ),
        "colors_desc": "cyan, all elements same color",
    },
    "ic32_calib": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "calibration ruler icon, horizontal ruler bar with 5 tick marks "
            "alternating major and minor heights, downward pointing triangle marker "
            "caret below ruler at two-thirds position, represents measured travel"
        ),
        "colors_desc": "grey ruler and ticks, amber position marker",
    },
    "ic32_goto": {
        "series": "A", "size": 32,
        "palette": ["CYAN", "WHITE"],
        "prompt": (
            "go to position icon, dashed horizontal path line left to right, "
            "small vertical flag marker post at right end, arrow head pointing right, "
            "represents moving to target position"
        ),
        "colors_desc": "cyan path and arrow, white flag",
    },
    "ic32_display": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "display settings icon, small rounded rectangle monitor screen frame "
            "taking left two-thirds, inside screen a sun symbol: small circle "
            "with 4 short rays at cardinal directions, represents brightness"
        ),
        "colors_desc": "grey monitor frame, amber sun symbol",
    },

    # ── Series B: 16×16, submenu lists, 2 colors ────────────────────────────
    "ic16_speed": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "speedometer icon 16x16 pixel art, semicircular dial arc, "
            "single needle pointer, no tick marks, minimal silhouette"
        ),
        "colors_desc": "grey arc, cyan needle",
    },
    "ic16_current": {
        "series": "B", "size": 16,
        "palette": ["AMBER", "WHITE"],
        "prompt": (
            "lightning bolt in circle icon 16x16 pixel art, "
            "circle outline, bold lightning bolt inside, minimal"
        ),
        "colors_desc": "amber circle, white bolt",
    },
    "ic16_microstep": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "grid of dots with brackets icon 16x16 pixel art, "
            "3x3 dot grid, square brackets sides, center dot highlighted"
        ),
        "colors_desc": "grey dots and brackets, cyan center",
    },
    "ic16_ramp": {
        "series": "B", "size": 16,
        "palette": ["CYAN"],
        "prompt": (
            "trapezoid ramp shape icon 16x16 pixel art, "
            "flat top, slanted left rise, vertical right, flat base"
        ),
        "colors_desc": "cyan solid",
    },
    "ic16_home": {
        "series": "B", "size": 16,
        "palette": ["GREEN", "WHITE"],
        "prompt": (
            "house icon 16x16 pixel art, triangle roof, square body, "
            "small door opening, no windows, minimal"
        ),
        "colors_desc": "green roof, white body",
    },
    "ic16_sleep": {
        "series": "B", "size": 16,
        "palette": ["PURPLE"],
        "prompt": (
            "crescent moon icon 16x16 pixel art, simple moon arc, "
            "no stars, pure silhouette"
        ),
        "colors_desc": "purple solid",
    },
    "ic16_endstop": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "RED"],
        "prompt": (
            "horizontal track with end bumpers icon 16x16 pixel art, "
            "line with thick vertical bars at both ends, minimal"
        ),
        "colors_desc": "grey track, red bumpers",
    },
    "ic16_adxl": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "three axis arrows icon 16x16 pixel art, "
            "X Y Z arrows from common origin point, no cube, minimal"
        ),
        "colors_desc": "grey lines, cyan arrowheads",
    },
    "ic16_settings": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC"],
        "prompt": (
            "gear cog icon 16x16 pixel art, 6 teeth gear, center hole, "
            "classic settings symbol, minimal pixel art"
        ),
        "colors_desc": "grey solid gear",
    },
    "ic16_ble": {
        "series": "B", "size": 16,
        "palette": ["CYAN"],
        "prompt": (
            "bluetooth symbol 16x16 pixel art, classic BT logo, "
            "thick strokes, clear silhouette"
        ),
        "colors_desc": "cyan solid",
    },
    "ic16_calib": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "ruler with position marker 16x16 pixel art, "
            "ruler bar with 3 tick marks, triangle caret below"
        ),
        "colors_desc": "grey ruler, amber marker",
    },
    "ic16_goto": {
        "series": "B", "size": 16,
        "palette": ["CYAN", "WHITE"],
        "prompt": (
            "arrow to flag icon 16x16 pixel art, "
            "right-pointing arrow, small flag at tip"
        ),
        "colors_desc": "cyan arrow, white flag",
    },
    "ic16_display": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "monitor with sun icon 16x16 pixel art, "
            "rectangle frame, small sun symbol inside"
        ),
        "colors_desc": "grey frame, amber sun",
    },
    "ic16_high_contrast": {
        "series": "B", "size": 16,
        "palette": ["WHITE", "BLACK"],
        "prompt": (
            "contrast toggle icon 16x16 pixel art, "
            "circle half filled black half filled white diagonally, "
            "represents high contrast mode"
        ),
        "colors_desc": "black and white halves",
    },

    # ── Series C: 12×12, status bar, 1-2 colors ─────────────────────────────
    "ic12_bt_on": {
        "series": "C", "size": 12,
        "palette": ["CYAN"],
        "prompt": (
            "bluetooth symbol 12x12 pixel art, classic BT logo, "
            "2 pixel strokes, pure silhouette on black"
        ),
        "colors_desc": "cyan solid",
    },
    "ic12_bt_off": {
        "series": "C", "size": 12,
        "palette": ["TEXT_SEC"],
        "prompt": (
            "bluetooth symbol with strikethrough 12x12 pixel art, "
            "BT logo with diagonal line across, disconnected state"
        ),
        "colors_desc": "dark grey solid",
    },
    "ic12_bat_0": {
        "series": "C", "size": 12,
        "palette": ["RED"],
        "prompt": (
            "battery icon empty 12x12 pixel art, "
            "horizontal rectangle outline with small nub on right end, "
            "empty inside, critical state"
        ),
        "colors_desc": "red outline, empty fill",
    },
    "ic12_bat_1": {
        "series": "C", "size": 12,
        "palette": ["AMBER"],
        "prompt": (
            "battery icon 25 percent 12x12 pixel art, "
            "horizontal rectangle outline with nub, one segment filled left side"
        ),
        "colors_desc": "amber, one segment",
    },
    "ic12_bat_2": {
        "series": "C", "size": 12,
        "palette": ["AMBER"],
        "prompt": (
            "battery icon 50 percent 12x12 pixel art, "
            "horizontal rectangle with nub, two segments filled"
        ),
        "colors_desc": "amber, two segments",
    },
    "ic12_bat_3": {
        "series": "C", "size": 12,
        "palette": ["GREEN"],
        "prompt": (
            "battery icon 75 percent 12x12 pixel art, "
            "horizontal rectangle with nub, three segments filled"
        ),
        "colors_desc": "green, three segments",
    },
    "ic12_bat_4": {
        "series": "C", "size": 12,
        "palette": ["GREEN"],
        "prompt": (
            "battery icon full 12x12 pixel art, "
            "horizontal rectangle with nub, fully filled"
        ),
        "colors_desc": "green, fully filled",
    },
}

# ---------------------------------------------------------------------------
# Post-processing
# ---------------------------------------------------------------------------

def build_palette_image(color_names: list[str]) -> list[tuple]:
    """Build list of RGB tuples from palette name list."""
    colors = [PAL["BLACK"]]  # always include black as background/transparent key
    for name in color_names:
        c = PAL[name]
        if c not in colors:
            colors.append(c)
    # Pad to 256 with black
    while len(colors) < 256:
        colors.append((0, 0, 0))
    return colors


def quantize_to_palette(img: Image.Image, color_names: list[str]) -> Image.Image:
    """Downscale to target size and quantize to exact palette colors."""
    target_size = img.size  # already at target size when called

    # Convert to RGB
    if img.mode != "RGB":
        img = img.convert("RGB")

    palette_colors = [PAL[n] for n in color_names]
    # Add black as transparent background color
    all_colors = [PAL["BLACK"]] + [c for c in palette_colors if c != PAL["BLACK"]]

    # Build PIL palette image for quantization
    palette_img = Image.new("P", (1, 1))
    flat = []
    for c in all_colors:
        flat.extend(c)
    while len(flat) < 768:
        flat.extend((0, 0, 0))
    palette_img.putpalette(flat)

    # Quantize
    quantized = img.quantize(colors=len(all_colors), palette=palette_img, dither=0)
    result = quantized.convert("RGBA")

    # Make black pixels transparent
    pixels = result.load()
    black_threshold = 30
    for y in range(result.height):
        for x in range(result.width):
            r, g, b, a = pixels[x, y]
            if r < black_threshold and g < black_threshold and b < black_threshold:
                pixels[x, y] = (0, 0, 0, 0)

    return result


def postprocess(raw_img: Image.Image, icon_def: dict) -> Image.Image:
    """
    Take 512x512 generated image, downscale to icon size,
    quantize to palette, ensure no anti-aliasing.
    """
    size = icon_def["size"]
    palette = icon_def["palette"]

    # 1. Crop to square (already 512x512, but just in case)
    w, h = raw_img.size
    if w != h:
        s = min(w, h)
        raw_img = raw_img.crop(((w - s) // 2, (h - s) // 2, (w + s) // 2, (h + s) // 2))

    # 2. Downscale in two steps: first to 2x, then to target
    #    This gives sharper pixel art than direct downscale
    intermediate = size * 4
    img = raw_img.resize((intermediate, intermediate), Image.LANCZOS)
    img = img.resize((size, size), Image.LANCZOS)

    # 3. Quantize to exact palette
    img = quantize_to_palette(img, palette)

    return img


# ---------------------------------------------------------------------------
# SD API
# ---------------------------------------------------------------------------

def check_api() -> bool:
    try:
        r = requests.get(f"{SD_API}/sdapi/v1/sd-models", timeout=5)
        return r.status_code == 200
    except Exception:
        return False


def generate_image(icon_name: str, icon_def: dict) -> Image.Image | None:
    prompt = STYLE_PREFIX + icon_def["prompt"]
    payload = {
        **SD_PARAMS_BASE,
        "prompt": prompt,
        "negative_prompt": NEGATIVE_PROMPT,
        "seed": abs(hash(icon_name)) % (2**31),  # deterministic per icon
    }

    try:
        r = requests.post(f"{SD_API}/sdapi/v1/txt2img", json=payload, timeout=120)
        r.raise_for_status()
        data = r.json()
        img_data = base64.b64decode(data["images"][0])
        return Image.open(io.BytesIO(img_data))
    except requests.exceptions.Timeout:
        print(f"  [TIMEOUT] {icon_name}")
        return None
    except Exception as e:
        print(f"  [ERROR] {icon_name}: {e}")
        return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def series_dir(series: str) -> Path:
    d = OUTPUT_DIR / f"series_{series}"
    d.mkdir(parents=True, exist_ok=True)
    return d


def run(icon_names: list[str], dry_run: bool = False):
    if not dry_run and not check_api():
        print("ERROR: SD WebUI API not reachable at", SD_API)
        print()
        print("Start it with:")
        print("  cd ~/stable-diffusion-webui")
        print("  ./webui.sh --api --nowebui")
        print()
        print("Then re-run this script.")
        sys.exit(1)

    total = len(icon_names)
    for i, name in enumerate(icon_names, 1):
        if name not in ICONS:
            print(f"[{i}/{total}] SKIP unknown: {name}")
            continue

        icon = ICONS[name]
        out_path = series_dir(icon["series"]) / f"{name}.png"

        palette_str = ", ".join(
            f"{n}={PAL[n]}" for n in icon["palette"]
        )
        print(f"[{i}/{total}] {name}  ({icon['size']}×{icon['size']})  palette: {palette_str}")

        if dry_run:
            print(f"  PROMPT: {STYLE_PREFIX + icon['prompt']}")
            print()
            continue

        if out_path.exists():
            print(f"  → already exists, skipping (delete to regenerate)")
            continue

        print(f"  generating...", end="", flush=True)
        raw = generate_image(name, icon)
        if raw is None:
            continue

        print(f" postprocessing...", end="", flush=True)
        result = postprocess(raw, icon)
        result.save(out_path, "PNG")
        print(f" saved → {out_path.relative_to(Path(__file__).parent.parent)}")

        # Small pause to not overwhelm the API
        time.sleep(0.5)

    if not dry_run:
        print()
        print(f"Done. Icons saved to: {OUTPUT_DIR}")


def main():
    parser = argparse.ArgumentParser(description="Generate slider UI icons via SD WebUI")
    parser.add_argument(
        "--series", choices=["A", "B", "C", "D"],
        help="Generate only this series"
    )
    parser.add_argument(
        "--icon", nargs="+", metavar="ICON_NAME",
        help="Generate specific icons by name (e.g. ic32_speed ic16_home)"
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Print prompts without generating"
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List all defined icons"
    )
    args = parser.parse_args()

    if args.list:
        for name, icon in ICONS.items():
            print(f"  {name:25s}  series={icon['series']}  size={icon['size']:2d}  "
                  f"colors={', '.join(icon['palette'])}")
        return

    if args.icon:
        names = args.icon
    elif args.series:
        names = [n for n, d in ICONS.items() if d["series"] == args.series]
    else:
        names = list(ICONS.keys())

    print(f"Camera Slider Icon Generator")
    print(f"Icons to process: {len(names)}")
    print(f"Output: {OUTPUT_DIR}")
    print()

    run(names, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
