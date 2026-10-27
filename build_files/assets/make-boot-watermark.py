# The CabinetOS boot watermark (#108): the startup screen's cabinet icon
# (startscreen.cpp, drawCabinet, the same shapes and colours in its 1024 space) and
# the wordmark in Noto Sans Bold, white, on transparent. Bazzite's is 149x43.
import sys
from PIL import Image, ImageDraw, ImageFont
H = 43                  # final height, as Bazzite's
SS = 8                  # supersampling
ICON_H = 36             # icon height inside H
font_path, out = sys.argv[1], sys.argv[2]

def rgb(h, a=1.0): return ((h >> 16) & 255, (h >> 8) & 255, h & 255, int(a * 255))

def icon(px_h):
    s = px_h / 720.0                       # icon spans y 130..850
    ox, oy = -248 * s, -130 * s
    w, h = int(527 * s + 2), int(px_h + 2)
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    def layer(): return Image.new("RGBA", im.size, (0, 0, 0, 0))
    def R(x, y, ww, hh, rad, c):
        L = layer(); d = ImageDraw.Draw(L)
        d.rounded_rectangle([ox + x*s, oy + y*s, ox + (x+ww)*s, oy + (y+hh)*s], radius=rad*s, fill=c)
        im.alpha_composite(L)
    def dot(cx, cy, rad, c): R(cx-rad, cy-rad, rad*2, rad*2, rad, c)
    def ramp(x, y, ww, hh, rad, a, b, vertical):
        G = Image.new("RGBA", (int(ww*s)+1, int(hh*s)+1))
        n = G.height if vertical else G.width
        for i in range(n):
            f = i / max(n-1, 1)
            c = tuple(int(a[k] + (b[k]-a[k]) * f) for k in range(3)) + (255,)
            ImageDraw.Draw(G).line([(0, i), (G.width, i)] if vertical else [(i, 0), (i, G.height)], fill=c)
        M = Image.new("L", G.size, 0)
        ImageDraw.Draw(M).rounded_rectangle([0, 0, G.width-1, G.height-1], radius=rad*s, fill=255)
        L = layer(); L.paste(G, (int(ox + x*s), int(oy + y*s)), M); im.alpha_composite(L)
    body, panel = rgb(0xF2EEE8), rgb(0xD7D1C8)
    R(268, 130, 488, 670, 42, body)
    ramp(318, 176, 390, 84, 16, rgb(0xFF96CA), rgb(0xFFCB6E), False)
    ramp(316, 292, 394, 284, 18, rgb(0xFEE08F), rgb(0xEB5736), True)
    R(248, 600, 527, 112, 20, panel)
    dot(348, 656, 30, rgb(0x3A3444)); dot(500, 656, 25, rgb(0xEC405C)); dot(583, 656, 24, rgb(0xFFC457))
    dot(692, 656, 54, rgb(0xFFB27A, .10)); dot(692, 656, 40, rgb(0xFFB27A, .16)); dot(692, 656, 28, rgb(0xFFB27A, .22))
    R(668, 646, 48, 20, 10, rgb(0xFFF0E2))
    R(303, 795, 419, 55, 14, panel)
    return im

ic = icon(ICON_H * SS)
font = ImageFont.truetype(font_path, int(28 * SS))
text = "CabinetOS"
bb = font.getbbox(text)
tw, th = bb[2] - bb[0], bb[3] - bb[1]
# FORK (handheld): a smaller second line under the name.
sub_font = ImageFont.truetype(font_path, int(15 * SS))
sub_text = "Handheld"
sbb = sub_font.getbbox(sub_text)
sw, sh = sbb[2] - sbb[0], sbb[3] - sbb[1]
line_gap = int(3 * SS)
block_h = th + line_gap + sh
gap = int(9 * SS)
W = ic.width + gap + max(tw, sw) + 2 * SS
canvas = Image.new("RGBA", (W, H * SS), (0, 0, 0, 0))
canvas.alpha_composite(ic, (0, (H * SS - ic.height) // 2))
d = ImageDraw.Draw(canvas)
top = (H * SS - block_h) // 2
d.text((ic.width + gap - bb[0], top - bb[1]), text, font=font, fill=(255, 255, 255, 255))
d.text((ic.width + gap - sbb[0], top + th + line_gap - sbb[1]), sub_text, font=sub_font,
       fill=(255, 255, 255, 190))
final = canvas.resize((W // SS, H), Image.LANCZOS)
final.save(out)
print(out, final.size)
