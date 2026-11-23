#!/usr/bin/env python3
"""The installer's pictures, in the console's look. Run by hand; the pictures
are committed and ci/brand-installer-iso.sh puts them on the ISO:

    tools/make-installer-art.py /usr/share/fonts/google-noto

(the folder with NotoSans-Bold.ttf and NotoSans-SemiBold.ttf, the console's
fonts) writes, in disk_config/:

- installer-logo.png, and installer-logo@2x.png for a screen the installer
  draws at twice the scale: the cabinet with the CabinetOS wordmark under it,
  the same width, white on transparent, for the dark strip down the left of
  the installer's screens (disk_config/installer.css).
- grub-background.png: the boot menu under UEFI (disk_config/grub-theme.txt),
  1920x1080, laid out as the console's start screen draws itself
  (startscreen.cpp, drawStartup): the backdrop, the cabinet, the wordmark,
  and the one entry, "Install CabinetOS", as a pill like first run's buttons.

The cabinet's shapes and colours are build_files/assets/make-boot-watermark.py's,
which are startscreen.cpp's drawCabinet in its 1024 space. They are repeated
here rather than shared because build_files/ is mounted into every layer of
the image build, so editing it rebuilds layers every console downloads.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

WIDTH = 132            # the logo's width at 1x, in pixels
SS = 8                 # supersampling


def rgb(h, a=1.0):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255, int(a * 255))


def cabinet(px_w):
    """The cabinet, px_w wide (it spans x 248..775, y 130..850 of its space)."""
    s = px_w / 527.0
    ox, oy = -248 * s, -130 * s
    im = Image.new("RGBA", (int(px_w + 2), int(720 * s + 2)), (0, 0, 0, 0))

    def layer():
        return Image.new("RGBA", im.size, (0, 0, 0, 0))

    def R(x, y, ww, hh, rad, c):
        L = layer()
        ImageDraw.Draw(L).rounded_rectangle(
            [ox + x * s, oy + y * s, ox + (x + ww) * s, oy + (y + hh) * s], radius=rad * s, fill=c)
        im.alpha_composite(L)

    def dot(cx, cy, rad, c):
        R(cx - rad, cy - rad, rad * 2, rad * 2, rad, c)

    def ramp(x, y, ww, hh, rad, a, b, vertical):
        G = Image.new("RGBA", (int(ww * s) + 1, int(hh * s) + 1))
        n = G.height if vertical else G.width
        for i in range(n):
            f = i / max(n - 1, 1)
            c = tuple(int(a[k] + (b[k] - a[k]) * f) for k in range(3)) + (255,)
            ImageDraw.Draw(G).line([(0, i), (G.width, i)] if vertical else [(i, 0), (i, G.height)], fill=c)
        M = Image.new("L", G.size, 0)
        ImageDraw.Draw(M).rounded_rectangle([0, 0, G.width - 1, G.height - 1], radius=rad * s, fill=255)
        L = layer()
        L.paste(G, (int(ox + x * s), int(oy + y * s)), M)
        im.alpha_composite(L)

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


def logo(font_path, width):
    W = width * SS
    text = "CabinetOS"
    # The wordmark is set to the cabinet's width, as on the start screen.
    probe = ImageFont.truetype(font_path, 100 * SS)
    pb = probe.getbbox(text)
    font = ImageFont.truetype(font_path, int(100 * SS * W / (pb[2] - pb[0])))
    bb = font.getbbox(text)
    # The start screen leaves 56 between the cabinet and the wordmark's
    # ascent line (where Pillow puts the text's top) when the wordmark is
    # LargeTitle (76) wide.
    ref = ImageFont.truetype(font_path, 76)
    gap = W * 56 / (ref.getbbox(text)[2] - ref.getbbox(text)[0])
    cab = cabinet(W)
    top = cab.height + gap
    H = int(top + bb[3]) + SS
    canvas = Image.new("RGBA", (W + 2 * SS, H), (0, 0, 0, 0))
    canvas.alpha_composite(cab, (SS, 0))
    ImageDraw.Draw(canvas).text((SS - bb[0], top), text, font=font, fill=(255, 255, 255, 255))
    return canvas


def backdrop(w, h):
    """The console's backdrop (ui.h, kBackdropTop, kBackdropMid at 0.55,
    kBackdropBottom), dithered as its shader is so the long dark ramp does not
    band; ordered rather than random, so the picture stays small."""
    top, mid, bottom = (0x3A, 0x22, 0x68), (0x12, 0x0C, 0x26), (0x09, 0x06, 0x14)
    bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
    rows = bytearray()
    for y in range(h):
        t = y / (h - 1)
        a, b, f = (top, mid, t / 0.55) if t < 0.55 else (mid, bottom, (t - 0.55) / 0.45)
        c = [a[k] + (b[k] - a[k]) * f for k in range(3)]
        cell = bytearray()
        for x in range(4):
            d = (bayer[(y % 4) * 4 + x] + 0.5) / 16
            cell += bytes(min(255, int(v + d)) for v in c)
        rows += cell * (w // 4)
    return Image.frombytes("RGB", (w, h), bytes(rows))


def menu_picture(fonts):
    W, H = 1920, 1080
    bold = os.path.join(fonts, "NotoSans-Bold.ttf")
    semi = os.path.join(fonts, "NotoSans-SemiBold.ttf")
    im = backdrop(W, H).convert("RGBA")
    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)

    # The start screen: the cabinet sized from the wordmark (LargeTitle, 76),
    # its top at 150, the wordmark's ascent line 56 under its base.
    mark = ImageFont.truetype(bold, 76)
    mb = mark.getbbox("CabinetOS")
    ww = mb[2] - mb[0]
    s = ww / 527.0
    cab = cabinet(ww * SS)
    cab = cab.resize((cab.width // SS, cab.height // SS), Image.LANCZOS)
    x0 = (W - ww) // 2
    over.alpha_composite(cab, (x0, 150))
    y = 150 + 720 * s + 56
    d.text((x0 - mb[0], y), "CabinetOS", font=mark, fill=(255, 255, 255, 240))
    ascent, descent = mark.getmetrics()
    y += ascent + descent

    # The one entry, as a focused pill (Title3, 38, SemiBold; a white rim).
    label = ImageFont.truetype(semi, 38)
    lb = label.getbbox("Install CabinetOS")
    pw, ph = lb[2] - lb[0] + 2 * 40, 76
    py = int(y + 56)
    pill = Image.new("RGBA", (pw * SS, ph * SS), (0, 0, 0, 0))
    ImageDraw.Draw(pill).rounded_rectangle([0, 0, pw * SS - 1, ph * SS - 1], radius=ph * SS // 2,
                                           outline=(255, 255, 255, 217), width=3 * SS)
    over.alpha_composite(pill.resize((pw, ph), Image.LANCZOS), ((W - pw) // 2, py))
    la, ld = label.getmetrics()
    d.text(((W - (lb[2] - lb[0])) // 2 - lb[0], py + (ph - (la + ld)) // 2), "Install CabinetOS",
           font=label, fill=(255, 255, 255, 255))

    im.alpha_composite(over)
    return im.convert("RGB")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fonts = sys.argv[1]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "disk_config")
    for scale, name in ((1, "installer-logo.png"), (2, "installer-logo@2x.png")):
        big = logo(os.path.join(fonts, "NotoSans-Bold.ttf"), WIDTH * scale)
        final = big.resize((big.width // SS, big.height // SS), Image.LANCZOS)
        final.save(os.path.join(out, name))
        print(name, final.size)
    menu_picture(fonts).save(os.path.join(out, "grub-background.png"), optimize=True)


if __name__ == "__main__":
    main()
