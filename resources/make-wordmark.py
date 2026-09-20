#!/usr/bin/env python3
"""Regenerate resources/wordmark.svg.

The wordmark is "OmaKeyboard" drawn in Omarchy's own letterforms. o, m, a, r
and y are lifted unchanged from its wordmark (/usr/share/omarchy/logo.svg);
k, e, b, d and s are drawn here to the same construction, on a 15-unit grid,
16 rows tall, stems three cells wide, bars two cells thick, outer corners
stepped twice.

Run it after changing a glyph or the tracking table:

    python3 resources/make-wordmark.py
"""

import re

CELL = 15
ROWS = 16          # 240 de alto, de y=15 a y=255
TOP  = 15

src = open('/usr/share/omarchy/logo.svg').read()
paths = re.findall(r'd="([^"]+)"', src)
def startx(d): return float(re.match(r'[mM](-?\d*\.?\d+)', d.strip()).group(1))
ordered = sorted(paths, key=startx)
# (trazo original, x minima del glifo, ancho) medidos antes
ORIG = {
    'o': (ordered[0],    0.0, 135.0),
    'm': (ordered[1],  165.0, 225.0),
    'a': (ordered[2],  405.0, 150.0),
    'r': (ordered[3],  570.0, 150.0),
    'y': (ordered[6], 1080.0, 135.0),
}

# Letras nuevas como celdas encendidas por fila. Mismas reglas: montantes de
# 3 celdas, barras de 2, esquinas escalonadas de a una celda dos veces.
def spans(*ranges):
    out = set()
    for a, b in ranges:
        out |= set(range(a, b + 1))
    return out

NEW = {
 'k': [spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,2),(5,7)), spans((0,2),(5,7)),
       spans((0,2),(4,6)), spans((0,2),(4,6)), spans((0,4)),       spans((0,5)),
       spans((0,5)),       spans((0,4)),       spans((0,2),(4,6)), spans((0,2),(4,6)),
       spans((0,2),(5,7)), spans((0,2),(5,7)), spans((0,2),(6,8)), spans((0,2),(6,8))],

 'e': [spans((2,6)),       spans((1,7)),       spans((0,2),(6,8)), spans((0,2),(6,8)),
       spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,8)),       spans((0,8)),
       spans((0,2)),       spans((0,2)),       spans((0,2)),       spans((0,2)),
       spans((0,2)),       spans((0,2)),       spans((1,7)),       spans((2,6))],

 'b': [spans((0,2)),       spans((0,2)),       spans((0,2)),       spans((0,2)),
       spans((0,2)),       spans((0,2)),       spans((0,6)),       spans((0,7)),
       spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,2),(6,8)),
       spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,7)),       spans((0,6))],

 'd': [spans((6,8)),       spans((6,8)),       spans((6,8)),       spans((6,8)),
       spans((6,8)),       spans((6,8)),       spans((2,8)),       spans((1,8)),
       spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,2),(6,8)), spans((0,2),(6,8)),
       spans((0,2),(6,8)), spans((0,2),(6,8)), spans((1,8)),       spans((2,8))],

 's': [spans((2,6)),       spans((1,7)),       spans((0,2),(6,8)), spans((0,2),(6,8)),
       spans((0,2)),       spans((0,2)),       spans((0,6)),       spans((1,7)),
       spans((6,8)),       spans((6,8)),       spans((6,8)),       spans((6,8)),
       spans((0,2),(6,8)), spans((0,2),(6,8)), spans((1,7)),       spans((2,6))],
}
NEW_W = {k: 9 * CELL for k in NEW}

def rects_for(bitmap, x0):
    """Una barra por corrida contigua, en vez de una celda por celda."""
    out = []
    for r, cells in enumerate(bitmap):
        if not cells: continue
        cols = sorted(cells)
        run_start = prev = cols[0]
        for c in cols[1:] + [None]:
            if c is not None and c == prev + 1:
                prev = c; continue
            x = x0 + run_start * CELL
            w = (prev - run_start + 1) * CELL
            out.append(f'<path d="M{x:g} {TOP + r*CELL:g}h{w:g}v{CELL}h-{w:g}z"/>')
            if c is not None:
                run_start = prev = c
    return out

WORD = "omakeyboard"
# Tracking per pair, not a single value. Measured rather than eyeballed: each
# glyph's per-row edges give the area of white a pair encloses, and the gap is
# chosen so every pair encloses about the same area, never closer than one
# cell. Omarchy's own wordmark spaces itself the same way -- its gaps run
# 30, 15, 15, 30, 0, 15 rather than one uniform value.
# Diez pares: dropping the trailing "s" removes only the last entry,
# since every remaining pair is unchanged.
TRACKS = [45, 30, 45, 15, 15, 45, 15, 30, 30, 15]
parts, x = [], 0.0
for i, ch in enumerate(WORD):
    if ch in ORIG:
        d, gx0, w = ORIG[ch]
        parts.append(f'<g transform="translate({x - gx0:g} 0)"><path d="{d}"/></g>')
        adv = w
    else:
        parts.extend(rects_for(NEW[ch], x))
        adv = NEW_W[ch]
    x += adv + (TRACKS[i] if i < len(TRACKS) else 0)
width = x

# Finish, matched to the app icon (resources/omakeyboard-logo.png): each letter
# is drawn like one of its keycaps -- a face shading from #53B7F1 to #2172D2,
# sampled from the keycap's top and bottom, sitting on a #023283 base one grid
# cell lower, the keycap's bevel. Depth comes from geometry, not from filters:
# Qt renders SVG Tiny 1.2, which silently ignores blur and drop shadows.
BEVEL = 15
HEIGHT = 285 + BEVEL
body = chr(10).join("      " + p for p in parts)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:g} {HEIGHT}" width="{width:g}" height="{HEIGHT}">
  <!--
    "OmaKeyboard" in Omarchy's own letterforms. o, m, a, r and y are its
    wordmark's glyphs reused unchanged (/usr/share/omarchy/logo.svg); k, e, b
    and d are drawn to the same construction: a 15-unit grid, 16 rows tall,
    stems three cells wide, bars two cells thick, and outer corners stepped
    twice. Monocase, like the original: these letters have no capitals.
    Coloured like the keycaps in the app icon. Generated by make-wordmark.py.
  -->
  <defs>
    <!-- userSpaceOnUse: one gradient across the whole word. The default,
         objectBoundingBox, would restart it inside every row of cells. -->
    <linearGradient id="face" gradientUnits="userSpaceOnUse" x1="0" y1="15" x2="0" y2="255">
      <stop offset="0" stop-color="#53B7F1"/>
      <stop offset="1" stop-color="#2172D2"/>
    </linearGradient>
  </defs>
  <g fill="#023283" fill-rule="evenodd" clip-rule="evenodd" transform="translate(0 {BEVEL})">
{body}
  </g>
  <g fill="url(#face)" fill-rule="evenodd" clip-rule="evenodd">
{body}
  </g>
</svg>
'''
open(__file__.rsplit('/', 1)[0] + '/wordmark.svg', 'w').write(svg)
print(f"ancho {width:g}, {len(parts)} trazos")
