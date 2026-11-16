#!/usr/bin/env python3
# Draws res/Thorax.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/Thorax.cpp (the numbers in the LAYOUT block are the same in both).
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 203.2, 128.5  # 40 HP

# Palette: dark chitin shell with amber and verdigris accents
BG = "#1D1B19"
INK = "#F1E9DA"       # control names
HINT = "#E2A93B"      # where the control lives on the T-1
SOFT = "#9A8F80"
LINE = "#3A3631"
GROUP = {"RHYTHM": "#E2A93B", "FEEL": "#D9714A", "REPEATS": "#6FAE9B", "RANDOM": "#B48AC9",
         "PITCH": "#7FA7D9", "KEY": "#7FA7D9", "CLOCK": "#C9C1B2"}
OUT_BG = "#2C2824"

# ---- LAYOUT (same numbers in src/Thorax.cpp) ----
COL_X = [15.0, 34.0, 53.0, 72.0, 91.0, 110.0]
ROW_Y = [27.0, 48.0, 69.0, 90.0, 111.0]
DISP_X, DISP_Y, DISP_W, DISP_H = 124.0, 12.5, 75.0, 18.0
TRK_Y0, TRK_DY = 41.0, 9.6
X_NUM, X_LED, X_SEL, X_MUTE, X_GATE, X_PITCH, X_VEL = 126.6, 131.4, 137.5, 148.5, 162.5, 175.5, 188.5
GJ_Y = 121.5
X_CLKIN, X_RSTIN, X_RUNIN, X_TRANS, X_CLKOUT = 131.0, 144.0, 157.0, 170.0, 183.0

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)
HINT_SIZE = 2.05

# (label, T-1 location, group) for every knob, row by row. None = empty slot.
GRID = [
    [("STEPS", "T-1: STEPS", "RHYTHM"), ("PULSES", "T-1: PULSES", "RHYTHM"), ("ROTATE", "CTRL+PULSES", "RHYTHM"),
     ("LENGTH", "T-1: LENGTH", "RHYTHM"), ("DIVIDE", "T-1: DIVISION", "RHYTHM"), ("CHANCE", "CTRL+VELOCITY", "RHYTHM")],
    [("VELOCITY", "T-1: VELOCITY", "FEEL"), ("ACCENT", "T-1: ACCENT", "FEEL"), ("GROOVE", "CTRL+ACCENT", "FEEL"),
     ("SUSTAIN", "T-1: SUSTAIN", "FEEL"), ("SWING", "T-1: TIMING", "FEEL"), ("DELAY", "CTRL+TIMING", "FEEL")],
    [("REPEATS", "T-1: REPEATS", "REPEATS"), ("TIME", "T-1: TIME", "REPEATS"), ("PACE", "CTRL+TIME", "REPEATS"),
     ("RAMP", "CTRL+REPEATS", "REPEATS"), ("RANDOM", "T-1: RANDOM", "RANDOM"), ("RATE", "CTRL+RANDOM", "RANDOM")],
    [("PITCH", "T-1: PITCH", "PITCH"), ("HARMONY", "CTRL+PITCH", "PITCH"), ("VOICING", "T-1: VOICING", "PITCH"),
     ("STYLE", "CTRL+VOICING", "PITCH"), ("RANGE", "T-1: RANGE", "PITCH"), ("PHRASE", "CTRL+RANGE", "PITCH")],
    [("SCALE", "T-1: SCALE", "KEY"), ("ROOT", "CTRL+SCALE", "KEY"), ("TEMPO", "T-1: TEMPO", "CLOCK"),
     ("RUN", "T-1: PLAY", "CLOCK"), ("RESET", "back to step 1", "CLOCK"), ("DICE", "random track", "CLOCK")],
]
ROW_NAMES = ["RHYTHM", "FEEL", "REPEATS  ·  RANDOM", "PITCH", "KEY  ·  CLOCK"]

fonts = {}


def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.04, rotate=None):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": -width * scale / 2, "end": -width * scale}.get(anchor, 0.0)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, 0)))
        cx += gs[n].width + track
    tf = f"translate({x:.3f} {y:.3f})" + (f" rotate({rotate})" if rotate else "")
    return f'<path transform="{tf}" fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

# title
add(text("THORAX", 6.0, 10.6, 5.6, FONT_BOLD, INK, anchor="start", spacing=0.12))
# three little segments after the title (a thorax has three)
for i, c in enumerate(["#E2A93B", "#D9714A", "#6FAE9B"]):
    add(f'<rect x="{35.6 + i * 3.4}" y="5.4" width="2.6" height="5.2" rx="1.0" fill="{c}"/>')
add(text("8-track algorithmic sequencer", 47.5, 10.2, 2.6, FONT_MED, SOFT, anchor="start"))
add(text("labelled T-1 companion", 47.5, 6.4, 2.2, FONT_MED, HINT, anchor="start"))
add(f'<line x1="3.5" y1="14.0" x2="119.0" y2="14.0" stroke="{LINE}" stroke-width="0.35"/>')

# knob rows
for r, row in enumerate(GRID):
    y = ROW_Y[r]
    # group tag: small coloured bar and the group name turned on its side
    gcol = GROUP[row[0][2]]
    add(f'<rect x="2.6" y="{y - 9.6}" width="1.1" height="18.6" rx="0.5" fill="{gcol}"/>')
    add(text(ROW_NAMES[r].split("  ")[0], 5.9, y, 1.9, FONT_BOLD, gcol, rotate=-90))
    if r > 0:
        add(f'<line x1="3.5" y1="{y - 11.0}" x2="119.0" y2="{y - 11.0}" stroke="{LINE}" stroke-width="0.25"/>')
    for c, item in enumerate(row):
        if not item:
            continue
        label, hint, grp = item
        x = COL_X[c]
        add(text(label, x, y - 7.0, CONTROL, FONT_BOLD, INK))
        add(text(hint, x, y + 9.3, HINT_SIZE, FONT_MED, HINT if hint.startswith(("T-1", "CTRL")) else SOFT))
        if grp != row[0][2] and c > 0 and row[c - 1][2] != grp:
            # a second group starts in this row: short divider and coloured tick
            add(f'<line x1="{x - 9.5}" y1="{y - 9.6}" x2="{x - 9.5}" y2="{y + 9.6}" stroke="{LINE}" stroke-width="0.35"/>')

# (second group in a row is marked by the divider line only)

# divider between the knob section and the track section
add(f'<line x1="120.6" y1="4.0" x2="120.6" y2="124.0" stroke="{LINE}" stroke-width="0.35"/>')

# display window frame (the screen itself is drawn by the module)
add(f'<rect x="{DISP_X - 0.6}" y="{DISP_Y - 0.6}" width="{DISP_W + 1.2}" height="{DISP_H + 1.2}" rx="1.4" fill="#0E0D0C" stroke="#4A443D" stroke-width="0.3"/>')

# track section
add(f'<rect x="{X_GATE - 5.6}" y="{TRK_Y0 - 9.2}" width="{X_VEL + 5.6 - (X_GATE - 5.6)}" height="{TRK_DY * 7 + 14.0}" rx="1.4" fill="{OUT_BG}"/>')
HDR_Y = TRK_Y0 - 5.6
add(text("TRACK", (X_NUM + X_SEL) / 2 + 0.8, HDR_Y, JACK, FONT_BOLD, INK))
add(text("MUTE", X_MUTE, HDR_Y, JACK, FONT_BOLD, INK))
add(text("GATE", X_GATE, HDR_Y, JACK, FONT_BOLD, INK))
add(text("PITCH", X_PITCH, HDR_Y, JACK, FONT_BOLD, INK))
add(text("VEL", X_VEL, HDR_Y, JACK, FONT_BOLD, INK))
for i in range(8):
    y = TRK_Y0 + i * TRK_DY
    add(text(str(i + 1), X_NUM, y + 1.15, CONTROL, FONT_BOLD, INK))
    if i:
        add(f'<line x1="{X_NUM - 2.4}" y1="{y - TRK_DY / 2}" x2="{X_MUTE + 4}" y2="{y - TRK_DY / 2}" stroke="{LINE}" stroke-width="0.2"/>')

# global jacks
GL_Y = GJ_Y - 5.6
for x, s in [(X_CLKIN, "CLOCK"), (X_RSTIN, "RESET"), (X_RUNIN, "RUN"), (X_TRANS, "TRANSP")]:
    add(text(s, x, GL_Y, JACK, FONT_BOLD, INK))
add(f'<rect x="{X_CLKOUT - 6.0}" y="{GJ_Y - 9.0}" width="12.0" height="13.4" rx="1.2" fill="{OUT_BG}"/>')
add(text("CLK", X_CLKOUT, GL_Y, JACK, FONT_BOLD, INK))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(parts) + "\n</svg>\n")
open(os.path.join(HERE, "Thorax.svg"), "w").write(svg)
print("ok")

# --- Haer maker logo (res/haer_logo.py) on the bottom rail
import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "Thorax.svg"), x=60.0, y=126.0, cap=3.0, bg=BG)
