#!/usr/bin/env python3
"""
Camera Slider — Icon Generator via ComfyUI API
Generates all icons, skips already existing files.

Usage:
  python3 tools/generate_icons_comfy.py
  python3 tools/generate_icons_comfy.py --series A
  python3 tools/generate_icons_comfy.py --icon ic32_speed ic16_home
  python3 tools/generate_icons_comfy.py --dry-run
"""

import argparse
import io
import json
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

COMFY_API = "http://localhost:8188"
OUTPUT_DIR = Path(__file__).parent / "icons_comfy"

CHECKPOINT = "flux1-dev.safetensors"
CLIP1 = "t5xxl_fp16.safetensors"
CLIP2 = "clip_l.safetensors"
VAE = "ae.safetensors"

NEGATIVE = "blurry, anti-aliased, photorealistic, 3d render, noise, glow, text, watermark, multiple icons, collage"

STYLE_PREFIX = (
    "pixel art icon, game sprite, flat colors, crisp hard edges, "
    "limited color palette, isolated icon on solid black background, "
    "clean sharp pixels, no anti-aliasing, no gradients, no glow, "
)

# ---------------------------------------------------------------------------
# Palette
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
# Icon catalog
# ---------------------------------------------------------------------------

ICONS = {
    # ── Series A: 32×32 ─────────────────────────────────────────────────────
    "ic32_speed": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "CYAN", "WHITE"],
        "prompt": (
            "speedometer gauge icon 32x32 pixels, semicircular dial arc top half, "
            "evenly spaced 7 tick marks along the arc, "
            "sharp triangular needle pointing upper-right at high speed, "
            "small filled circle at needle pivot center, "
            "minimalist technical style, no labels no numbers, "
            "grey body and tick marks, cyan needle, white center dot, "
            "centered filling 26x26 pixels"
        ),
    },
    "ic32_current": {
        "series": "A", "size": 32,
        "palette": ["AMBER", "WHITE"],
        "prompt": (
            "electric current icon 32x32 pixels, filled solid circle, "
            "bold lightning bolt zigzag arrow inside the circle pointing downward, "
            "lightning bolt is white on amber circle, thick 3-pixel bolt stroke, "
            "centered filling 24x24 pixels"
        ),
    },
    "ic32_microstep": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "microstep resolution icon 32x32 pixels, "
            "two square brackets on left and right sides, "
            "inside: 4x4 regular grid of small square dots, "
            "center dot is cyan accent color, all other dots and brackets are grey, "
            "represents fine resolution subdivision, centered filling 24x24 pixels"
        ),
    },
    "ic32_ramp": {
        "series": "A", "size": 32,
        "palette": ["CYAN", "TEXT_SEC"],
        "prompt": (
            "acceleration ramp icon 32x32 pixels, "
            "trapezoid shape: flat top edge, slanted left side rising from baseline, "
            "vertical right side, flat bottom baseline, "
            "cyan trapezoid filled solid, grey baseline axis line, "
            "represents motor acceleration profile, clean geometric shape, "
            "centered filling 26x20 pixels vertically centered"
        ),
    },
    "ic32_home": {
        "series": "A", "size": 32,
        "palette": ["GREEN", "WHITE"],
        "prompt": (
            "home homing icon 32x32 pixels, simple house silhouette, "
            "equilateral triangle roof on top, square body below, "
            "small centered rectangular door cutout at bottom of body, "
            "no windows, classic minimal house, "
            "green roof, white walls and door area, centered filling 24x26 pixels"
        ),
    },
    "ic32_sleep": {
        "series": "A", "size": 32,
        "palette": ["PURPLE", "WHITE"],
        "prompt": (
            "sleep night icon 32x32 pixels, "
            "crescent moon right-facing thick arc in pixel art style, "
            "three small 2x2 pixel star dots scattered in upper-right area, "
            "purple crescent moon, white star dots, "
            "minimal clear silhouette, centered filling 24x24 pixels"
        ),
    },
    "ic32_endstop": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "RED"],
        "prompt": (
            "rail end stop icon 32x32 pixels, "
            "horizontal line track spanning full width of icon, "
            "thick vertical bar bumper at each end like railroad end buffers, "
            "grey rail track, red end bumpers, "
            "centered vertically filling 28x12 pixels"
        ),
    },
    "ic32_adxl": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "3D accelerometer axes icon 32x32 pixels, "
            "small isometric cube in lower-left area, "
            "three axis arrows from cube corner: X rightward, Y upward, Z diagonal toward viewer, "
            "each arrow has small arrowhead, grey cube faces, cyan axis arrows, "
            "centered filling 26x26 pixels"
        ),
    },
    "ic32_settings": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "WHITE"],
        "prompt": (
            "settings gear cog icon 32x32 pixels, "
            "single gear with 8 evenly spaced rectangular teeth around perimeter, "
            "circle cutout hole in center, symmetrical classic settings icon, "
            "grey gear body and teeth, white center circle cutout, "
            "centered filling 26x26 pixels"
        ),
    },
    "ic32_ble": {
        "series": "A", "size": 32,
        "palette": ["CYAN"],
        "prompt": (
            "bluetooth symbol icon 32x32 pixels, classic bluetooth logo, "
            "vertical line with two diagonal lines forming stylized B shape, "
            "upper-right diagonal and lower-right diagonal crossing the vertical line, "
            "like rune shape with pointed top and bottom, thick 3 pixel strokes, "
            "all cyan color, centered filling 14x24 pixels"
        ),
    },
    "ic32_calib": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "calibration ruler icon 32x32 pixels, "
            "horizontal ruler bar with 5 tick marks alternating major and minor heights, "
            "downward pointing triangle marker caret below ruler at two-thirds position, "
            "grey ruler body and ticks, amber position marker triangle, "
            "centered filling 28x16 pixels vertically centered"
        ),
    },
    "ic32_goto": {
        "series": "A", "size": 32,
        "palette": ["CYAN", "WHITE"],
        "prompt": (
            "go to position icon 32x32 pixels, "
            "dashed horizontal path line going left to right, "
            "small vertical flag post marker at right end, "
            "arrow head pointing right at end of path, "
            "cyan dashed path and arrowhead, white flag marker, "
            "centered filling 28x14 pixels vertically centered"
        ),
    },
    "ic32_display": {
        "series": "A", "size": 32,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "display brightness settings icon 32x32 pixels, "
            "small rounded rectangle monitor screen frame left two-thirds, "
            "inside screen: sun symbol with small circle and 4 short rays at cardinal directions, "
            "grey monitor frame, amber sun symbol inside, "
            "centered filling 26x22 pixels"
        ),
    },

    # ── Series B: 16×16 ─────────────────────────────────────────────────────
    "ic16_speed": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "speedometer icon 16x16 pixels pixel art, "
            "semicircular dial arc top half, single needle pointer, no tick marks, "
            "grey arc, cyan needle, minimal silhouette, fills 12x12 centered"
        ),
    },
    "ic16_current": {
        "series": "B", "size": 16,
        "palette": ["AMBER", "WHITE"],
        "prompt": (
            "lightning bolt in circle icon 16x16 pixels pixel art, "
            "circle outline, bold lightning bolt inside, "
            "amber circle, white bolt, minimal 2-color, fills 12x12 centered"
        ),
    },
    "ic16_microstep": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "dot grid with brackets icon 16x16 pixels pixel art, "
            "3x3 dot grid inside square brackets, center dot cyan accent, "
            "grey dots and brackets, fills 12x12 centered"
        ),
    },
    "ic16_ramp": {
        "series": "B", "size": 16,
        "palette": ["CYAN"],
        "prompt": (
            "trapezoid ramp shape icon 16x16 pixels pixel art, "
            "flat top, slanted left rising side, vertical right side, flat base, "
            "solid cyan filled shape, minimal, fills 12x10 centered"
        ),
    },
    "ic16_home": {
        "series": "B", "size": 16,
        "palette": ["GREEN", "WHITE"],
        "prompt": (
            "house icon 16x16 pixels pixel art, "
            "triangle roof on top, square body below, small door opening at bottom, "
            "no windows, green roof, white body, minimal silhouette, fills 12x13 centered"
        ),
    },
    "ic16_sleep": {
        "series": "B", "size": 16,
        "palette": ["PURPLE"],
        "prompt": (
            "crescent moon icon 16x16 pixels pixel art, "
            "simple moon arc shape, no stars, pure solid silhouette, "
            "purple color, fills 10x12 centered"
        ),
    },
    "ic16_endstop": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "RED"],
        "prompt": (
            "horizontal track with end bumpers icon 16x16 pixels pixel art, "
            "horizontal line with thick vertical bars at both ends, "
            "grey track line, red bumper bars, minimal, fills 14x8 vertically centered"
        ),
    },
    "ic16_adxl": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "CYAN"],
        "prompt": (
            "three axis arrows icon 16x16 pixels pixel art, "
            "X Y Z arrows from common center origin point, no cube, "
            "grey lines, cyan arrowheads, minimal, fills 12x12 centered"
        ),
    },
    "ic16_settings": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC"],
        "prompt": (
            "gear cog icon 16x16 pixels pixel art, "
            "6-tooth gear with center hole, classic settings symbol, "
            "solid grey, minimal pixel art, fills 12x12 centered"
        ),
    },
    "ic16_ble": {
        "series": "B", "size": 16,
        "palette": ["CYAN"],
        "prompt": (
            "bluetooth symbol 16x16 pixels pixel art, "
            "classic BT logo vertical line with diagonals, thick strokes, "
            "clear silhouette, solid cyan, fills 8x14 centered"
        ),
    },
    "ic16_calib": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "ruler with position marker 16x16 pixels pixel art, "
            "ruler bar with 3 tick marks, downward triangle caret marker below, "
            "grey ruler, amber marker, fills 14x10 centered"
        ),
    },
    "ic16_goto": {
        "series": "B", "size": 16,
        "palette": ["CYAN", "WHITE"],
        "prompt": (
            "arrow to flag icon 16x16 pixels pixel art, "
            "right-pointing arrow with small vertical flag at tip, "
            "cyan arrow, white flag, fills 14x10 centered"
        ),
    },
    "ic16_display": {
        "series": "B", "size": 16,
        "palette": ["TEXT_SEC", "AMBER"],
        "prompt": (
            "monitor with brightness sun icon 16x16 pixels pixel art, "
            "rectangle screen frame, small sun symbol inside, "
            "grey frame, amber sun, fills 14x11 centered"
        ),
    },
    "ic16_high_contrast": {
        "series": "B", "size": 16,
        "palette": ["WHITE", "BLACK"],
        "prompt": (
            "high contrast toggle icon 16x16 pixels pixel art, "
            "circle split diagonally: left half filled black, right half filled white, "
            "represents contrast mode toggle, fills 12x12 centered"
        ),
    },

    # ── Series C: 12×12 ─────────────────────────────────────────────────────
    "ic12_bt_on": {
        "series": "C", "size": 12,
        "palette": ["CYAN"],
        "prompt": (
            "bluetooth symbol 12x12 pixels pixel art, "
            "classic BT logo, 2-pixel strokes, pure silhouette, "
            "solid cyan, fills 7x11 centered"
        ),
    },
    "ic12_bt_off": {
        "series": "C", "size": 12,
        "palette": ["TEXT_SEC"],
        "prompt": (
            "bluetooth symbol with strikethrough 12x12 pixels pixel art, "
            "BT logo with diagonal line crossing through it, disconnected state, "
            "solid dark grey, fills 10x11 centered"
        ),
    },
    "ic12_bat_0": {
        "series": "C", "size": 12,
        "palette": ["RED"],
        "prompt": (
            "battery icon empty 12x12 pixels pixel art, "
            "horizontal rectangle outline body with small nub bump on right end, "
            "empty inside, critical battery state, "
            "red outline only no fill, fills 10x6 centered"
        ),
    },
    "ic12_bat_1": {
        "series": "C", "size": 12,
        "palette": ["AMBER"],
        "prompt": (
            "battery 25 percent icon 12x12 pixels pixel art, "
            "horizontal rectangle outline with small nub on right, "
            "one segment filled on left side, "
            "amber color, fills 10x6 centered"
        ),
    },
    "ic12_bat_2": {
        "series": "C", "size": 12,
        "palette": ["AMBER"],
        "prompt": (
            "battery 50 percent icon 12x12 pixels pixel art, "
            "horizontal rectangle outline with small nub on right, "
            "two segments filled, half full, "
            "amber color, fills 10x6 centered"
        ),
    },
    "ic12_bat_3": {
        "series": "C", "size": 12,
        "palette": ["GREEN"],
        "prompt": (
            "battery 75 percent icon 12x12 pixels pixel art, "
            "horizontal rectangle outline with small nub on right, "
            "three segments filled, mostly full, "
            "green color, fills 10x6 centered"
        ),
    },
    "ic12_bat_4": {
        "series": "C", "size": 12,
        "palette": ["GREEN"],
        "prompt": (
            "battery full 100 percent icon 12x12 pixels pixel art, "
            "horizontal rectangle outline with small nub on right, "
            "completely filled solid, fully charged, "
            "green color, fills 10x6 centered"
        ),
    },
}

# ---------------------------------------------------------------------------
# ComfyUI helpers
# ---------------------------------------------------------------------------

def build_workflow(name: str, icon: dict) -> dict:
    prompt = STYLE_PREFIX + icon["prompt"]
    return {
        "1":  {"class_type": "CheckpointLoaderSimple", "inputs": {"ckpt_name": CHECKPOINT}},
        "2":  {"class_type": "DualCLIPLoader",          "inputs": {"clip_name1": CLIP1, "clip_name2": CLIP2, "type": "flux"}},
        "3":  {"class_type": "VAELoader",               "inputs": {"vae_name": VAE}},
        "4":  {"class_type": "CLIPTextEncode",          "inputs": {"text": prompt, "clip": ["2", 0]}},
        "5":  {"class_type": "CLIPTextEncode",          "inputs": {"text": NEGATIVE, "clip": ["2", 0]}},
        "6":  {"class_type": "EmptyLatentImage",        "inputs": {"width": 512, "height": 512, "batch_size": 1}},
        "7":  {"class_type": "FluxGuidance",            "inputs": {"conditioning": ["4", 0], "guidance": 3.5}},
        "8":  {"class_type": "KSampler",                "inputs": {
            "model": ["1", 0], "positive": ["7", 0], "negative": ["5", 0],
            "latent_image": ["6", 0],
            "seed": abs(hash(name)) % (2**31),
            "steps": 20, "cfg": 1.0,
            "sampler_name": "euler", "scheduler": "simple", "denoise": 1.0,
        }},
        "9":  {"class_type": "VAEDecode",               "inputs": {"samples": ["8", 0], "vae": ["3", 0]}},
        "10": {"class_type": "SaveImage",               "inputs": {"images": ["9", 0], "filename_prefix": name}},
    }


def api_post(path: str, data: dict) -> dict:
    body = json.dumps(data).encode()
    req = urllib.request.Request(f"{COMFY_API}{path}", data=body, headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req).read())


def api_get(path: str) -> dict:
    return json.loads(urllib.request.urlopen(f"{COMFY_API}{path}").read())


def submit(name: str, icon: dict) -> str:
    result = api_post("/prompt", {"prompt": build_workflow(name, icon)})
    return result["prompt_id"]


def fetch_image(img_info: dict) -> bytes:
    params = urllib.parse.urlencode({
        "filename": img_info["filename"],
        "subfolder": img_info.get("subfolder", ""),
        "type": img_info.get("type", "output"),
    })
    return urllib.request.urlopen(f"{COMFY_API}/view?{params}").read()


# ---------------------------------------------------------------------------
# Post-processing
# ---------------------------------------------------------------------------

def postprocess(raw_bytes: bytes, icon: dict) -> "Image":
    from PIL import Image as PILImage

    size = icon["size"]
    palette_names = icon["palette"]

    raw = PILImage.open(io.BytesIO(raw_bytes)).convert("RGB")

    # Two-step downscale for sharper pixels
    intermediate = size * 4
    img = raw.resize((intermediate, intermediate), PILImage.LANCZOS)
    img = img.resize((size, size), PILImage.LANCZOS)

    # Build quantization palette: BLACK + icon colors
    palette_colors = [PAL["BLACK"]] + [PAL[n] for n in palette_names if PAL[n] != PAL["BLACK"]]
    flat = []
    for c in palette_colors:
        flat.extend(c)
    while len(flat) < 768:
        flat.extend((0, 0, 0))

    pal_img = PILImage.new("P", (1, 1))
    pal_img.putpalette(flat)

    quantized = img.quantize(colors=len(palette_colors), palette=pal_img, dither=0)
    result = quantized.convert("RGBA")

    # Black → transparent
    pixels = result.load()
    for y in range(result.height):
        for x in range(result.width):
            r, g, b, a = pixels[x, y]
            if r < 40 and g < 40 and b < 40:
                pixels[x, y] = (0, 0, 0, 0)

    return result


def ascii_preview(img) -> str:
    lines = []
    for y in range(img.height):
        row = ""
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))
            if a == 0:
                row += "."
            else:
                row += "#"
        lines.append(row)
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run(names: list, dry_run: bool = False):
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Skip already done
    to_generate = []
    for name in names:
        out = OUTPUT_DIR / f"{name}.png"
        if out.exists():
            print(f"  SKIP {name} (already exists)")
        else:
            to_generate.append(name)

    if not to_generate:
        print("Nothing to generate.")
        return

    print(f"\nGenerating {len(to_generate)} icons...\n")

    if dry_run:
        for name in to_generate:
            icon = ICONS[name]
            print(f"[{name}] series={icon['series']} size={icon['size']} palette={icon['palette']}")
            print(f"  PROMPT: {STYLE_PREFIX + icon['prompt']}\n")
        return

    # Submit all jobs
    jobs = {}  # name → prompt_id
    for name in to_generate:
        pid = submit(name, ICONS[name])
        jobs[name] = pid
        print(f"  queued {name} → {pid}")

    print(f"\nAll {len(jobs)} jobs submitted. Waiting for results...\n")

    done = set()
    start = time.time()

    while len(done) < len(jobs):
        time.sleep(8)
        elapsed = int(time.time() - start)

        for name, pid in jobs.items():
            if name in done:
                continue

            hist = api_get(f"/history/{pid}")
            if pid not in hist:
                continue

            entry = hist[pid]
            status = entry.get("status", {})
            status_str = status.get("status_str", "")

            if status_str == "error":
                for m in status.get("messages", []):
                    if m[0] == "execution_error":
                        print(f"  ERROR {name}: {m[1].get('exception_message','?')[:120]}")
                done.add(name)
                continue

            outputs = entry.get("outputs", {})
            if not outputs:
                continue

            for node_id, out in outputs.items():
                if "images" not in out:
                    continue
                raw_bytes = fetch_image(out["images"][0])
                result = postprocess(raw_bytes, ICONS[name])

                out_path = OUTPUT_DIR / f"{name}.png"
                preview_path = OUTPUT_DIR / f"{name}_preview.png"
                result.save(out_path, "PNG")
                from PIL import Image as PILImage
                result.resize((result.width * 8, result.height * 8), PILImage.NEAREST).save(preview_path, "PNG")

                q = api_get("/queue")
                remaining = len(jobs) - len(done) - 1
                print(f"  [{elapsed}s] DONE {name:25s} → {out_path.name}  (queue: {len(q['queue_running'])} running, {len(q['queue_pending'])} pending, {remaining} left)")
                done.add(name)
                break

    total = int(time.time() - start)
    print(f"\nAll done in {total//60}m {total%60}s.")
    print(f"Icons saved to: {OUTPUT_DIR}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--series", choices=["A", "B", "C"])
    parser.add_argument("--icon", nargs="+", metavar="NAME")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    if args.list:
        for name, icon in ICONS.items():
            print(f"  {name:25s} series={icon['series']} size={icon['size']:2d} palette={','.join(icon['palette'])}")
        return

    if args.icon:
        names = args.icon
    elif args.series:
        names = [n for n, d in ICONS.items() if d["series"] == args.series]
    else:
        names = list(ICONS.keys())

    run(names, dry_run=args.dry_run)


if __name__ == "__main__":
    main()
