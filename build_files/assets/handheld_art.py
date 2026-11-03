# The handheld drawn in front of the CabinetOS cabinet (FORK (handheld)).
#
# ONE LIST OF SHAPES, in the cabinet's own 1024 space (frontend/src/startscreen.cpp,
# drawCabinet), so the start screen, the boot watermark and the logo in the README
# cannot disagree. One plain, generic handheld, no real product's shape or artwork.
#
#   python3 build_files/assets/handheld_art.py svg   > docs/assets/mark-handheld.svg
#   python3 build_files/assets/handheld_art.py cpp   (the lines in startscreen.cpp)
#
# make-boot-watermark.py imports shapes() and cabinet() from here.
import sys

CREAM, PANEL, DARK = 0xF2EEE8, 0xD7D1C8, 0x3A3444
RED, YEL, LGREY = 0xEC405C, 0xFFC457, 0x9A93A6

def device():
    """One plain handheld: dark body, screen in the middle, a d-pad on the left,
    four buttons on the right. No brand's shape or features."""
    x, y, w, h = 362, 634, 300, 134
    s = []
    s.append(("R", x - 7, y - 7, w + 14, h + 14, h * .40 + 7, CREAM))
    s.append(("R", x, y, w, h, h * .40, DARK))
    s.append(("ramp", x + w * .24, y + h * .12, w * .52, h * .66, 9))
    s.append(("R", x + w * .115 - h * .045, y + h * .30, h * .09, h * .30, 3, LGREY))     # d-pad
    s.append(("R", x + w * .115 - h * .15, y + h * .40, h * .30, h * .09, 3, LGREY))
    bx, by = x + w * .885, y + h * .45                                                     # buttons
    for dx, dy, c in ((0, -1, YEL), (-1, 0, RED), (1, 0, RED), (0, 1, YEL)):
        s.append(("dot", bx + dx * h * .14, by + dy * h * .14, h * .055, c))
    s.append(("dot", x + w * .43, y + h * .90, h * .035, LGREY))                           # start, select
    s.append(("dot", x + w * .57, y + h * .90, h * .035, LGREY))
    return s

def shapes():
    return device()

# ---- the cabinet, the same numbers as drawCabinet
def cabinet():
    return [("R",268,130,488,670,42,CREAM),("ramp_m",318,176,390,84,16),("ramp",316,292,394,284,18),
            ("R",248,600,527,112,20,PANEL),("dot",348,656,30,DARK),("dot",500,656,25,RED),("dot",583,656,24,YEL),
            ("dot",692,656,54,(0xFFB27A,.10)),("dot",692,656,40,(0xFFB27A,.16)),("dot",692,656,28,(0xFFB27A,.22)),
            ("R",668,646,48,20,10,0xFFF0E2),("R",303,795,419,55,14,PANEL)]
def hexs(c): return "#%06X" % c
def svg(shapes, vb=(170,110,684,780), extra=""):
    o=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb[0]} {vb[1]} {vb[2]} {vb[3]}" role="img" aria-label="CabinetOS Handheld">',
       '<defs><linearGradient id="m" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#FF96CA"/><stop offset="1" stop-color="#FFCB6E"/></linearGradient>',
       '<linearGradient id="s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#FEE08F"/><stop offset="1" stop-color="#EB5736"/></linearGradient></defs>']
    for sh in shapes:
        k=sh[0]
        if k=="R":
            _,x,y,w,h,r,c=sh; op=1
            if isinstance(c,tuple): c,op=c
            o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{r:.1f}" fill="{hexs(c)}" fill-opacity="{op}"/>')
        elif k=="dot":
            _,cx,cy,r,c=sh; op=1
            if isinstance(c,tuple): c,op=c
            o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{hexs(c)}" fill-opacity="{op}"/>')
        elif k in("ramp","ramp_m"):
            _,x,y,w,h,r=sh
            o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{r:.1f}" fill="url(#{"m" if k=="ramp_m" else "s"})"/>')
    o.append(extra); o.append('</svg>'); return "\n".join(o)

def cpp():
    def f(v):
        return str(int(v)) if float(v).is_integer() else ("%.1f" % v).rstrip("0").rstrip(".")
    out = []
    for sh in shapes():
        if sh[0] == "R":
            _, x, y, w, h, r, c = sh
            out.append("        R(%s, %s, %s, %s, %s, Color::rgb(0x%06X));" % (f(x), f(y), f(w), f(h), f(r), c))
        elif sh[0] == "dot":
            _, x, y, r, c = sh
            out.append("        dot(%s, %s, %s, Color::rgb(0x%06X));" % (f(x), f(y), f(r), c))
        else:
            _, x, y, w, h, r = sh
            out.append("        ramp(%s, %s, %s, %s, %s, screenTex);" % (f(x), f(y), f(w), f(h), f(r)))
    return "\n".join(out)

if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    if what == "svg":
        print(svg(cabinet() + shapes()))
    elif what == "cpp":
        print(cpp())
    else:
        sys.exit("usage: handheld_art.py svg|cpp")
