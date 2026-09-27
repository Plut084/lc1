#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成《博宇大陆 · 现世形势图》。

用法::

    python3 docs/map/generate_map.py          # 写出 docs/map/world-map.svg
    magick -density 144 -background none \\
        docs/map/world-map.svg docs/map/world-map.png

只依赖标准库。地图上每一个地理要素都来自下面那几张表，改表就改图；
随机抖动用固定种子，因此同样的输入永远得到同一张图。

世界观到地理的映射（见 docs/content.md 与世界观草案）：

* 帝国占据大陆中部与东部的大河平原 —— 「大部分地区」；
* 类人种（部落）退守四周：北岭、西部森林、南部荒原、东北群岛，
  正好是「森林、山脉、荒原以及群岛等地区」；
* 11战争停战线（灰烬防线）把帝国与南部荒原隔开，是当前争议地带；
* 星尘枯竭之后，神秘种遗迹散布各地，帝国把仅存的星尘收拢到都城研究；
* 组织不入明面，只有几个传闻据点。
"""

from __future__ import annotations

import math
import random
from pathlib import Path

OUT = Path(__file__).with_name("world-map.svg")

W, H = 2200.0, 1500.0
SEED = 112011  # 11战争

# ---------------------------------------------------------------- 颜色

INK = "#3b2b1a"
INK_SOFT = "#6d5940"
PARCHMENT = "#efe2c4"
LAND = "#f5ebd3"
SEA = "#c3d0c9"
EMPIRE = "#e5b969"
TRIBE = "#7d7a4e"
RIVER = "#7c9aa8"
FOREST = "#6d7d4d"
FOREST_DARK = "#536138"
ROCK = "#b9a888"
SNOW = "#f8f3e7"
SAND = "#cdb083"
GOLD = "#d9a53a"
VIOLET = "#6a4a6b"
CONTEST = "#a4553b"
PANEL = "#f4ead2"
HALO = "#f7efdb"

FONT = "'Noto Serif CJK SC','Noto Sans CJK SC',serif"

# ---------------------------------------------------------------- 几何工具


def catmull_rom(points, per_segment=12, closed=True):
    """把控制点变成平滑折线。"""
    n = len(points)
    out = []
    last = n if closed else n - 1
    for i in range(last):
        p0 = points[(i - 1) % n] if closed else points[max(i - 1, 0)]
        p1 = points[i]
        p2 = points[(i + 1) % n] if closed else points[min(i + 1, n - 1)]
        p3 = points[(i + 2) % n] if closed else points[min(i + 2, n - 1)]
        for s in range(per_segment):
            t = s / per_segment
            t2, t3 = t * t, t * t * t
            x = 0.5 * (2 * p1[0] + (-p0[0] + p2[0]) * t
                       + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2
                       + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3)
            y = 0.5 * (2 * p1[1] + (-p0[1] + p2[1]) * t
                       + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2
                       + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3)
            out.append((x, y))
    if not closed:
        out.append(points[-1])
    return out


def value_noise(n, period, rng):
    """一维值噪声，余弦插值，用来做海岸线的自然抖动。"""
    keys = [rng.uniform(-1.0, 1.0) for _ in range(n // period + 3)]
    out = []
    for i in range(n):
        k = i / period
        i0 = int(k)
        f = (1.0 - math.cos((k - i0) * math.pi)) * 0.5
        out.append(keys[i0] * (1.0 - f) + keys[i0 + 1] * f)
    return out


def roughen(points, layers, rng):
    """沿法线做多倍频扰动。layers 是 [(周期, 振幅), ...]。"""
    n = len(points)
    offset = [0.0] * n
    for period, amp in layers:
        noise = value_noise(n, period, rng)
        for i in range(n):
            offset[i] += noise[i] * amp
    out = []
    for i in range(n):
        px, py = points[i - 1]
        qx, qy = points[(i + 1) % n]
        dx, dy = qx - px, qy - py
        length = math.hypot(dx, dy) or 1.0
        out.append((points[i][0] - dy / length * offset[i],
                    points[i][1] + dx / length * offset[i]))
    return out


def d_of(points, closed=True, prec=1):
    head = f"M {points[0][0]:.{prec}f} {points[0][1]:.{prec}f}"
    body = " ".join(f"L {x:.{prec}f} {y:.{prec}f}" for x, y in points[1:])
    return f"{head} {body}" + (" Z" if closed else "")


def region_d(points, samples=8, closed=True):
    """势力范围一律走平滑曲线：填充和边界必须用同一条，否则会对不齐。"""
    return d_of(catmull_rom(points, samples), closed=closed)


def blob(cx, cy, radius, irregularity, rng, vertices=10, squash=0.78):
    base = []
    for i in range(vertices):
        a = 2.0 * math.pi * i / vertices
        r = radius * (1.0 + rng.uniform(-irregularity, irregularity))
        base.append((cx + math.cos(a) * r, cy + math.sin(a) * r * squash))
    return roughen(catmull_rom(base, 8), [(5, radius * 0.08)], rng)


def inside(point, polygon):
    x, y = point
    n = len(polygon)
    hit = False
    j = n - 1
    for i in range(n):
        xi, yi = polygon[i]
        xj, yj = polygon[j]
        if (yi > y) != (yj > y):
            if x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                hit = not hit
        j = i
    return hit


def scatter(rng, polygon, count, bbox, jitter=0.0):
    """在多边形内撒点；jitter 额外放宽 bbox 边缘。"""
    x0, y0, x1, y1 = bbox
    out = []
    guard = 0
    while len(out) < count and guard < count * 200:
        guard += 1
        p = (rng.uniform(x0, x1), rng.uniform(y0, y1))
        if inside(p, polygon):
            if jitter:
                p = (p[0] + rng.uniform(-jitter, jitter),
                     p[1] + rng.uniform(-jitter, jitter))
            out.append(p)
    return out


def bbox_of(polygon):
    xs = [p[0] for p in polygon]
    ys = [p[1] for p in polygon]
    return (min(xs), min(ys), max(xs), max(ys))


def lerp_curve(points, x):
    """按 x 在折线上线性取值，用来沿山脊铺山。"""
    ordered = sorted(points)
    if x <= ordered[0][0]:
        return ordered[0][1]
    if x >= ordered[-1][0]:
        return ordered[-1][1]
    for i in range(len(ordered) - 1):
        x0, y0 = ordered[i]
        x1, y1 = ordered[i + 1]
        if x0 <= x <= x1:
            t = (x - x0) / ((x1 - x0) or 1.0)
            return y0 + (y1 - y0) * t
    return ordered[-1][1]


# ---------------------------------------------------------------- 文字


def text(x, y, s, size, fill=INK, anchor="middle", spacing=None, rotate=None,
         italic=False, weight=None, opacity=None, halo=HALO, halo_width=4.0):
    """带纸色描边的文字：先画描边层，再画填充层（比 paint-order 兼容性好）。"""
    attrs = [f'x="{x:.1f}"', f'y="{y:.1f}"', f'font-family="{FONT}"',
             f'font-size="{size:.1f}"', f'text-anchor="{anchor}"']
    if spacing is not None:
        attrs.append(f'letter-spacing="{spacing:.1f}"')
    if italic:
        attrs.append('font-style="italic"')
    if weight is not None:
        attrs.append(f'font-weight="{weight}"')
    if opacity is not None:
        attrs.append(f'opacity="{opacity}"')
    if rotate is not None:
        attrs.append(f'transform="rotate({rotate:.1f} {x:.1f} {y:.1f})"')
    join = " ".join(attrs)
    out = []
    if halo:
        out.append(f'<text {join} fill="none" stroke="{halo}" '
                   f'stroke-width="{halo_width:.1f}" stroke-linejoin="round" '
                   f'stroke-linecap="round">{s}</text>')
    out.append(f'<text {join} fill="{fill}">{s}</text>')
    return "".join(out)


# ---------------------------------------------------------------- 数据表

# 大陆轮廓（顺时针：北岸自西向东 → 东岸南下 → 南岸向西 → 西岸北上）
LAND_CONTROL = [
    (620, 400), (700, 330), (860, 300), (1010, 322), (1150, 286), (1300, 312),
    (1430, 382), (1520, 452), (1466, 540), (1544, 640), (1580, 762),
    (1548, 862), (1452, 902), (1424, 1010), (1512, 1082), (1570, 1182),
    (1470, 1252), (1330, 1268), (1200, 1352), (1075, 1300), (985, 1235),
    (900, 1145), (800, 1058), (700, 1000), (570, 962), (470, 882), (430, 762),
    (500, 662), (452, 562), (560, 482), (600, 432),
]

# 碎星群岛：地图边缘，东北方向的岛链
ISLANDS = [
    (1720, 700, 44), (1800, 620, 30), (1880, 540, 54), (1970, 452, 34),
    (2060, 380, 26), (1700, 402, 24), (1780, 350, 20), (1900, 706, 22),
    (2020, 600, 30), (2100, 502, 22), (1656, 562, 18),
    (1130, 1400, 16), (980, 1382, 12),  # 南岸外侧小岛，纯装饰
]

# 铁脊山脉：东西横贯北境，帝国的天然北界
CREST = [(660, 424), (800, 392), (950, 384), (1100, 390), (1240, 406),
         (1360, 428), (1460, 460)]

# 势力的范围。都会被 landclip 裁进陆地，所以靠海的一侧可以画得随便一点。
EMPIRE_REGION = [
    (620, 424), (800, 392), (950, 384), (1100, 390), (1240, 406), (1360, 428),
    (1460, 460), (1520, 520), (1560, 650), (1560, 870), (1470, 900),
    (1424, 1010), (1400, 1078), (1290, 998), (1150, 1036), (960, 1036),
    (790, 986), (668, 908), (616, 760), (632, 560),
]
TRIBE_NORTH = [
    (560, 250), (1620, 250), (1620, 540), (1460, 460), (1360, 428),
    (1240, 406), (1100, 390), (950, 384), (800, 392), (660, 424), (600, 440),
]
TRIBE_FOREST = [
    (700, 420), (655, 520), (610, 615), (642, 705), (600, 795), (655, 880),
    (668, 908), (330, 1010), (330, 520), (420, 360),
]
TRIBE_DESERT = [
    (668, 908), (790, 986), (960, 1036), (1150, 1036), (1290, 998),
    (1400, 1078), (1520, 1230), (1520, 1440), (700, 1440),
]

# 11战争停战线：帝国与南部荒原之间的旧战线，现在的争议地带
ASH_LINE = [(668, 908), (790, 986), (960, 1036), (1150, 1036), (1290, 998),
            (1400, 1078)]

RIVERS = {
    "沧河": [(1002, 452), (1030, 528), (1040, 620), (1010, 700), (972, 756),
             (1004, 820), (1032, 874)],
    "沧河下游": [(1075, 898), (1132, 962), (1206, 1032), (1300, 1106),
                 (1400, 1162), (1494, 1206)],
    "青溪": [(470, 700), (560, 748), (662, 792), (764, 812), (880, 800),
             (972, 756)],
    "铁水": [(1136, 392), (1200, 432), (1246, 482), (1200, 546), (1120, 586),
             (1044, 616)],
}
LAKE = (1032, 880, 62)

# name, x, y, kind, 标签方位
CITIES = [
    ("曜京", 1040, 620, "capital", "br"),
    ("铁炉城", 1246, 482, "city", "r"),
    ("河梁城", 972, 756, "city", "bl"),
    ("浦海港", 1452, 1140, "port", "r"),
    ("望北关", 1178, 402, "fort", "r"),
    ("青溪镇", 700, 806, "town", "bl"),
    ("赤垒", 1292, 996, "fort", "r"),
]
ROADS = [
    [(1040, 620), (1088, 560), (1180, 512), (1246, 482)],
    # 绕星陨湖东岸走，别从湖里穿过去
    [(1040, 620), (1000, 690), (972, 756), (1052, 798), (1160, 862),
     (1240, 962), (1312, 1060), (1452, 1140)],
    [(1040, 620), (1092, 540), (1140, 462), (1178, 402)],
    [(1040, 620), (938, 662), (830, 722), (700, 806)],
]

# 神秘种遗迹：True 表示仍有星尘残留
MYSTERY = [
    ("观星峰·古观星台", 1042, 372, True, "t"),
    ("星陨湖·湖底遗迹", 1032, 880, True, "bl"),
    ("沉语林·古神祭坛", 508, 792, False, "b"),
    ("碎星群岛·坠落之环", 1880, 540, True, "b"),
    ("赤砂荒原·风蚀台地", 1188, 1218, False, "r"),
]
STARDUST_FACILITY = ("帝国星尘院", 1122, 556)
WAR_RUINS = [(700, 942), (800, 1000), (900, 1030), (1010, 1042), (1120, 1042),
             (1230, 1022), (1330, 1008), (1402, 1080)]
ORG_SITES = [(1158, 690), (900, 452), (1800, 620)]

SEA_LABELS = [
    ("北寂海", 950, 178, 0, 34, 12),
    ("星落洋", 300, 812, -72, 34, 12),
    ("月牙湾", 1622, 1024, 0, 30, 6),
    ("碎星海", 1618, 690, -78, 30, 8),
]

NOTES = [
    "11战争停战后，大陆表面归于稳定。",
    "帝国据中部与东部，以人类为首，控扼大河与山口。",
    "类人种退守北岭、森林、荒原与碎星群岛，阶级已成。",
    "星尘枯竭：神秘种凋零，法术由常见变为稀有而危险。",
    "帝国转以机械、炼金、冶炼立国，星尘只用于军事与研究。",
    "战争遗迹、失落群落与残余星尘散布各地，三方皆在搜寻。",
]

LEGEND_REGIONS = [
    ("帝国疆域", "empire"),
    ("部落势力范围", "tribe"),
    ("争议地带（11战争旧战线）", "contest"),
]
LEGEND_TERRAIN = [("山脉", "peak"), ("森林", "tree"), ("荒原", "dune"),
                  ("河流 · 湖泊", "river")]
LEGEND_MARKS = [
    ("都城", "capital"), ("城市", "city"), ("关隘 · 城镇", "fort"),
    ("驿道", "road"), ("神秘种遗迹（星尘已枯）", "ruin_star"),
    ("神秘种遗迹（星尘残留）", "ruin_star_live"), ("战争遗迹", "ruin_wall"),
    ("帝国星尘院", "facility"), ("组织据点（传闻）", "org"),
]


# ---------------------------------------------------------------- 符号库


def defs():
    """所有可复用的图形符号。形状不带 fill，由 <use> 上的 fill 继承。"""
    shapes = []

    # 针叶树 / 阔叶树
    for i, (w, h, tiers) in enumerate([(6.0, 22.0, 3), (7.0, 18.0, 3),
                                       (5.0, 25.0, 4), (8.0, 20.0, 2)]):
        parts = [f"M 0 0 L 0 {-h * 0.26:.1f}"]
        for k in range(tiers):
            ly = -h * (0.22 + 0.24 * k)
            lw = w * (1.0 - 0.22 * k)
            parts.append(f"M {-lw:.1f} {ly:.1f} L 0 {ly - h * 0.46:.1f} "
                         f"L {lw:.1f} {ly:.1f} Z")
        shapes.append(f'<path id="tree{i}" d="{" ".join(parts)}"/>')
    for i, (r, h) in enumerate([(6.5, 16.0), (5.0, 20.0), (8.0, 14.0)]):
        shapes.append(
            f'<path id="tree{i + 4}" d="M 0 0 L 0 {-h * 0.4:.1f} '
            f'M {-r:.1f} {-h * 0.68:.1f} '
            f'a {r:.1f} {r * 0.92:.1f} 0 1 0 {r * 2:.1f} 0 '
            f'a {r:.1f} {r * 0.92:.1f} 0 1 0 {-r * 2:.1f} 0 Z"/>')

    # 山峰：左坡 + 右坡 + 右侧阴影 + 雪顶
    for i in range(6):
        rng = random.Random(SEED + 900 + i)
        w = rng.uniform(13.0, 17.0)
        h = rng.uniform(20.0, 26.0)
        tx = rng.uniform(-2.0, 2.0)
        shade = (f"M {tx:.1f} {-h:.1f} L {w:.1f} 0 L {w * 0.12:.1f} 0 Z")
        f = 0.34
        sl = (tx + (-w - tx) * f, -h + h * f)
        sr = (tx + (w - tx) * f, -h + h * f)
        cap = (f"M {tx:.1f} {-h:.1f} L {sl[0]:.1f} {sl[1]:.1f} "
               f"L {tx * 0.5:.1f} {sl[1] + 3.0:.1f} "
               f"L {sr[0]:.1f} {sr[1]:.1f} Z")
        shapes.append(
            f'<g id="peak{i}"><path d="M {-w:.1f} 0 L {tx:.1f} {-h:.1f} '
            f'L {w:.1f} 0" fill="{ROCK}" stroke="{INK}" stroke-width="1.3" '
            f'stroke-linejoin="round"/>'
            f'<path d="{shade}" fill="{INK}" opacity="0.20"/>'
            f'<path d="{cap}" fill="{SNOW}" opacity="0.9"/></g>')

    # 沙丘
    for i, (w, h) in enumerate([(11.0, 6.0), (14.0, 7.5), (8.0, 5.0)]):
        shapes.append(
            f'<g id="dune{i}" fill="none" stroke="{SAND}" '
            f'stroke-width="1.6" stroke-linecap="round">'
            f'<path d="M {-w:.1f} 0 Q 0 {-h:.1f} {w:.1f} 0"/>'
            f'<path d="M {-w * 0.45:.1f} 0 Q 0 {-h * 0.55:.1f} '
            f'{w * 0.45:.1f} 0" stroke="{INK_SOFT}" opacity="0.45"/></g>')

    # 风蚀台地
    shapes.append(
        f'<path id="mesa" d="M -17 0 L -11 -14 L 9 -14 L 16 0 Z" fill="{SAND}" '
        f'stroke="{INK_SOFT}" stroke-width="1.3" stroke-linejoin="round"/>')

    # 田畴：帝国腹地沿河的耕作痕迹
    shapes.append(
        f'<g id="field">'
        f'<path d="M 0 0 L 18 -4 L 18 8 L 0 12 Z" fill="{SAND}" '
        f'fill-opacity="0.32" stroke="{INK_SOFT}" stroke-width="1" '
        f'stroke-opacity="0.65"/>'
        f'<path d="M 4.5 2 L 4.5 10 M 9 1 L 9 9 M 13.5 0 L 13.5 8" '
        f'stroke="{INK_SOFT}" stroke-width="1.1" stroke-opacity="0.55" '
        f'fill="none"/></g>')

    # 四角星：神秘种的记号
    star = []
    for i in range(8):
        a = -math.pi / 2 + i * math.pi / 4
        r = 10.0 if i % 2 == 0 else 3.2
        star.append((math.cos(a) * r, math.sin(a) * r))
    star_d = d_of(star)
    shapes.append(f'<path id="star" d="{star_d}"/>')

    # 战争遗迹：带垛口的断墙
    shapes.append(
        '<g id="ruin_wall" fill="none" stroke="' + INK + '" stroke-width="1.5" '
        'stroke-linejoin="round">'
        '<path d="M -11 0 L -11 -8 L -7 -8 L -7 -5 L -3.5 -5 L -3.5 -8 L 1.5 -8 '
        'L 1.5 -4.5 L 5.5 -4.5 L 5.5 -8 L 9 -8 L 9 -2.5 L 11 -2.5 L 11 0"/>'
        '<path d="M -8 0 L -8 3.5 M 6 0 L 6 3" opacity="0.6"/></g>')

    # 城市记号
    shapes.append(
        f'<g id="capital"><circle r="12" fill="none" stroke="{INK}" '
        f'stroke-width="2"/><circle r="5.4" fill="{INK}"/>'
        f'<path d="M 0 -12 L 0 -17 M 0 12 L 0 17 M -12 0 L -17 0 M 12 0 L 17 0" '
        f'stroke="{INK}" stroke-width="2"/></g>')
    shapes.append(
        f'<g id="city"><circle r="7.5" fill="none" stroke="{INK}" '
        f'stroke-width="1.8"/><circle r="3.2" fill="{INK}"/></g>')
    shapes.append(
        f'<g id="town"><circle r="4.2" fill="{INK}"/></g>')
    shapes.append(
        f'<g id="fort" fill="none" stroke="{INK}" stroke-width="1.8" '
        f'stroke-linejoin="round"><path d="M -8 4 L -8 -5 L -5 -5 L -5 -8 '
        f'L -2 -8 L -2 -5 L 2 -5 L 2 -8 L 5 -8 L 5 -5 L 8 -5 L 8 4 Z"/></g>')
    shapes.append(
        f'<g id="port"><circle r="7.5" fill="none" stroke="{INK}" '
        f'stroke-width="1.8"/><circle r="3.2" fill="{INK}"/>'
        f'<path d="M 0 9 L 0 20 M -4 12 L 4 12 M -5 15 Q 0 21 5 15" fill="none" '
        f'stroke="{INK}" stroke-width="1.6" stroke-linecap="round"/></g>')

    # 帝国星尘院：星尘被收进一个圈里
    shapes.append(
        f'<g id="facility"><circle r="11" fill="{PANEL}" stroke="{GOLD}" '
        f'stroke-width="2"/><use xlink:href="#star" fill="{GOLD}" '
        f'transform="scale(0.72)"/></g>')

    # 组织的眼睛
    shapes.append(
        f'<g id="org" fill="none" stroke="{VIOLET}" stroke-width="1.7">'
        f'<path d="M -12 0 Q 0 -11 12 0 Q 0 11 -12 0 Z"/>'
        f'<circle r="3.4" fill="{VIOLET}" stroke="none"/></g>')

    patterns = f'''
    <pattern id="hatch" width="19" height="19" patternUnits="userSpaceOnUse"
             patternTransform="rotate(35)">
      <line x1="0" y1="0" x2="0" y2="19" stroke="{TRIBE}" stroke-width="2.6"
            opacity="0.4"/>
    </pattern>
    <radialGradient id="vignette" cx="50%" cy="48%" r="72%">
      <stop offset="55%" stop-color="{INK}" stop-opacity="0"/>
      <stop offset="100%" stop-color="{INK}" stop-opacity="0.26"/>
    </radialGradient>
    <radialGradient id="glow">
      <stop offset="0%" stop-color="{GOLD}" stop-opacity="0.55"/>
      <stop offset="100%" stop-color="{GOLD}" stop-opacity="0"/>
    </radialGradient>'''

    return '<defs>' + "".join(shapes) + patterns + '</defs>'


# ---------------------------------------------------------------- 组装


def build():
    rng = random.Random(SEED)

    land = roughen(catmull_rom(LAND_CONTROL, 9),
                   [(41, 9.0), (17, 4.5), (7, 2.0)], rng)
    land_d = d_of(land)
    island_paths = [blob(x, y, r, 0.34, rng) for x, y, r in ISLANDS]
    island_d = " ".join(d_of(p) for p in island_paths)
    all_land_d = land_d + " " + island_d

    svg = []
    add = svg.append

    add(f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{W:.0f}" height="{H:.0f}" viewBox="0 0 {W:.0f} {H:.0f}">')

    # ---- 符号与裁剪
    add(defs())
    add(f'<clipPath id="landclip"><path d="{all_land_d}"/></clipPath>')

    # ---- 纸与海
    add(f'<rect width="{W:.0f}" height="{H:.0f}" fill="{PARCHMENT}"/>')
    add(f'<rect width="{W:.0f}" height="{H:.0f}" fill="{SEA}" opacity="0.5"/>')

    grain = []
    for _ in range(2600):
        x, y = rng.uniform(0, W), rng.uniform(0, H)
        r = rng.uniform(0.5, 2.0)
        c = INK_SOFT if rng.random() < 0.7 else "#ffffff"
        grain.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" r="{r:.1f}" '
                     f'fill="{c}" opacity="{rng.uniform(0.03, 0.09):.3f}"/>')
    add("<g>" + "".join(grain) + "</g>")

    # ---- 海岸光晕 + 陆地
    for width, op in ((30, 0.10), (16, 0.16), (8, 0.26)):
        add(f'<path d="{all_land_d}" fill="none" stroke="{INK}" '
            f'stroke-width="{width}" opacity="{op}" stroke-linejoin="round"/>')
    add(f'<path d="{all_land_d}" fill="{LAND}"/>')
    add(f'<path d="{all_land_d}" fill="none" stroke="{INK}" stroke-width="2.2" '
        f'stroke-linejoin="round"/>')

    # ---- 势力范围（裁进陆地）
    empire_d = region_d(EMPIRE_REGION)
    add(f'<g clip-path="url(#landclip)">')
    add(f'<path d="{empire_d}" fill="{EMPIRE}" opacity="0.32"/>')
    for region in (TRIBE_NORTH, TRIBE_FOREST, TRIBE_DESERT):
        rd = region_d(region)
        add(f'<path d="{rd}" fill="{TRIBE}" opacity="0.09"/>')
        add(f'<path d="{rd}" fill="url(#hatch)"/>')
    add(f'<path d="{island_d}" fill="{TRIBE}" opacity="0.09"/>')
    add(f'<path d="{island_d}" fill="url(#hatch)"/>')
    add('</g>')

    # ---- 河与湖（在陆地裁剪内，压在地形符号下面）
    add('<g clip-path="url(#landclip)">')
    for name, pts in RIVERS.items():
        smooth = catmull_rom(pts, 10, closed=False)
        width = 5.6 if name == "沧河" else 3.0
        add(f'<path d="{d_of(smooth, closed=False)}" fill="none" '
            f'stroke="{RIVER}" stroke-width="{width + 2.6:.1f}" opacity="0.35" '
            f'stroke-linecap="round"/>')
        add(f'<path d="{d_of(smooth, closed=False)}" fill="none" '
            f'stroke="{RIVER}" stroke-width="{width:.1f}" stroke-linecap="round"/>')

    lx, ly, lr = LAKE
    lake = blob(lx, ly, lr, 0.16, rng, vertices=12)
    shore = blob(lx, ly, lr * 1.62, 0.22, rng, vertices=14)
    add(f'<path d="{d_of(shore)}" fill="{SAND}" opacity="0.22"/>')
    add(f'<path d="{d_of(shore)}" fill="none" stroke="{GOLD}" '
        f'stroke-width="1.4" stroke-dasharray="7 6" opacity="0.6"/>')
    add(f'<path d="{d_of(lake)}" fill="{RIVER}" stroke="{INK}" '
        f'stroke-width="1.6"/>')
    cracks = []
    for _ in range(90):
        a = rng.uniform(0, 2 * math.pi)
        d = rng.uniform(lr * 1.05, lr * 1.6)
        x, y = lx + math.cos(a) * d, ly + math.sin(a) * d * 0.78
        s = rng.uniform(3.0, 8.0)
        cracks.append(f'<path d="M {x - s:.1f} {y:.1f} L {x + s:.1f} '
                      f'{y + rng.uniform(-3, 3):.1f}" stroke="{INK_SOFT}" '
                      f'stroke-width="1" opacity="0.45"/>')
    add("".join(cracks))
    add('</g>')

    # ---- 森林
    trees = []
    forest_poly = catmull_rom(TRIBE_FOREST, 6)
    empire_poly = catmull_rom(EMPIRE_REGION, 6)
    for x, y in scatter(rng, forest_poly, 900, bbox_of(forest_poly)):
        dark = rng.random() < 0.5
        add_tree(trees, x, y, rng.uniform(0.6, 1.1),
                 FOREST_DARK if dark else FOREST, rng, rng.uniform(0.72, 0.95))
    # 北岭林带
    north_poly = catmull_rom(TRIBE_NORTH, 6)
    for x, y in scatter(rng, north_poly, 95, (600, 250, 1320, 440)):
        add_tree(trees, x, y, rng.uniform(0.45, 0.8), FOREST, rng, 0.6)
    # 帝国境内的疏林：中部不该是一片空色
    placed = 0
    while placed < 200:
        x, y = rng.uniform(660, 1440), rng.uniform(450, 990)
        if inside((x, y), empire_poly) and not inside((x, y), forest_poly):
            add_tree(trees, x, y, rng.uniform(0.38, 0.7), FOREST, rng,
                     rng.uniform(0.3, 0.55))
            placed += 1
    # 大岛上的几棵树
    for ix, iy, ir in ISLANDS[:5]:
        for _ in range(int(ir / 9)):
            a, d = rng.uniform(0, 2 * math.pi), rng.uniform(0, ir * 0.6)
            add_tree(trees, ix + math.cos(a) * d, iy + math.sin(a) * d * 0.78,
                     rng.uniform(0.4, 0.7), FOREST_DARK, rng, 0.6)
    add(f'<g clip-path="url(#landclip)">{"".join(trees)}</g>')

    # ---- 荒原
    dunes = []
    desert_poly = catmull_rom(TRIBE_DESERT, 6)
    for x, y in scatter(rng, desert_poly, 190, bbox_of(desert_poly)):
        k = rng.randrange(3)
        s = rng.uniform(1.0, 2.0)
        rot = rng.uniform(-16, 16)
        dunes.append(f'<use xlink:href="#dune{k}" transform="translate({x:.0f},'
                     f'{y:.0f}) rotate({rot:.0f}) scale({s:.2f})"/>')
    for x, y in scatter(rng, desert_poly, 14, bbox_of(desert_poly)):
        dunes.append(f'<use xlink:href="#mesa" transform="translate({x:.0f},'
                     f'{y:.0f}) scale({rng.uniform(0.8, 1.4):.2f})"/>')
    stipple = []
    for _ in range(2200):
        x = rng.uniform(700, 1530)
        y = rng.uniform(970, 1420)
        if inside((x, y), desert_poly):
            stipple.append(f'<circle cx="{x:.0f}" cy="{y:.0f}" '
                           f'r="{rng.uniform(0.7, 1.8):.1f}" fill="{INK_SOFT}" '
                           f'opacity="{rng.uniform(0.10, 0.28):.3f}"/>')
    add('<g clip-path="url(#landclip)">' + "".join(dunes) + "".join(stipple) + '</g>')

    # ---- 铁脊山脉
    peaks = []
    for row, (dy, smin, smax, step) in enumerate([(-46, 0.65, 0.92, 40),
                                                  (-12, 0.95, 1.35, 48),
                                                  (26, 1.2, 1.8, 56)]):
        x = 620 + row * 34
        while x < 1490:
            base = lerp_curve(CREST, x)
            y = base + dy + rng.uniform(-15, 15)
            peaks.append((x + rng.uniform(-16, 16), y, rng.uniform(smin, smax)))
            x += step * rng.uniform(0.78, 1.28)
    peaks.append((1042, 352, 1.95))          # 观星峰
    peaks.append((1008, 372, 1.45))
    peaks.append((1076, 368, 1.35))
    peaks.append((600, 432, 1.0))
    peaks.append((1508, 474, 0.95))
    # 山麓与帝国东部的零星丘陵
    placed = 0
    while placed < 30:
        x, y = rng.uniform(700, 1440), rng.uniform(452, 556)
        if inside((x, y), empire_poly):
            peaks.append((x, y, rng.uniform(0.36, 0.56)))
            placed += 1
    placed = 0
    while placed < 10:
        x, y = rng.uniform(1320, 1520), rng.uniform(600, 900)
        if inside((x, y), empire_poly):
            peaks.append((x, y, rng.uniform(0.4, 0.6)))
            placed += 1
    # 大岛上的小山
    for ix, iy, ir in ISLANDS[:3]:
        peaks.append((ix, iy - ir * 0.2, rng.uniform(0.45, 0.7)))
    peaks.sort(key=lambda p: p[1])
    glyphs = []
    for x, y, s in peaks:
        k = rng.randrange(6)
        glyphs.append(f'<use xlink:href="#peak{k}" transform="translate({x:.0f},'
                      f'{y:.0f}) scale({s:.2f})"/>')
    add('<g clip-path="url(#landclip)">' + "".join(glyphs) + '</g>')

    # ---- 田畴：帝国腹地沿沧河的耕作带
    river_pts = catmull_rom(RIVERS["沧河"], 8, closed=False)
    fields = []
    tries = 0
    while len(fields) < 30 and tries < 6000:
        tries += 1
        x, y = rng.uniform(780, 1330), rng.uniform(510, 860)
        if not inside((x, y), empire_poly):
            continue
        if min(math.dist((x, y), p) for p in river_pts) > 155:
            continue
        fields.append((x, y, rng.uniform(-26, 26), rng.uniform(0.8, 1.3)))
    glyphs = [f'<use xlink:href="#field" transform="translate({x:.0f},{y:.0f}) '
              f'rotate({rot:.0f}) scale({s:.2f})"/>'
              for x, y, rot, s in fields]
    add('<g clip-path="url(#landclip)">' + "".join(glyphs) + '</g>')

    # ---- 驿道
    roads = []
    for pts in ROADS:
        smooth = catmull_rom(pts, 10, closed=False)
        d = d_of(smooth, closed=False)
        roads.append(f'<path d="{d}" fill="none" stroke="{HALO}" '
                     f'stroke-width="4.4" stroke-linecap="round"/>')
        roads.append(f'<path d="{d}" fill="none" stroke="{INK_SOFT}" '
                     f'stroke-width="2.2" stroke-dasharray="11 7" '
                     f'stroke-linecap="round"/>')
    add('<g clip-path="url(#landclip)">' + "".join(roads) + '</g>')

    # ---- 帝国边界与争议地带
    add(f'<g clip-path="url(#landclip)">')
    add(f'<path d="{empire_d}" fill="none" stroke="{INK}" '
        f'stroke-width="3.4" stroke-linejoin="round" opacity="0.85"/>')
    ash = catmull_rom(ASH_LINE, 12, closed=False)
    add(f'<path d="{d_of(ash, closed=False)}" fill="none" stroke="{HALO}" '
        f'stroke-width="9" stroke-linecap="round"/>')
    add(f'<path d="{d_of(ash, closed=False)}" fill="none" stroke="{CONTEST}" '
        f'stroke-width="4.2" stroke-dasharray="16 9" stroke-linecap="round"/>')
    add('</g>')

    # ---- 遗迹、城市、据点
    for x, y in WAR_RUINS:
        add(f'<use xlink:href="#ruin_wall" transform="translate({x},{y}) '
            f'scale({rng.uniform(1.1, 1.4):.2f})" opacity="0.9"/>')
    for name, x, y, live, side in MYSTERY:
        if live:
            add(f'<circle cx="{x}" cy="{y}" r="52" fill="url(#glow)"/>')
            add(f'<circle cx="{x}" cy="{y}" r="23" fill="{PANEL}" '
                f'opacity="0.55"/>')
            add(f'<use xlink:href="#star" transform="translate({x},{y}) '
                f'scale(1.35)" fill="{GOLD}" stroke="{INK}" stroke-width="1.4"/>')
        else:
            add(f'<circle cx="{x}" cy="{y}" r="20" fill="{PANEL}" '
                f'opacity="0.5"/>')
            add(f'<use xlink:href="#star" transform="translate({x},{y}) '
                f'scale(1.2)" fill="{SNOW}" stroke="{INK}" stroke-width="1.5" '
                f'opacity="0.95"/>')
    fx, fy = STARDUST_FACILITY[1], STARDUST_FACILITY[2]
    add(f'<use xlink:href="#facility" transform="translate({fx},{fy})"/>')

    for x, y in ORG_SITES:
        add(f'<circle cx="{x}" cy="{y}" r="19" fill="{PANEL}" opacity="0.55"/>')
        add(f'<use xlink:href="#org" transform="translate({x},{y})" '
            f'opacity="0.8"/>')

    marker_scale = {"capital": 1.18, "city": 1.12, "port": 1.1, "fort": 1.05,
                    "town": 1.2}
    labels = []
    for name, x, y, kind, side in CITIES:
        add(f'<use xlink:href="#{kind}" transform="translate({x},{y}) '
            f'scale({marker_scale[kind]})"/>')
        dx, dy, anchor = {"r": (18, 6, "start"), "bl": (-16, 18, "end"),
                          "br": (16, 22, "start"), "t": (0, -22, "middle"),
                          "b": (0, 26, "middle")}[side]
        size = 26 if kind == "capital" else 22
        weight = "700" if kind == "capital" else None
        labels.append(text(x + dx, y + dy, name, size, anchor=anchor,
                           weight=weight, spacing=1.5))

    # ---- 地名
    region_labels = [
        ("曜帝国", 1180, 706, -7, 46, 20, INK, 0.9),
        ("铁脊山脉", 790, 328, 0, 34, 22, INK, 0.9),
        ("沉语森林", 520, 700, -68, 34, 14, INK, 0.85),
        ("赤砂荒原", 1120, 1180, 4, 38, 22, INK, 0.85),
        ("北岭部落", 1300, 344, 0, 26, 10, INK, 0.8),
        ("碎星部族", 1885, 645, 0, 26, 8, INK, 0.8),
        ("灰烬防线", 900, 962, 6, 26, 8, CONTEST, 0.95),
        ("星尘滩", 1158, 836, 0, 20, 4, INK_SOFT, 0.9),
    ]
    for s, x, y, rot, size, spacing, fill, op in region_labels:
        labels.append(text(x, y, s, size, fill=fill, rotate=rot,
                           spacing=spacing, opacity=op, halo_width=5.0))
    labels.append(text(1032, 880, "星陨湖", 20, fill="#2f4a55", halo_width=4.5))
    labels.append(text(STARDUST_FACILITY[1] + 20, STARDUST_FACILITY[2] + 6,
                       STARDUST_FACILITY[0], 18, fill=INK_SOFT,
                       anchor="start", halo_width=4.5))
    for name, x, y, live, side in MYSTERY:
        dx, dy, anchor = {"r": (20, 4, "start"), "t": (0, -26, "middle"),
                          "b": (0, 32, "middle"), "bl": (-18, 10, "end")}[side]
        labels.append(text(x + dx, y + dy, name, 19, fill=INK_SOFT,
                           anchor=anchor, halo_width=4.5))
    for name, x, y, rot, size, spacing in SEA_LABELS:
        labels.append(text(x, y, name, size, fill="#4a6b76", rotate=rot,
                           spacing=spacing, italic=True, halo_width=5.0,
                           opacity=0.9))
    add("".join(labels))

    # ---- 海面波纹
    waves = []
    for _ in range(400):
        x, y = rng.uniform(60, W - 60), rng.uniform(60, H - 60)
        if inside((x, y), catmull_rom(LAND_CONTROL, 4)):
            continue
        if any(inside((x, y), p) for p in island_paths):
            continue
        if 60 < x < 760 and 60 < y < 280:
            continue
        if 1650 < x < 2170 and 750 < y < 1490:
            continue
        if 70 < x < 680 and 1030 < y < 1400:
            continue
        if 1890 < x and y < 330:
            continue
        w = rng.uniform(9, 18)
        waves.append(f'<path d="M {x - w:.0f} {y:.0f} q {w / 2:.0f} -5 '
                     f'{w:.0f} 0" fill="none" stroke="{INK_SOFT}" '
                     f'stroke-width="1.3" opacity="{rng.uniform(0.10, 0.26):.2f}"/>')
        if len(waves) >= 46:
            break
    add("".join(waves))

    # ---- 面板
    add(panel(90, 70, 620, 196))
    add(text(400, 148, "博宇大陆", 62, spacing=16, weight="700"))
    add(text(400, 196, "现世形势图", 34, spacing=12, fill=INK_SOFT))
    add(f'<path d="M 250 216 L 550 216" stroke="{INK_SOFT}" stroke-width="1"/>')
    add(text(400, 242, "星尘枯竭之世 · 11战争停战之后", 20, fill=INK_SOFT,
             spacing=2))
    add(f'<use xlink:href="#star" transform="translate(140,168) scale(0.8)" '
        f'fill="{GOLD}" stroke="{INK}" stroke-width="1.4"/>')
    add(f'<use xlink:href="#star" transform="translate(660,168) scale(0.8)" '
        f'fill="{GOLD}" stroke="{INK}" stroke-width="1.4"/>')

    add(panel(90, 1040, 570, 340))
    add(text(120, 1090, "世 情 提 要", 26, anchor="start", weight="700",
             spacing=3))
    add(f'<path d="M 118 1108 L 632 1108" stroke="{INK_SOFT}" stroke-width="1"/>')
    for i, line in enumerate(NOTES):
        y = 1146 + i * 38
        add(f'<circle cx="128" cy="{y - 6}" r="3" fill="{INK_SOFT}"/>')
        add(text(146, y, line, 21, anchor="start", halo_width=3.5))

    legend = []
    y = 866.0
    for head, items in (("势力", LEGEND_REGIONS), ("地貌", LEGEND_TERRAIN),
                        ("标记", LEGEND_MARKS)):
        legend.append(("head", head, y))
        y += 28
        for name, kind in items:
            legend.append((kind, name, y))
            y += 31
    add(panel(1660, 760, 500, y - 760 + 30))
    add(text(1692, 810, "图 例", 26, anchor="start", weight="700", spacing=3))
    add(f'<path d="M 1690 828 L 2130 828" stroke="{INK_SOFT}" stroke-width="1"/>')
    for kind, name, ry in legend:
        if kind == "head":
            add(text(1692, ry, name, 20, anchor="start", fill=INK_SOFT,
                     weight="700", spacing=4))
            add(f'<path d="M 1742 {ry - 6} L 2130 {ry - 6}" stroke="{INK_SOFT}" '
                f'stroke-width="0.8" opacity="0.5"/>')
            continue
        add(legend_symbol(kind, 1716, ry - 9))
        add(text(1758, ry, name, 21, anchor="start", halo_width=3.5))

    add(compass(2030, 200, 112))
    add(scale_bar(780, 1436, 4, 86))
    add(f'<rect width="{W:.0f}" height="{H:.0f}" fill="url(#vignette)"/>')
    add('</svg>')

    return "".join(svg)


def add_tree(out, x, y, scale, color, rng, opacity):
    k = rng.randrange(7)
    out.append(f'<use xlink:href="#tree{k}" transform="translate({x:.0f},'
               f'{y:.0f}) scale({scale:.2f})" fill="{color}" '
               f'opacity="{opacity:.2f}"/>')


def panel(x, y, w, h):
    return (f'<g><rect x="{x + 7}" y="{y + 8}" width="{w}" height="{h}" rx="7" '
            f'fill="{INK}" opacity="0.16"/>'
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="6" '
            f'fill="{PANEL}" stroke="{INK}" stroke-width="2.2"/>'
            f'<rect x="{x + 8}" y="{y + 8}" width="{w - 16}" height="{h - 16}" '
            f'rx="3" fill="none" stroke="{INK_SOFT}" stroke-width="1" '
            f'opacity="0.8"/></g>')


def legend_symbol(kind, x, y):
    if kind == "empire":
        return (f'<rect x="{x - 22}" y="{y - 12}" width="44" height="24" '
                f'rx="3" fill="{EMPIRE}" opacity="0.32" stroke="{INK}" '
                f'stroke-width="1.6"/>')
    if kind == "tribe":
        return (f'<rect x="{x - 22}" y="{y - 12}" width="44" height="24" '
                f'rx="3" fill="{TRIBE}" opacity="0.13" stroke="{INK}" '
                f'stroke-width="1.2" stroke-dasharray="6 4"/>'
                f'<rect x="{x - 22}" y="{y - 12}" width="44" height="24" '
                f'rx="3" fill="url(#hatch)"/>')
    if kind == "contest":
        return (f'<path d="M {x - 24} {y} L {x + 24} {y}" stroke="{CONTEST}" '
                f'stroke-width="4.2" stroke-dasharray="12 7"/>')
    if kind == "river":
        return (f'<path d="M {x - 24} {y} q 12 -12 24 0 q 12 12 24 0" '
                f'fill="none" stroke="{RIVER}" stroke-width="3.4"/>')
    if kind == "road":
        return (f'<path d="M {x - 24} {y} L {x + 24} {y}" fill="none" '
                f'stroke="{INK_SOFT}" stroke-width="2.4" '
                f'stroke-dasharray="9 6"/>')
    if kind == "peak":
        return f'<use xlink:href="#peak2" transform="translate({x},{y + 12})"/>'
    if kind == "tree":
        return (f'<use xlink:href="#tree1" transform="translate({x},{y + 13}) '
                f'scale(0.62)" fill="{FOREST}"/>'
                f'<use xlink:href="#tree4" transform="translate({x + 13},'
                f'{y + 13}) scale(0.7)" fill="{FOREST_DARK}"/>')
    if kind == "dune":
        return f'<use xlink:href="#dune1" transform="translate({x},{y + 5})"/>'
    if kind == "capital":
        return f'<use xlink:href="#capital" transform="translate({x},{y})"/>'
    if kind == "city":
        return f'<use xlink:href="#city" transform="translate({x},{y})"/>'
    if kind == "fort":
        return (f'<use xlink:href="#fort" transform="translate({x - 14},{y})"/>'
                f'<use xlink:href="#town" transform="translate({x + 14},{y})"/>')
    if kind == "ruin_star":
        return (f'<use xlink:href="#star" transform="translate({x},{y})" '
                f'fill="{SNOW}" stroke="{INK}" stroke-width="1.5"/>')
    if kind == "ruin_star_live":
        return (f'<circle cx="{x}" cy="{y}" r="20" fill="url(#glow)"/>'
                f'<use xlink:href="#star" transform="translate({x},{y})" '
                f'fill="{GOLD}" stroke="{INK}" stroke-width="1.4"/>')
    if kind == "ruin_wall":
        return f'<use xlink:href="#ruin_wall" transform="translate({x},{y + 4})"/>'
    if kind == "facility":
        return f'<use xlink:href="#facility" transform="translate({x},{y})"/>'
    if kind == "org":
        return f'<use xlink:href="#org" transform="translate({x},{y})"/>'
    return ""


def compass(cx, cy, r):
    parts = [f'<circle cx="{cx}" cy="{cy}" r="{r * 0.98:.0f}" fill="{PANEL}" '
             f'opacity="0.55" stroke="{INK}" stroke-width="2"/>',
             f'<circle cx="{cx}" cy="{cy}" r="{r * 0.8:.0f}" fill="none" '
             f'stroke="{INK_SOFT}" stroke-width="1" stroke-dasharray="3 5"/>']
    for i in range(8):
        a = -math.pi / 2 + i * math.pi / 4
        cardinal = i % 2 == 0
        tip = r * (0.74 if cardinal else 0.46)
        waist = r * (0.11 if cardinal else 0.075)
        back = -r * 0.13
        ca, sa = math.cos(a), math.sin(a)
        pa, pb = -sa, ca
        pts = [(cx + ca * tip, cy + sa * tip),
               (cx + ca * r * 0.22 + pa * waist, cy + sa * r * 0.22 + pb * waist),
               (cx + ca * back, cy + sa * back),
               (cx + ca * r * 0.22 - pa * waist, cy + sa * r * 0.22 - pb * waist)]
        fill = INK if cardinal else INK_SOFT
        parts.append(f'<path d="{d_of(pts)}" fill="{fill}" '
                     f'opacity="{0.92 if cardinal else 0.7}"/>')
    parts.append(f'<circle cx="{cx}" cy="{cy}" r="4" fill="{INK}"/>')
    for s, dx, dy in (("北", 0, -1), ("南", 0, 1), ("东", 1, 0), ("西", -1, 0)):
        parts.append(text(cx + dx * (r + 24), cy + dy * (r + 24) + 9, s, 24,
                          weight="700"))
    return "".join(parts)


def scale_bar(x, y, segments, seg_w):
    parts = []
    for i in range(segments):
        fill = INK if i % 2 == 0 else PANEL
        parts.append(f'<rect x="{x + i * seg_w}" y="{y}" width="{seg_w}" '
                     f'height="11" fill="{fill}" stroke="{INK}" '
                     f'stroke-width="1.4"/>')
    for i in range(segments + 1):
        parts.append(f'<path d="M {x + i * seg_w} {y - 5} L {x + i * seg_w} '
                     f'{y + 16}" stroke="{INK}" stroke-width="1.2"/>')
        parts.append(text(x + i * seg_w, y - 12, str(i * 100), 17,
                          halo_width=3.5))
    parts.append(text(x + segments * seg_w + 52, y + 10, "里（示意）", 18,
                      anchor="start", fill=INK_SOFT, halo_width=3.5))
    return "".join(parts)


def main():
    svg = build()
    OUT.write_text(svg, encoding="utf-8")
    print(f"wrote {OUT} ({len(svg) / 1024:.0f} KiB)")


if __name__ == "__main__":
    main()
