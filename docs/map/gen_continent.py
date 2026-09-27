#!/usr/bin/env python3
"""博宇大陆 · 现今势力图 generator.

Writes continent-present.svg next to this file. Render a PNG with:
    magick -density 96 continent-present.svg continent-present.png

Only numpy + Pillow are needed; Pillow rasterizes region masks so glyph
placement (trees, peaks, dunes) can test "is this point on land / in the
forest / clear of rivers and labels" in O(1).
"""

import math
import random
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

W, H = 2400, 1600
OUT = Path(__file__).with_name("continent-present.svg")

# Every random choice flows from this seed, so re-running reproduces the map.
SEED = 11

INK = "#3a2e22"
LAND = "#ebdfbf"
SEA_CENTER = "#b3c8c3"
SEA_EDGE = "#8aa6a6"
SHALLOW = "#c6d8cf"
RIVER = "#5a8ca6"
LAKE = "#a6c4c7"
FONT = "Noto Serif CJK SC"

EMPIRE = "#9c2f25"
NORTH = "#56657a"
FOREST = "#3d7446"
WASTE = "#b0692a"
ISLES = "#2a7a80"
MYSTIC = "#4f5bb0"
STARDUST = "#2b9cc4"
ORG = "#6a3a86"
CONFLICT = "#c0301f"
WAR = "#4a3d33"


# ---------------------------------------------------------------- geometry

def _subdivide(a, b, depth, rough, rng):
    if depth == 0:
        return []
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    if length < 3:
        return []
    off = max(-1.0, min(1.0, rng.gauss(0, 0.5))) * rough * length
    m = ((a[0] + b[0]) / 2 - dy / length * off, (a[1] + b[1]) / 2 + dx / length * off)
    return (_subdivide(a, m, depth - 1, rough, rng) + [m]
            + _subdivide(m, b, depth - 1, rough, rng))


def fractal(pts, closed, depth, rough, rng):
    """Midpoint displacement: turns a hand-placed control polygon into a natural edge."""
    out = []
    n = len(pts)
    for i in range(n if closed else n - 1):
        a, b = pts[i], pts[(i + 1) % n]
        out.append(a)
        out.extend(_subdivide(a, b, depth, rough, rng))
    if not closed:
        out.append(pts[-1])
    return out


def chaikin(pts, closed, iterations=2):
    for _ in range(iterations):
        out = []
        n = len(pts)
        segs = n if closed else n - 1
        if not closed:
            out.append(pts[0])
        for i in range(segs):
            a, b = pts[i], pts[(i + 1) % n]
            out.append((0.75 * a[0] + 0.25 * b[0], 0.75 * a[1] + 0.25 * b[1]))
            out.append((0.25 * a[0] + 0.75 * b[0], 0.25 * a[1] + 0.75 * b[1]))
        if not closed:
            out.append(pts[-1])
        pts = out
    return pts


def d_of(pts, closed=True):
    s = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in pts)
    return s + (" Z" if closed else "")


def blob(cx, cy, rx, ry, n, rng, wobble=0.25, rot=0.0):
    """Irregular closed control polygon around a centre (islands, lakes, areas)."""
    pts = []
    for i in range(n):
        t = 2 * math.pi * i / n
        r = 1 + rng.uniform(-wobble, wobble)
        x, y = math.cos(t) * rx * r, math.sin(t) * ry * r
        c, s = math.cos(rot), math.sin(rot)
        pts.append((cx + x * c - y * s, cy + x * s + y * c))
    return pts


def resample(pts, step):
    """Evenly spaced points along a polyline, each with its tangent angle."""
    seg = [math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
           for i in range(len(pts) - 1)]
    total = sum(seg)
    out, s, i, acc = [], 0.0, 0, 0.0
    while s <= total:
        while i < len(seg) - 1 and acc + seg[i] < s:
            acc += seg[i]
            i += 1
        t = 0 if seg[i] == 0 else (s - acc) / seg[i]
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        out.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, math.atan2(y1 - y0, x1 - x0)))
        s += step
    return out


def poly_len(pts):
    return sum(math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
               for i in range(len(pts) - 1))


class Mask:
    """1:1 raster of a set of polygons, for point-in-region queries."""

    def __init__(self, polys=(), lines=(), width=0):
        self.img = Image.new("L", (W, H), 0)
        dr = ImageDraw.Draw(self.img)
        for p in polys:
            dr.polygon([(x, y) for x, y in p], fill=255)
        for p in lines:
            dr.line([(x, y) for x, y in p], fill=255, width=width, joint="curve")
        self.a = None

    def add_poly(self, p, value=255):
        ImageDraw.Draw(self.img).polygon([(x, y) for x, y in p], fill=value)
        self.a = None

    def add_line(self, p, width):
        ImageDraw.Draw(self.img).line([(x, y) for x, y in p], fill=255, width=int(width),
                                      joint="curve")
        self.a = None

    def add_circle(self, x, y, r):
        ImageDraw.Draw(self.img).ellipse((x - r, y - r, x + r, y + r), fill=255)
        self.a = None

    def add_rect(self, x0, y0, x1, y1):
        ImageDraw.Draw(self.img).rectangle((x0, y0, x1, y1), fill=255)
        self.a = None

    def __contains__(self, p):
        if self.a is None:
            self.a = np.asarray(self.img)
        x, y = int(p[0]), int(p[1])
        return 0 <= x < W and 0 <= y < H and self.a[y, x] > 0

    def area(self):
        return int((np.asarray(self.img) > 0).sum())


# ------------------------------------------------------------- world data
# Hand-placed control points; fractal() adds the natural detail. Clockwise
# from the north-west cape.

COAST = [
    (330, 480), (345, 420), (390, 375), (450, 345), (520, 318), (585, 290), (640, 262),
    (700, 250), (760, 215), (800, 170), (850, 185), (905, 200), (960, 165), (1010, 138),
    (1070, 160), (1110, 200), (1140, 245), (1165, 285), (1190, 270), (1200, 230),
    (1230, 190), (1290, 160), (1360, 150), (1430, 170), (1490, 150), (1560, 170),
    (1620, 205), (1680, 225), (1735, 260), (1780, 300), (1800, 350),
    (1790, 400), (1830, 450), (1860, 520), (1850, 590), (1800, 630), (1770, 680),
    (1800, 720), (1860, 760), (1880, 840), (1850, 900), (1860, 960), (1830, 1010),
    (1855, 1055), (1890, 1100), (1925, 1160), (1950, 1230), (1945, 1285), (1910, 1300),
    (1870, 1270), (1830, 1255), (1780, 1265), (1730, 1300),
    (1680, 1335), (1620, 1370), (1550, 1395), (1480, 1380), (1430, 1340), (1400, 1280),
    (1380, 1220), (1340, 1170), (1290, 1150), (1250, 1160), (1200, 1185), (1165, 1240),
    (1140, 1310), (1100, 1375), (1060, 1420), (1000, 1455), (940, 1445), (890, 1405),
    (840, 1372), (780, 1352), (720, 1342), (670, 1300), (630, 1245), (590, 1190),
    (530, 1150), (470, 1120), (420, 1085), (375, 1040), (345, 980),
    (320, 910), (300, 840), (295, 800), (240, 780), (200, 745), (190, 700), (225, 670),
    (280, 655), (300, 620), (335, 570), (320, 520),
]

# (cx, cy, rx, ry, rotation). The archipelago proper — all of it tribal.
ISLES_BIG = [
    (2140, 780, 115, 80, 0.3), (2010, 470, 60, 42, -0.4), (2255, 560, 72, 48, 0.6),
    (2070, 1030, 70, 52, -0.2), (2235, 1135, 85, 55, 0.4), (2065, 1335, 48, 34, 0.1),
    (2285, 1330, 62, 40, -0.5), (1990, 330, 40, 28, 0.2), (2300, 900, 52, 40, 0.0),
    (2175, 380, 45, 30, 0.7), (2338, 1010, 18, 13, 0.3), (2330, 360, 20, 14, -0.2),
]
# Islets hugging the continent; they take the faction of whatever region they sit in.
ISLETS_COAST = [
    (215, 880, 20, 13, 0.2), (1500, 1455, 26, 15, -0.1), (1160, 97, 22, 12, 0.1),
    (820, 1440, 18, 11, 0.4),
]

# Ceasefire line, drawn left edge to right edge through the sea so the four
# faction regions partition the whole canvas. A and B are where the tribal
# dividers branch off.
A = (625, 495)
B = (1570, 440)
C_WEST = [(0, 1150), (250, 1120), (400, 1095), (470, 1070), (540, 1020), (590, 950),
          (615, 870), (620, 790), (600, 710), (580, 630), (590, 560), A]
C_NORTH = [A, (700, 480), (780, 492), (860, 470), (950, 462), (1040, 450), (1130, 452),
           (1220, 458), (1310, 440), (1400, 446), (1480, 436), B]
C_EAST = [B, (1590, 520), (1605, 610), (1585, 700), (1600, 790), (1640, 880), (1700, 950),
          (1770, 990), (1840, 1015), (1950, 1030), (W, 1040)]
D_WEST = [A, (570, 455), (500, 425), (430, 400), (360, 385), (200, 370), (0, 360)]
D_EAST = [B, (1640, 405), (1710, 370), (1770, 330), (1830, 290), (1950, 200), (2000, 0)]

FOREST_EAST = [
    (1420, 470), (1500, 440), (1600, 452), (1700, 425), (1790, 425), (1850, 480),
    (1870, 560), (1830, 640), (1860, 740), (1880, 830), (1850, 920), (1800, 990),
    (1720, 1000), (1640, 960), (1560, 900), (1500, 820), (1440, 740), (1400, 640),
    (1390, 550),
]
WOODS = [
    [(640, 1080), (720, 1040), (790, 1070), (800, 1150), (740, 1190), (660, 1170)],
    [(900, 1240), (980, 1220), (1040, 1260), (1030, 1330), (950, 1350), (890, 1310)],
    [(1650, 1100), (1740, 1080), (1790, 1130), (1760, 1200), (1680, 1210), (1630, 1160)],
    [(1230, 690), (1320, 670), (1370, 730), (1320, 800), (1240, 780)],
]
TAIGA = [
    [(540, 300), (640, 268), (700, 300), (640, 345), (560, 345)],
    [(1245, 225), (1340, 195), (1420, 222), (1370, 262), (1265, 262)],
    [(850, 215), (930, 205), (960, 250), (900, 270), (845, 255)],
]
WASTELAND = [
    (320, 520), (420, 470), (540, 470), (640, 520), (680, 620), (700, 740), (690, 860),
    (660, 960), (590, 1040), (500, 1070), (400, 1040), (330, 960), (290, 840), (280, 700),
    (300, 600),
]
TUNDRA = [
    (380, 380), (500, 330), (700, 240), (900, 180), (1100, 140), (1300, 140), (1500, 140),
    (1700, 210), (1800, 320), (1760, 360), (1600, 320), (1400, 290), (1200, 310),
    (1000, 290), (800, 320), (600, 370), (450, 400),
]
MAIN_RANGE = [
    (500, 395), (600, 380), (700, 360), (800, 335), (900, 312), (1000, 302), (1100, 318),
    (1200, 330), (1300, 305), (1400, 292), (1500, 300), (1600, 322), (1680, 345),
    (1745, 385),
]
HILL_BANDS = [  # (spine, spread)
    ([(560, 450), (700, 440), (820, 420), (960, 400), (1100, 405), (1250, 410),
      (1400, 395), (1520, 400)], 22),
    ([(1390, 1060), (1450, 1110), (1500, 1170)], 30),
    ([(360, 560), (380, 630), (395, 700)], 18),
    ([(1140, 590), (1200, 610), (1260, 630)], 18),
]

# Rivers run source -> mouth.
RIVERS = [
    ("main", [(1060, 360), (1045, 440), (1015, 530), (990, 620), (982, 700), (995, 775),
              (1030, 850), (1080, 930), (1130, 1010), (1175, 1075), (1215, 1125),
              (1250, 1163)], 4.6),
    ("west", [(800, 430), (840, 490), (880, 555), (915, 630), (950, 690), (984, 712)], 2.6),
    ("east", [(1330, 400), (1310, 480), (1270, 560), (1210, 640), (1140, 720),
              (1080, 790), (1036, 862)], 2.8),
    ("cang", [(1640, 390), (1660, 470), (1680, 560), (1700, 650), (1708, 752)], 2.6),
    ("cang_out", [(1742, 785), (1790, 792), (1830, 772), (1872, 772)], 3.2),
    ("sw", [(780, 880), (760, 960), (740, 1050), (730, 1140), (735, 1230), (748, 1348)], 3.0),
    ("se", [(1480, 880), (1500, 980), (1520, 1080), (1530, 1180), (1522, 1280),
            (1516, 1390)], 3.0),
]
DRY_RIVER = [(610, 560), (560, 630), (500, 700), (440, 760), (380, 800), (300, 832)]
LAKES = [("星湖", 1716, 776, 38, 25, 0.2), ("冰湖", 1420, 232, 30, 14, -0.1)]


def build(rng):
    g = {}
    g["coast"] = chaikin(fractal(COAST, True, 5, 0.2, rng), True, 1)
    g["isles"] = [fractal(blob(cx, cy, rx, ry, 9, rng, 0.3, rot), True, 4, 0.26, rng)
                  for cx, cy, rx, ry, rot in ISLES_BIG]
    # Scattered islets along the chain; rejected if they would crowd a bigger isle
    # or the compass in the top-right corner.
    shore = Mask([g["coast"]], [g["coast"]], 90)
    tiny = []
    while len(tiny) < 16:
        cx, cy = rng.uniform(1960, 2350), rng.uniform(290, 1420)
        r = rng.uniform(5, 14)
        if (cx > 2130 and cy < 300) or (cx, cy) in shore:
            continue
        near = [(ix, iy, max(irx, iry)) for ix, iy, irx, iry, _ in ISLES_BIG]
        near += [(tx, ty, tr) for tx, ty, tr in tiny]
        if any(math.hypot(cx - ix, cy - iy) < ir + r + 18 for ix, iy, ir in near):
            continue
        tiny.append((cx, cy, r))
    g["isles"] += [fractal(blob(cx, cy, r, r * rng.uniform(0.6, 0.9), 7, rng, 0.35,
                                rng.uniform(0, 3)), True, 3, 0.25, rng)
                   for cx, cy, r in tiny]
    g["islets"] = [fractal(blob(cx, cy, rx, ry, 7, rng, 0.3, rot), True, 3, 0.25, rng)
                   for cx, cy, rx, ry, rot in ISLETS_COAST]

    cw = fractal(C_WEST, False, 4, 0.14, rng)
    cn = fractal(C_NORTH, False, 4, 0.14, rng)
    ce = fractal(C_EAST, False, 4, 0.14, rng)
    dw = fractal(D_WEST, False, 4, 0.16, rng)
    de = fractal(D_EAST, False, 4, 0.16, rng)
    g["ceasefire"] = cw + cn[1:] + ce[1:]
    g["dividers"] = [dw, de]
    g["region"] = {
        "empire": g["ceasefire"] + [(W, H), (0, H)],
        "west": cw + dw[1:],
        "north": [(0, 0), (2000, 0)] + de[::-1][1:] + cn[::-1][1:] + dw[1:],
        "east": de + [(W, 0), (W, 1040)] + ce[::-1][1:-1],
    }

    g["forest"] = fractal(FOREST_EAST, True, 4, 0.3, rng)
    g["woods"] = [fractal(p, True, 3, 0.3, rng) for p in WOODS]
    g["taiga"] = [fractal(p, True, 3, 0.3, rng) for p in TAIGA]
    g["waste"] = fractal(WASTELAND, True, 4, 0.3, rng)
    g["tundra"] = fractal(TUNDRA, True, 4, 0.3, rng)
    g["range"] = chaikin(fractal(MAIN_RANGE, False, 3, 0.15, rng), False)
    g["hills"] = [(chaikin(fractal(p, False, 2, 0.15, rng), False), s) for p, s in HILL_BANDS]
    g["rivers"] = [(name, chaikin(fractal(p, False, 4, 0.2, rng), False, 2), w)
                   for name, p, w in RIVERS]
    g["dry"] = chaikin(fractal(DRY_RIVER, False, 4, 0.2, rng), False, 2)
    g["lakes"] = [(name, fractal(blob(cx, cy, rx, ry, 9, rng, 0.2, rot), True, 3, 0.25, rng))
                  for name, cx, cy, rx, ry, rot in LAKES]
    return g


# ------------------------------------------------------------------ layers

def tapered(pts, w0, w1, color, pieces=14, opacity=1.0):
    """A river as short round-capped runs whose width grows toward the mouth."""
    out = []
    n = len(pts)
    for k in range(pieces):
        i0 = k * (n - 1) // pieces
        i1 = (k + 1) * (n - 1) // pieces
        if i1 <= i0:
            continue
        w = w0 + (w1 - w0) * (k + 0.5) / pieces
        out.append(f'<path d="{d_of(pts[i0:i1 + 1], False)}" fill="none" stroke="{color}" '
                   f'stroke-width="{w:.2f}" stroke-linecap="round" stroke-linejoin="round" '
                   f'opacity="{opacity}"/>')
    return "".join(out)


def layer_base(g):
    land = [g["coast"]] + g["isles"] + g["islets"]
    mainland = [g["coast"]] + g["islets"]
    land_d = " ".join(d_of(p) for p in land)
    defs = [
        f'<path id="landshape" d="{land_d}"/>',
        f'<clipPath id="land"><use href="#landshape"/></clipPath>',
        '<clipPath id="mainland">' + "".join(f'<path d="{d_of(p)}"/>' for p in mainland)
        + "</clipPath>",
        '<clipPath id="isles">' + "".join(f'<path d="{d_of(p)}"/>' for p in g["isles"])
        + "</clipPath>",
        f'<radialGradient id="sea" gradientUnits="userSpaceOnUse" cx="{W * 0.45}" '
        f'cy="{H * 0.5}" r="{W * 0.62}"><stop offset="0" stop-color="{SEA_CENTER}"/>'
        f'<stop offset="1" stop-color="{SEA_EDGE}"/></radialGradient>',
        f'<filter id="soft" filterUnits="userSpaceOnUse" x="0" y="0" width="{W}" '
        f'height="{H}"><feGaussianBlur stdDeviation="9"/></filter>',
        f'<filter id="softer" filterUnits="userSpaceOnUse" x="0" y="0" width="{W}" '
        f'height="{H}"><feGaussianBlur stdDeviation="18"/></filter>',
    ]
    for name, poly in g["region"].items():
        defs.append(f'<clipPath id="reg-{name}"><path d="{d_of(poly)}"/></clipPath>')
    # Ripple rings: each ring is (stroke w) minus (stroke w - line), applied to ink.
    rings = []
    for i, (w, op) in enumerate([(46, 0.16), (32, 0.24), (19, 0.36)]):
        defs.append(f'<mask id="ring{i}" maskUnits="userSpaceOnUse" x="0" y="0" width="{W}" '
                    f'height="{H}"><use href="#landshape" fill="white" stroke="white" '
                    f'stroke-width="{w}" stroke-linejoin="round"/><use href="#landshape" '
                    f'fill="black" stroke="black" stroke-width="{w - 2.4}" '
                    f'stroke-linejoin="round"/></mask>')
        rings.append(f'<rect width="{W}" height="{H}" fill="{INK}" opacity="{op}" '
                     f'mask="url(#ring{i})"/>')
    body = [
        f'<rect width="{W}" height="{H}" fill="url(#sea)"/>',
        f'<use href="#landshape" fill="none" stroke="{SHALLOW}" stroke-width="90" '
        f'stroke-linejoin="round" opacity="0.55" filter="url(#softer)"/>',
        *rings,
        f'<use href="#landshape" fill="{LAND}"/>',
    ]
    return defs, body


def layer_washes(g):
    """Terrain tints under everything else, feathered so biomes blend."""
    out = ['<g clip-path="url(#land)">']
    out.append(f'<path d="{d_of(g["tundra"])}" fill="#f4f6f1" opacity="0.75" '
               f'filter="url(#softer)"/>')
    out.append(f'<path d="{d_of(g["waste"])}" fill="#d8ae72" opacity="0.45" '
               f'filter="url(#softer)"/>')
    out.append(f'<path d="{d_of(g["range"], False)}" fill="none" stroke="#b39c78" '
               f'stroke-width="120" stroke-linecap="round" opacity="0.45" filter="url(#softer)"/>')
    for p in [g["forest"]] + g["woods"] + g["taiga"]:
        out.append(f'<path d="{d_of(p)}" fill="#8fa566" opacity="0.5" filter="url(#soft)"/>')
    out.append("</g>")
    return out


FACTION_COLOR = {"empire": EMPIRE, "west": WASTE, "north": NORTH, "east": FOREST}


def layer_factions(g):
    out = ['<g clip-path="url(#mainland)">']
    for name, poly in g["region"].items():
        c = FACTION_COLOR[name]
        tint = 0.05 if name == "empire" else 0.12
        out.append(f'<path d="{d_of(poly)}" fill="{c}" fill-opacity="{tint}"/>')
        # Inner band along the region's own border reads as "this side belongs to...".
        out.append(f'<g clip-path="url(#reg-{name})"><path d="{d_of(poly)}" fill="none" '
                   f'stroke="{c}" stroke-width="26" stroke-opacity="0.22" '
                   f'stroke-linejoin="round"/></g>')
    out.append("</g>")
    out.append(f'<g clip-path="url(#isles)"><rect width="{W}" height="{H}" fill="{ISLES}" '
               f'fill-opacity="0.16"/>'
               + "".join(f'<path d="{d_of(p)}" fill="none" stroke="{ISLES}" stroke-width="12" '
                         f'stroke-opacity="0.25"/>' for p in g["isles"]) + "</g>")
    return out


def layer_water(g):
    out = []
    for name, p in g["lakes"]:
        out.append(f'<path d="{d_of(p)}" fill="{LAKE}" stroke="{INK}" stroke-width="1.1"/>')
    out.append(f'<path d="{d_of(g["dry"], False)}" fill="none" stroke="#8c7350" '
               f'stroke-width="2" stroke-dasharray="3 5" stroke-linecap="round"/>')
    for name, p, w in g["rivers"]:
        w0 = 1.8 if name == "cang_out" else 0.8
        out.append(tapered(p, w0, w, RIVER))
    return out


def layer_borders(g):
    ce = d_of(g["ceasefire"], False)
    out = ['<g clip-path="url(#mainland)">']
    out.append(f'<path d="{ce}" fill="none" stroke="{LAND}" stroke-width="7" opacity="0.7"/>')
    out.append(f'<path d="{ce}" fill="none" stroke="{EMPIRE}" stroke-width="3" '
               f'stroke-dasharray="16 6 3 6" stroke-linejoin="round"/>')
    for p in g["dividers"]:
        out.append(f'<path d="{d_of(p, False)}" fill="none" stroke="#6b5a45" '
                   f'stroke-width="1.8" stroke-dasharray="2 6" stroke-linecap="round"/>')
    out.append("</g>")
    return out


def layer_coast_ink(g):
    return [f'<use href="#landshape" fill="none" stroke="{INK}" stroke-width="1.7" '
            f'stroke-linejoin="round"/>']


# ------------------------------------------------------------------ glyphs

SHADE = "#b59c74"
PAPER = "#efe5c9"

GLYPH_DEFS = f'''
<symbol id="tree-a" overflow="visible"><path d="M0 7 V3" stroke="{INK}" stroke-width="1"/>
<path d="M-5.5 2.5 C-8 1 -7 -4.5 -3.5 -4.5 C-3 -8 3 -8 3.5 -4.5 C7 -4.5 8 1 5.5 2.5 C4 5 -4 5 -5.5 2.5 Z"
 fill="#93a867" stroke="{INK}" stroke-width="0.9"/>
<path d="M5.5 2.5 C4 5 -1 5 -2 3.5 C1 3.5 4 1 4.5 -3.5 C7 -2 7.5 1.5 5.5 2.5 Z" fill="#6f8a4c"/></symbol>
<symbol id="tree-b" overflow="visible"><path d="M0 7 V2" stroke="{INK}" stroke-width="1"/>
<path d="M-5 3 C-8 0 -6 -6 -2 -6 C-1 -10 4 -9 4.5 -5 C8 -3 7 3 4 3.5 C2 5 -3 5 -5 3 Z"
 fill="#86a05e" stroke="{INK}" stroke-width="0.9"/>
<path d="M4 3.5 C2 5 -1 5 -2 4 C2 3 4 0 4.5 -5 C8 -3 7 3 4 3.5 Z" fill="#658244"/></symbol>
<symbol id="pine" overflow="visible"><path d="M0 7 V3" stroke="{INK}" stroke-width="1"/>
<path d="M0 -9 L3 -3 L1.8 -3 L5 3 L-5 3 L-1.8 -3 L-3 -3 Z" fill="#6f8c5c" stroke="{INK}"
 stroke-width="0.9" stroke-linejoin="round"/><path d="M0 -9 L3 -3 L1.8 -3 L5 3 L0.5 3 Z"
 fill="#526e45"/></symbol>
'''


def mountain(x, y, w, h, rng, snow):
    ax = x + rng.uniform(-0.12, 0.12) * w
    ay = y - h
    lx, rx = x - w / 2, x + w / 2
    ls = (lx + (ax - lx) * 0.5 + rng.uniform(-2, 2), y - h * rng.uniform(0.4, 0.55))
    rs = (ax + (rx - ax) * 0.5 + rng.uniform(-2, 2), y - h * rng.uniform(0.38, 0.52))
    ridge = (ax + w * rng.uniform(0.02, 0.1), y - h * rng.uniform(0.35, 0.5))
    foot = (x + w * rng.uniform(0.05, 0.18), y)
    f = lambda p: f"{p[0]:.1f} {p[1]:.1f}"
    parts = [
        f'<path d="M{f((lx, y))} L{f(ls)} L{f((ax, ay))} L{f(rs)} L{f((rx, y))} Z" fill="{PAPER}"/>',
        f'<path d="M{f((ax, ay))} L{f(rs)} L{f((rx, y))} L{f(foot)} L{f(ridge)} Z" '
        f'fill="{SHADE}" opacity="0.8"/>',
    ]
    if snow:
        t1, t2 = rng.uniform(0.28, 0.36), rng.uniform(0.25, 0.33)
        p1 = (ax + (ls[0] - ax) * t1 * 2, ay + (ls[1] - ay) * t1 * 2)
        p4 = (ax + (rs[0] - ax) * t2 * 2, ay + (rs[1] - ay) * t2 * 2)
        mid = (ax + (ridge[0] - ax) * 0.45, ay + (ridge[1] - ay) * 0.45)
        zig = ((p1[0] + mid[0]) / 2, (p1[1] + mid[1]) / 2 - h * 0.08)
        parts.append(f'<path d="M{f((ax, ay))} L{f(p1)} L{f(zig)} L{f(mid)} Z" fill="#fbfaf4"/>')
        parts.append(f'<path d="M{f((ax, ay))} L{f(mid)} L{f(p4)} Z" fill="#dde3e3"/>')
    parts.append(f'<path d="M{f((lx, y))} L{f(ls)} L{f((ax, ay))} L{f(rs)} L{f((rx, y))}" '
                 f'fill="none" stroke="{INK}" stroke-width="1.5" stroke-linejoin="round"/>')
    parts.append(f'<path d="M{f((ax, ay))} L{f(ridge)} L{f(foot)}" fill="none" stroke="{INK}" '
                 f'stroke-width="0.8" opacity="0.7"/>')
    return "".join(parts)


def hill(x, y, w, h):
    return (f'<path d="M{x - w / 2:.1f} {y:.1f} Q{x:.1f} {y - h * 1.7:.1f} {x + w / 2:.1f} {y:.1f} Z" '
            f'fill="{PAPER}" opacity="0.9"/>'
            f'<path d="M{x - w / 2:.1f} {y:.1f} Q{x:.1f} {y - h * 1.7:.1f} {x + w / 2:.1f} {y:.1f}" '
            f'fill="none" stroke="{INK}" stroke-width="1.2"/>'
            f'<path d="M{x + w * 0.12:.1f} {y - h * 0.7:.1f} Q{x + w * 0.3:.1f} {y - h * 0.45:.1f} '
            f'{x + w * 0.34:.1f} {y - h * 0.08:.1f}" fill="none" stroke="{INK}" stroke-width="0.9" '
            f'opacity="0.55"/>')


def scatter(cell, rng, prob=1.0, box=(0, 0, W, H)):
    """Jittered grid: evenly spread random points without the clumping of pure noise."""
    x0, y0, x1, y1 = box
    y = y0
    while y < y1:
        x = x0
        while x < x1:
            if rng.random() < prob:
                yield x + rng.uniform(0, cell), y + rng.uniform(0, cell)
            x += cell
        y += cell


def layer_glyphs(g, rng, land, blocked):
    items = []  # (base y, svg) — drawn back to front

    peaks = []
    for x, y, ang in resample(g["range"], 11):
        for _ in range(2):
            off = max(-85.0, min(85.0, rng.gauss(0, 34)))
            px = x - math.sin(ang) * off + rng.uniform(-6, 6)
            py = y + math.cos(ang) * off + rng.uniform(-4, 4)
            big = 1 - abs(off) / 85
            w = rng.uniform(26, 36) + 30 * big
            h = w * rng.uniform(0.72, 0.95)
            if (px, py) not in land or (px, py - h * 0.5) not in land:
                continue
            if (px, py) in blocked or (px, py - h * 0.6) in blocked:
                continue
            if any(abs(px - qx) < (w + qw) * 0.24 and abs(py - qy) < 9 for qx, qy, qw in peaks):
                continue
            peaks.append((px, py, w))
            items.append((py, mountain(px, py, w, h, rng, snow=h > 40)))

    for spine, spread in g["hills"]:
        for x, y, ang in resample(spine, 16):
            for _ in range(2):
                off = rng.gauss(0, spread)
                px = x - math.sin(ang) * off + rng.uniform(-5, 5)
                py = y + math.cos(ang) * off
                if (px, py) in land and (px, py) not in blocked:
                    w = rng.uniform(18, 28)
                    items.append((py, hill(px, py, w, w * 0.32)))

    forest = Mask([g["forest"]] + g["woods"])
    for x, y in scatter(8.5, rng, 0.92):
        if (x, y) in forest and (x, y) in land and (x, y) not in blocked:
            s = rng.uniform(0.85, 1.2)
            t = "tree-a" if rng.random() < 0.55 else "tree-b"
            items.append((y, f'<use href="#{t}" transform="translate({x:.1f} {y:.1f}) '
                             f'scale({s:.2f})"/>'))
    taiga = Mask(g["taiga"])
    for x, y in scatter(9, rng, 0.9):
        if (x, y) in taiga and (x, y) in land and (x, y) not in blocked:
            s = rng.uniform(0.8, 1.15)
            items.append((y, f'<use href="#pine" transform="translate({x:.1f} {y:.1f}) '
                             f'scale({s:.2f})"/>'))

    items.sort(key=lambda it: it[0])
    out = [s for _, s in items]

    # Flat marks never overlap anything tall, so they go underneath, unsorted.
    flat = []
    waste = Mask([g["waste"]])
    for x, y in scatter(34, rng, 0.45):
        if (x, y) in waste and (x, y) in land and (x, y) not in blocked:
            w = rng.uniform(14, 22)
            flat.append(f'<path d="M{x - w / 2:.1f} {y:.1f} Q{x:.1f} {y - 6:.1f} {x + w / 2:.1f} '
                        f'{y:.1f} M{x - w / 3:.1f} {y + 5:.1f} Q{x + 2:.1f} {y + 1:.1f} '
                        f'{x + w / 2.5:.1f} {y + 5:.1f}" fill="none" stroke="#94703f" '
                        f'stroke-width="1.1" opacity="0.8"/>')
    for x, y in scatter(11, rng, 0.3):
        if (x, y) in waste and (x, y) in land and (x, y) not in blocked:
            flat.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{rng.uniform(0.7, 1.3):.1f}" '
                        f'fill="#94703f" opacity="0.55"/>')
    tundra = Mask([g["tundra"]])
    for x, y in scatter(28, rng, 0.35):
        if (x, y) in tundra and (x, y) in land and (x, y) not in blocked:
            flat.append(f'<path d="M{x - 5:.1f} {y:.1f} h10 M{x - 2:.1f} {y + 3:.1f} h6" '
                        f'stroke="#8d9ca0" stroke-width="1" opacity="0.8"/>')
    empire = Mask([g["region"]["empire"]])
    rough = Mask([g["forest"], g["waste"], g["tundra"]] + g["woods"])
    for x, y in scatter(48, rng, 0.3):
        if ((x, y) in empire and (x, y) in land and (x, y) not in blocked
                and (x, y) not in rough):
            flat.append(f'<path d="M{x - 3:.1f} {y:.1f} l-1 -4 M{x:.1f} {y:.1f} v-5 '
                        f'M{x + 3:.1f} {y:.1f} l1 -4" stroke="#7d7a4c" stroke-width="0.9" '
                        f'opacity="0.8"/>')
    return flat + out


def layer_sea_marks(g, rng, blocked):
    shore = Mask([g["coast"]] + g["isles"] + g["islets"], [g["coast"]] + g["isles"], 110)
    out = []
    for x, y in scatter(64, rng, 0.3, (40, 40, W - 40, H - 40)):
        if (x, y) in shore or (x, y) in blocked:
            continue
        out.append(f'<path d="M{x - 9:.1f} {y:.1f} q4.5 -5 9 0 q4.5 -5 9 0" fill="none" '
                   f'stroke="#5d7d83" stroke-width="1.1" opacity="0.5"/>')
    return out


# @@PART5@@


def main():
    rng = random.Random(SEED)
    g = build(rng)
    defs, body = layer_base(g)
    body += layer_washes(g)
    body += layer_factions(g)
    body += layer_water(g)
    body += layer_coast_ink(g)
    body += layer_borders(g)
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
           f'viewBox="0 0 {W} {H}" font-family="{FONT}">'
           f'<defs>{"".join(defs)}</defs>{"".join(body)}</svg>')
    OUT.write_text(svg, encoding="utf-8")

    mainland = Mask([g["coast"]] + g["islets"])
    total = mainland.area()
    for name, poly in g["region"].items():
        m = Mask([poly])
        share = (np.asarray(m.img) > 0) & (np.asarray(mainland.img) > 0)
        print(f"{name:7s} {share.sum() / total:6.1%}")


if __name__ == "__main__":
    main()
