#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Generates the iDrive (Supra j29 ID6L) menu images for the NBT-AAD plugin. Needs Pillow only.

Style matched to the factory assets in /opt/hmi/ID5/data/ro/j29/id6l/:
  aa_content_preview_549x484.png  main-menu content preview (cf. content_preview/cp_carplay/cp_carplay.png):
                                  grey gradient line glyph on a dark soft vignette, transparent edges
  aa_row_icon_63x54.png           shipped Android Auto arrow (copied, not regenerated)
  aa_statusbar_54x54.png          status-bar audio-source icon (cf. statusbar_main_audiosource/carplay.png)
Glyph: a road in perspective forming an "A" (original artwork, not the Google logo).
Usage: python3 make_icons.py [outdir]
"""
import sys, os, shutil
from PIL import Image, ImageDraw, ImageFilter, ImageChops
from PIL.PngImagePlugin import PngInfo

NOTICE = "Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto). License: PolyForm-Noncommercial-1.0.0"

def glyph_mask(w, h, box, stroke, ss=4):
    W2, H2 = w * ss, h * ss
    m = Image.new("L", (W2, H2), 0)
    d = ImageDraw.Draw(m)
    x0, y0, x1, y1 = [v * ss for v in box]
    bw, bh, sw = x1 - x0, y1 - y0, stroke * ss
    P = lambda u, v: (x0 + u * bw, y0 + v * bh)
    def seg(a, c, wd):
        d.line([a, c], fill=255, width=int(wd))
        for p in (a, c):
            d.ellipse([p[0] - wd / 2, p[1] - wd / 2, p[0] + wd / 2, p[1] + wd / 2], fill=255)
    apex = P(0.5, 0.0)
    seg(P(0.06, 1.0), apex, sw)
    seg(apex, P(0.94, 1.0), sw)
    for va, vb, k in [(0.80, 0.98, 1.0), (0.53, 0.67, 0.8), (0.33, 0.42, 0.62)]:   # lane dashes, perspective
        seg(P(0.5, va), P(0.5, vb), sw * k)
    return m.resize((w, h), Image.LANCZOS)

def vgrad(w, h, top, bot, y0, y1):
    g = Image.new("RGBA", (w, h))
    px = g.load()
    for y in range(h):
        t = min(1.0, max(0.0, (y - y0) / float(max(1, y1 - y0))))
        c = int(top + (bot - top) * t)
        for x in range(w):
            px[x, y] = (c, c, c, 255)
    return g

def save(img, path):
    info = PngInfo()
    info.add_text("Copyright", NOTICE)
    img.save(path, pnginfo=info)
    print("wrote", path, img.size)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    W, H = 549, 484
    dark = Image.new("L", (W, H), 0)
    ImageDraw.Draw(dark).rounded_rectangle([92, 88, 458, 398], radius=70, fill=255)
    bg = vgrad(W, H, 8, 28, 80, 420)
    bg.putalpha(dark.filter(ImageFilter.GaussianBlur(30)))
    floor = Image.new("L", (W, H), 0)
    ImageDraw.Draw(floor).ellipse([190, 355, 370, 392], fill=150)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.putalpha(floor.filter(ImageFilter.GaussianBlur(12)))
    fill = vgrad(W, H, 196, 104, 116, 318)
    fill.putalpha(glyph_mask(W, H, (192, 116, 362, 318), 9.5))
    prev = bg.copy(); prev.alpha_composite(shadow); prev.alpha_composite(fill)
    save(prev, os.path.join(out, "aa_content_preview_549x484.png"))

    icon = os.path.join(os.path.dirname(os.path.abspath(__file__)), "aa_row_icon_63x54.png")
    target = os.path.join(out, "aa_row_icon_63x54.png")
    if os.path.abspath(icon) != os.path.abspath(target):
        shutil.copyfile(icon, target)

    gm = glyph_mask(54, 54, (9, 5, 45, 49), 4.6, ss=8)
    sb = Image.new("RGBA", (54, 54), (255, 255, 255, 0)); sb.putalpha(gm)
    save(sb, os.path.join(out, "aa_statusbar_54x54.png"))

if __name__ == "__main__":
    main()
