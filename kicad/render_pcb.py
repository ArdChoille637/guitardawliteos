#!/usr/bin/env python3
"""
Render guitardawliteos.kicad_pcb to SVG without KiCad.

Why this exists: the .kicad_pcb and .kicad_sch are KiCad 10 files
(generator_version "10.0"), and Ubuntu 24.04 ships KiCad 7, which refuses
them ("Unknown token 'generator_version'"). The KiCad PPA and downloads.kicad.org
are both blocked from this environment, so a GUI review needs its own renderer.

Draws what pcbnew would show on an unrouted board: footprint courtyards, pads,
reference designators, and the RATSNEST — straight lines between pads sharing a
net, which on a board with no copper tracks *is* the wiring representation.

Usage:  python3 render_pcb.py [board.kicad_pcb] [out.svg]
"""
import re, sys, os, math
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- s-expr ----
def parse(text):
    """Minimal s-expression reader -> nested lists of str/list."""
    tok = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)
    stack, cur = [], []
    for t in tok:
        if t == '(':
            new = []
            cur.append(new)
            stack.append(cur)
            cur = new
        elif t == ')':
            if not stack:
                raise ValueError("unbalanced )")
            cur = stack.pop()
        else:
            cur.append(t[1:-1] if t.startswith('"') else t)
    return cur


def head(node):
    return node[0] if node and isinstance(node[0], str) else None


def kids(node, name):
    return [c for c in node if isinstance(c, list) and head(c) == name]


def kid(node, name):
    k = kids(node, name)
    return k[0] if k else None


def prop(fp, key):
    for p in kids(fp, "property"):
        if len(p) > 2 and p[1] == key:
            return p[2]
    return None


def nums(node, n=2):
    out = []
    for v in node[1:1 + n]:
        try:
            out.append(float(v))
        except (TypeError, ValueError):
            out.append(0.0)
    while len(out) < n:
        out.append(0.0)
    return out


# ------------------------------------------------------------- extraction ---
def load(path):
    root = parse(open(path).read())[0]
    fps = []
    for fp in kids(root, "footprint"):
        at = kid(fp, "at")
        x, y = nums(at, 2) if at else (0.0, 0.0)
        rot = 0.0
        if at and len(at) > 3:
            try:
                rot = float(at[3])
            except ValueError:
                pass
        pads = []
        for pd in kids(fp, "pad"):
            pat = kid(pd, "at")
            px, py = nums(pat, 2) if pat else (0.0, 0.0)
            sz = kid(pd, "size")
            sw, sh = nums(sz, 2) if sz else (1.0, 1.0)
            netn = kid(pd, "net")
            # KiCad 10: (net "NAME").  Older: (net <code> "NAME").
            net = ""
            if netn:
                net = netn[2] if len(netn) > 2 else (netn[1] if len(netn) > 1 else "")
            # rotate pad offset into board coordinates
            a = math.radians(rot)
            rx = px * math.cos(a) - py * math.sin(a)
            ry = px * math.sin(a) + py * math.cos(a)
            pads.append(dict(num=pd[1] if len(pd) > 1 else "?",
                             x=x + rx, y=y + ry, w=sw, h=sh, net=net))
        fps.append(dict(ref=prop(fp, "Reference") or "?",
                        val=prop(fp, "Value") or "",
                        x=x, y=y, rot=rot, pads=pads,
                        lib=fp[1] if len(fp) > 1 else ""))
    return fps


# ---------------------------------------------------------------- render ----
def render(fps, out):
    pts = [(p["x"], p["y"]) for f in fps for p in f["pads"]] or [(0, 0)]
    minx = min(p[0] for p in pts) - 8
    maxx = max(p[0] for p in pts) + 8
    miny = min(p[1] for p in pts) - 8
    maxy = max(p[1] for p in pts) + 8
    S = 6.0                       # px per mm
    W = max((maxx - minx) * S, 720)   # keep the header from clipping
    H = (maxy - miny) * S + 78        # header band

    def X(v): return (v - minx) * S
    def Y(v): return (v - miny) * S + 78

    nets = defaultdict(list)
    for f in fps:
        for p in f["pads"]:
            if p["net"]:
                nets[p["net"]].append(p)

    POWER = {"GND", "+5V", "+3V3", "VCC_A", "VREF"}
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.0f} {H:.0f}" '
         f'font-family="Helvetica, Arial, sans-serif">',
         f'<rect width="{W:.0f}" height="{H:.0f}" fill="#0d1117"/>',
         f'<text x="14" y="28" fill="#e6edf3" font-size="19" font-weight="bold">'
         f'guitardawliteos.kicad_pcb &#8212; rev A, as imported</text>',
         f'<text x="14" y="48" fill="#8b949e" font-size="12">'
         f'{len(fps)} footprints &#183; {len(nets)} nets &#183; '
         f'<tspan fill="#f85149">0 tracks &#183; 0 vias &#183; no board outline</tspan> '
         f'&#8212; ratsnest shown; nothing is routed</text>',
         f'<text x="14" y="66" fill="#8b949e" font-size="11">'
         f'rendered without KiCad (file is v10, container has v7)</text>']

    # ratsnest: chain each net's pads in nearest-neighbour order
    for net, pads in sorted(nets.items()):
        if len(pads) < 2:
            continue
        colour = "#6e7681" if net in POWER else "#2f81f7"
        wdt = 0.5 if net in POWER else 0.8
        rem = pads[1:]
        cur = pads[0]
        while rem:
            nxt = min(rem, key=lambda p: (p["x"] - cur["x"]) ** 2 + (p["y"] - cur["y"]) ** 2)
            o.append(f'<line x1="{X(cur["x"]):.1f}" y1="{Y(cur["y"]):.1f}" '
                     f'x2="{X(nxt["x"]):.1f}" y2="{Y(nxt["y"]):.1f}" '
                     f'stroke="{colour}" stroke-width="{wdt}" opacity="0.55"/>')
            rem.remove(nxt)
            cur = nxt

    # pads + bodies + refs
    for f in fps:
        for p in f["pads"]:
            w, h = max(p["w"] * S, 2), max(p["h"] * S, 2)
            o.append(f'<rect x="{X(p["x"]) - w / 2:.1f}" y="{Y(p["y"]) - h / 2:.1f}" '
                     f'width="{w:.1f}" height="{h:.1f}" rx="1" '
                     f'fill="#d29922" opacity="0.9"/>')
        o.append(f'<text x="{X(f["x"]):.1f}" y="{Y(f["y"]) - 5:.1f}" fill="#e6edf3" '
                 f'font-size="8" text-anchor="middle">{f["ref"]}</text>')

    o.append(f'<text x="14" y="{H - 16:.0f}" fill="#f85149" font-size="12">'
             f'RV1 (100 k gain trimmer) is absent from this board &#8212; '
             f'MIDF is left a one-node net, so Rf feeds nothing. See check_pcb.py</text>')
    o.append('</svg>')
    open(out, "w").write("\n".join(o))
    return len(fps), len(nets)


if __name__ == "__main__":
    board = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "guitardawliteos.kicad_pcb")
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/pcb-revA.svg"
    fps = load(board)
    n, m = render(fps, out)
    print(f"{n} footprints, {m} nets -> {out}")
