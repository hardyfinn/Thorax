#!/usr/bin/env python3
# Mock-up picture of the panel with knobs, buttons, jacks, lights and a sample
# screen drawn in, so the layout can be checked without Rack.
#   python3 tools/preview.py out.png
import os, sys, math, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
svg = open(os.path.join(HERE, "..", "res", "Thorax.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
g = []
def jack(x, y):
    g.append(f'<circle cx="{x}" cy="{y}" r="4.1" fill="#C9CCD1"/><circle cx="{x}" cy="{y}" r="3.1" fill="#7C8088"/><circle cx="{x}" cy="{y}" r="1.9" fill="#0B0B0C"/>')
def knob(x, y, a, r=4.8):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#2E2F33" stroke="#55575E" stroke-width="0.5"/>')
    ang = math.radians(-135 + 270 * a)
    g.append(f'<line x1="{x}" y1="{y}" x2="{x + (r-0.8)*math.sin(ang):.2f}" y2="{y - (r-0.8)*math.cos(ang):.2f}" stroke="#EEE" stroke-width="0.7" stroke-linecap="round"/>')
def light(x, y, b, col="#FFD23C", r=1.0):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#3a3320"/><circle cx="{x}" cy="{y}" r="{r}" fill="{col}" fill-opacity="{b}"/>')
def button(x, y, on, col="#F4F1EA", r=3.05):
    g.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#2B2D33"/><circle cx="{x}" cy="{y}" r="{r*0.72:.2f}" fill="{col if on else "#55565c"}"/>')
vals = [[1.0/4, .25, 0, 0, 7/11, 1], [.8, .3, 0, .15, 0, 0], [3/16, 9/11, .5, .5, 0, 0], [.5, .5, 2/7, 0, .5, 0], [2/9, 0, .4, None, None, None]]
for r, y in enumerate(L["ROW_Y"]):
    for c, x in enumerate(L["COL_X"]):
        v = vals[r][c]
        if v is None:
            button(x, y, c == 3, "#E2A93B" if c == 3 else "#F4F1EA", 3.6)
        else:
            knob(x, y, v)
# screen
dx, dy, dw, dh = L["DISP_X"], L["DISP_Y"], L["DISP_W"], L["DISP_H"]
g.append(f'<rect x="{dx}" y="{dy}" width="{dw}" height="{dh}" rx="1" fill="#14110D"/>')
g.append(f'<text x="{dx+2}" y="{dy+5}" font-family="monospace" font-size="3.4" fill="#E2A93B">TRACK 1</text>')
g.append(f'<text x="{dx+dw-2}" y="{dy+5}" font-family="monospace" font-size="3.4" fill="#F1E9DA" text-anchor="end">PULSES  4 of 16</text>')
pat = [1,0,0,0]*4
cw = (dw - 4) / 16
for i, h in enumerate(pat):
    col = "#E2A93B" if h else "#3A332A"
    g.append(f'<rect x="{dx+2+i*cw+0.3:.2f}" y="{dy+8}" width="{cw-0.6:.2f}" height="7" rx="0.5" fill="{col}"/>')
g.append(f'<rect x="{dx+2+5*cw:.2f}" y="{dy+15.6}" width="{cw:.2f}" height="0.8" fill="#F1E9DA"/>')
for i in range(8):
    y = L["TRK_Y0"] + i * L["TRK_DY"]
    light(L["X_LED"], y, 1.0 if i in (0, 2) else 0.0, "#E2A93B", 0.9)
    button(L["X_SEL"], y, i == 0, "#F4F1EA", 2.6)
    button(L["X_MUTE"], y, i == 5, "#D9714A", 2.6)
    for x in (L["X_GATE"], L["X_PITCH"], L["X_VEL"]):
        jack(x, y)
for x in (L["X_CLKIN"], L["X_RSTIN"], L["X_RUNIN"], L["X_TRANS"], L["X_CLKOUT"]):
    jack(x, L["GJ_Y"])
W = 203.2
for x in (7.6, W - 7.6):
    for y in (2.5, 126.0): g.append(f'<circle cx="{x}" cy="{y}" r="1.6" fill="#8A8D93"/>')
svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
tmp = out + ".svg"; open(tmp, "w").write(svg)
pw, ph = 1016, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(300);
await p.screenshot({{path: '{out}'}}); await b.close(); }})();"""
open(out + ".js", "w").write(js)
subprocess.run(["node", out + ".js"], check=True, cwd="/tmp")
os.remove(out + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", out)
