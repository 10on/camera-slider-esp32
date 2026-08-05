#!/usr/bin/env python3
"""Generate KiCad 7 schematic for Camera Slider ESP32-C3 controller.
Run: python3 gen_schematic.py  →  slider.kicad_sch
"""
import uuid, os

def uid(): return str(uuid.uuid4())

P   = 2.54   # pin pitch mm
PL  = 2.54   # pin stub length mm
BW  = 33.02  # box width (13 × P)
FSZ = 1.016  # pin label font size

# ── Symbol definition ──────────────────────────────────────────────────────
def make_symbol(name, left_pins, right_pins):
    rows = max(len(left_pins), len(right_pins), 1)
    bh = (rows + 1) * P
    x0, y0 = -BW/2, -bh/2   # y0 = bottom in symbol coords (Y-up)
    x1, y1 =  BW/2,  bh/2
    s  = f'  (symbol "{name}" (in_bom yes) (on_board yes)\n'
    # Everything in _1_1 (single unit, no demorgan split)
    s += f'    (symbol "{name}_1_1"\n'
    s += f'      (rectangle (start {x0:.4f} {y0:.4f}) (end {x1:.4f} {y1:.4f})\n'
    s += f'        (stroke (width 0.15) (type default)) (fill (type background)))\n'
    for i,(pn,num) in enumerate(left_pins):
        # In symbol coords Y is UP: pin 0 at top = +(rows-1)/2*P, going down
        py = (rows - 1) / 2.0 * P - i * P
        s += f'      (pin passive line (at {x0-PL:.4f} {py:.4f} 0) (length {PL:.4f})\n'
        s += f'        (name "{pn}" (effects (font (size {FSZ} {FSZ}))))\n'
        s += f'        (number "{num}" (effects (font (size {FSZ} {FSZ})))))\n'
    for i,(pn,num) in enumerate(right_pins):
        py = (rows - 1) / 2.0 * P - i * P
        s += f'      (pin passive line (at {x1+PL:.4f} {py:.4f} 180) (length {PL:.4f})\n'
        s += f'        (name "{pn}" (effects (font (size {FSZ} {FSZ}))))\n'
        s += f'        (number "{num}" (effects (font (size {FSZ} {FSZ})))))\n'
    s += f'    )\n  )\n'
    return s

# ── Symbol instance ────────────────────────────────────────────────────────
def place(sym, ref, value, sx, sy):
    return (
        f'  (symbol (lib_id "SLIDER:{sym}") (at {sx:.4f} {sy:.4f} 0) (unit 1)\n'
        f'    (in_bom yes) (on_board yes) (fields_autoplaced yes) (uuid "{uid()}")\n'
        f'    (property "Reference" "{ref}" (at {sx:.4f} {sy-2.54:.4f} 0)\n'
        f'      (effects (font (size 1.27 1.27))))\n'
        f'    (property "Value" "{value}" (at {sx:.4f} {sy+2.54:.4f} 0)\n'
        f'      (effects (font (size 1.27 1.27))))\n'
        f'    (instances (project "slider"\n'
        f'      (path "/{uid()}" (reference "{ref}") (unit 1)))))\n'
    )

# ── Net label ──────────────────────────────────────────────────────────────
def netlabel(net, x, y, angle=0):
    just = "right" if angle == 180 else "left"
    return (
        f'  (label "{net}" (at {x:.4f} {y:.4f} {angle}) (fields_autoplaced yes)\n'
        f'    (effects (font (size 1.27 1.27)) (justify {just}))\n'
        f'    (uuid "{uid()}"))\n'
    )

# ── Pin position on sheet ─────────────────────────────────────────────────
def pinpos(sx, sy, side, idx, nl, nr):
    rows = max(nl, nr, 1)
    # Symbol Y-up: pin idx at py_sym = (rows-1)/2*P - idx*P
    # Sheet Y-down: sheet_y = sy - py_sym
    py_sym = (rows - 1) / 2.0 * P - idx * P
    sheet_y = sy - py_sym
    if side == 'L': return sx - BW/2 - PL, sheet_y
    else:           return sx + BW/2 + PL, sheet_y

# ═══════════════════════════════════════════════════════════════════════════
# MODULES
# ═══════════════════════════════════════════════════════════════════════════
MODS = {
  "BATT":  { "sym":"J_BATT",     "ref":"J1",   "val":"3S-LiPo",
             "L":[("VBAT+","1"),("GND","2")],              "R":[] },
  "DCDC":  { "sym":"U_DCDC",     "ref":"U1",   "val":"MP1584-3V3",
             "L":[("IN+","1"),("GND_IN","2")],             "R":[("OUT_3V3","3"),("GND_OUT","4")] },
  "R1":    { "sym":"R_DIV",      "ref":"R1",   "val":"40.2k",
             "L":[("A","1")],                              "R":[("B","2")] },
  "R2":    { "sym":"R_DIV",      "ref":"R2",   "val":"10k",
             "L":[("A","1")],                              "R":[("B","2")] },
  "ESP":   { "sym":"U_ESP32C3",  "ref":"U2",   "val":"ESP32-C3-Zero",
             "L":[("GPIO0/EN","1"),("GPIO1/STEP","2"),("GPIO2/DIR","3"),
                  ("GPIO3/VBAT_ADC","4"),("GPIO4/TX","5"),("GPIO5/RX","6")],
             "R":[("3V3","7"),("GND","8"),("GPIO8/SDA","9"),("GPIO9/SCL","10")] },
  "TMC":   { "sym":"U_TMC2209",  "ref":"U3",   "val":"TMC2209-module",
             "L":[("VM","1"),("GND","2"),("VIO","3"),("EN","4"),
                  ("DIR","5"),("STEP","6"),("UART_RX","7"),("UART_TX","8")],
             "R":[("A1","9"),("A2","10"),("B1","11"),("B2","12")] },
  "PCF":   { "sym":"U_PCF8574",  "ref":"U4",   "val":"PCF8574",
             "L":[("VCC","1"),("GND","2"),("SDA","3"),("SCL","4")],
             "R":[("P0-nc","5"),("P1/LED1","6"),("P2/ENC_CLK","7"),
                  ("P3/ENC_DT","8"),("P4/ENC_SW","9"),
                  ("P5/ES1","10"),("P6/ES2","11"),("P7/LED2","12")] },
  "OLED":  { "sym":"U_SSD1306",  "ref":"U5",   "val":"SSD1306-128x64",
             "L":[("VCC","1"),("GND","2"),("SCL","3"),("SDA","4")], "R":[] },
  "ADXL":  { "sym":"U_ADXL345",  "ref":"U6",   "val":"ADXL345",
             "L":[("VCC","1"),("GND","2"),("SDA","3"),("SCL","4"),("SDO/GND","5")], "R":[] },
  "ENC":   { "sym":"J_ENCODER",  "ref":"ENC1", "val":"EC11",
             "L":[("CLK","1"),("DT","2"),("SW","3"),("GND","4")], "R":[] },
  "ES1":   { "sym":"J_ENDSTOP",  "ref":"J2",   "val":"Endstop-1",
             "L":[("3V3","1"),("GND","2"),("SIG","3")], "R":[] },
  "ES2":   { "sym":"J_ENDSTOP",  "ref":"J3",   "val":"Endstop-2",
             "L":[("3V3","1"),("GND","2"),("SIG","3")], "R":[] },
  "MOTOR": { "sym":"J_MOTOR",    "ref":"J4",   "val":"Stepper-Motor",
             "L":[("A1","1"),("A2","2"),("B1","3"),("B2","4")], "R":[] },
}

# Placement (x, y) in mm
POS = {
  "BATT":  (25,  35),
  "DCDC":  (85,  35),
  "R1":    (85,  75),
  "R2":    (85,  95),
  "ESP":   (150, 80),
  "TMC":   (245, 65),
  "PCF":   (150, 175),
  "OLED":  (75,  195),
  "ADXL":  (245, 175),
  "ENC":   (50,  210),
  "ES1":   (340,  55),
  "ES2":   (340, 100),
  "MOTOR": (340, 165),
}

# Net labels: (net_name, mod_key, side, pin_index)
LABELS = [
  # Battery
  ("VBAT+", "BATT","L",0), ("GND",   "BATT","L",1),
  # DC-DC
  ("VBAT+", "DCDC","L",0), ("GND",   "DCDC","L",1),
  ("+3V3",  "DCDC","R",0), ("GND",   "DCDC","R",1),
  # Voltage divider
  ("VBAT+",    "R1","L",0), ("VBAT_ADC","R1","R",0),
  ("VBAT_ADC", "R2","L",0), ("GND",     "R2","R",0),
  # ESP32-C3-Zero
  ("TMC_EN",   "ESP","L",0), ("TMC_STEP","ESP","L",1),
  ("TMC_DIR",  "ESP","L",2), ("VBAT_ADC","ESP","L",3),
  ("TMC_TX",   "ESP","L",4), ("TMC_RX",  "ESP","L",5),
  ("+3V3","ESP","R",0), ("GND","ESP","R",1),
  ("SDA", "ESP","R",2), ("SCL","ESP","R",3),
  # TMC2209
  ("VBAT+",   "TMC","L",0), ("GND",    "TMC","L",1),
  ("+3V3",    "TMC","L",2), ("TMC_EN", "TMC","L",3),
  ("TMC_DIR", "TMC","L",4), ("TMC_STEP","TMC","L",5),
  ("TMC_TX",  "TMC","L",6), ("TMC_RX", "TMC","L",7),
  ("MOT_A1","TMC","R",0), ("MOT_A2","TMC","R",1),
  ("MOT_B1","TMC","R",2), ("MOT_B2","TMC","R",3),
  # PCF8574
  ("+3V3","PCF","L",0), ("GND","PCF","L",1),
  ("SDA", "PCF","L",2), ("SCL","PCF","L",3),
  ("NC",          "PCF","R",0), ("PCF_LED1",    "PCF","R",1),
  ("PCF_ENC_CLK", "PCF","R",2), ("PCF_ENC_DT",  "PCF","R",3),
  ("PCF_ENC_SW",  "PCF","R",4), ("PCF_ES1",     "PCF","R",5),
  ("PCF_ES2",     "PCF","R",6), ("PCF_LED2",    "PCF","R",7),
  # OLED
  ("+3V3","OLED","L",0), ("GND","OLED","L",1),
  ("SCL", "OLED","L",2), ("SDA","OLED","L",3),
  # ADXL345
  ("+3V3","ADXL","L",0), ("GND","ADXL","L",1),
  ("SDA", "ADXL","L",2), ("SCL","ADXL","L",3),
  ("GND", "ADXL","L",4),  # SDO→GND → addr 0x53
  # Encoder
  ("PCF_ENC_CLK","ENC","L",0), ("PCF_ENC_DT","ENC","L",1),
  ("PCF_ENC_SW", "ENC","L",2), ("GND",        "ENC","L",3),
  # Endstops
  ("+3V3",   "ES1","L",0), ("GND","ES1","L",1), ("PCF_ES1","ES1","L",2),
  ("+3V3",   "ES2","L",0), ("GND","ES2","L",1), ("PCF_ES2","ES2","L",2),
  # Motor
  ("MOT_A1","MOTOR","L",0), ("MOT_A2","MOTOR","L",1),
  ("MOT_B1","MOTOR","L",2), ("MOT_B2","MOTOR","L",3),
]

# ── Build schematic ────────────────────────────────────────────────────────
out = []
out.append('(kicad_sch (version 20230121) (generator "slider_gen")')
out.append('  (paper "A1")')
out.append('  (lib_symbols')

# Deduplicated symbol defs
seen = set()
for m in MODS.values():
    sn = m["sym"]
    if sn not in seen:
        out.append(make_symbol(sn, m["L"], m["R"]))
        seen.add(sn)
out.append('  )')
out.append('')

# Symbol instances
for key, m in MODS.items():
    sx, sy = POS[key]
    out.append(place(m["sym"], m["ref"], m["val"], sx, sy))

# Net labels
for net, mk, side, idx in LABELS:
    m  = MODS[mk]
    nl, nr = len(m["L"]), len(m["R"])
    sx, sy = POS[mk]
    px, py = pinpos(sx, sy, side, idx, nl, nr)
    ang = 180 if side == "L" else 0
    out.append(netlabel(net, px, py, ang))

out.append(')')

os.makedirs("hardware", exist_ok=True)
path = os.path.join(os.path.dirname(__file__), "slider.kicad_sch")
with open(path, "w") as f:
    f.write('\n'.join(out))
print(f"Written: {path}")
