#!/usr/bin/env python3
"""Generate the GoaSynth brand assets: app icon + logo, in the UV GOA palette.

    python tools/make-brand.py            # writes into Assets/ and docs/

Everything is drawn procedurally (no font files needed) from the plugin's
house palette — the same three accents the editor's uvGradient() uses:

    violet  #9b6cff   (accent)
    teal    #36d6c3   (accentA)
    magenta #ff5f9e   (accentB)
    on purple-black  #171126 -> #0b0a14   (header / background darks)

The mark is the site's sine swoosh (docs/index.html .brand-mark) redrawn in
the true palette with a supersaw-style echo and a swirl glow behind it — the
plugin header's animated spiral, frozen into a logo. The script is
deterministic: same input, byte-stable output, so re-running it never churns
the repository.

Outputs
    Assets/icon_512.png   ICON_BIG for juce_add_plugin / juce_add_gui_app
    Assets/icon_256.png   docs / store / general use
    Assets/icon_32.png    ICON_SMALL for juceaide's Windows .ico
    docs/favicon.ico      multi-size 16/32/48 favicon for the website
    Assets/goasynth-logo.png   raster lockup, mark + GOASYNTH wordmark
    Assets/goasynth-logo-white.png   lockup with white wordmark (dark grounds)
    Assets/goasynth-logo-square.png  1024x1024 stacked lockup
    Assets/goasynth-mark.svg   vector mark (swoosh only, transparent)
    Assets/goasynth-logo.svg   vector lockup (mark + GOASYNTH wordmark)
"""


def _system_font(names):
    """First Windows/system font that loads, else Pillow's tiny default."""
    for name in names:
        for folder in (os.path.join(os.environ.get("WINDIR", "C:\\Windows"), "Fonts"), ""):
            try:
                return ImageFont.truetype(os.path.join(folder, name), 20)
            except OSError:
                continue
    return ImageFont.load_default()


# Wordmark faces: Segoe UI bold/light like the site lockup, graceful fallbacks.
FONT_BOLD = [("segoeuib.ttf"), ("arialbd.ttf"), ("DejaVuSans-Bold.ttf")]
FONT_LIGHT = [("segoeuil.ttf"), ("segoeui.ttf"), ("arial.ttf"), ("DejaVuSans.ttf")]


def _scaled_font(candidates, px):
    f = _system_font(candidates)
    try:
        return f.font_variant(size=px)
    except AttributeError:            # load_default() bitmap font
        return f

import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "Assets")
DOCS = os.path.join(ROOT, "docs")

# --- house palette (Source/PluginEditor.cpp, uvPalette()) --------------------
VIOLET = (0x9B, 0x6C, 0xFF)
TEAL   = (0x36, 0xD6, 0xC3)
MAGENTA= (0xFF, 0x5F, 0x9E)
TILE_TOP  = (0x1C, 0x14, 0x33)   # purple-black, lifted for the tile
TILE_BOT  = (0x0B, 0x0A, 0x14)   # the plugin's bgDark
TEXT_BRIGHT = (0xEC, 0xEF, 0xFB) # the plugin's textBright, for white lockups

SS = 4            # supersample factor: draw at N*512, downscale with LANCZOS
SIZE = 512

# The swoosh, in 512-space: half-sine dip, crest, then the rising sweep off
# the top-right — the site's M8 42c7-22 13-22 20 0s13 22 28-16, recomposed.
STROKE = [
    ((44, 330), (128, 176), (216, 176), (256, 300)),   # dip
    ((256, 300), (296, 424), (378, 448), (448, 138)),  # crest -> rising sweep
]
STROKE_W = 58


# --- helpers -----------------------------------------------------------------
def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def three_stop_strip(c0, c1, c2):
    """256x1 horizontal three-stop gradient (0 / .5 / 1), stretched to width."""
    strip = Image.new("RGB", (256, 1))
    px = strip.load()
    for x in range(256):
        t = x / 255.0
        px[x, 0] = lerp(c0, c1, t * 2.0) if t < 0.5 else lerp(c1, c2, t * 2.0 - 1.0)
    return strip


def bezier(p0, p1, p2, p3, t):
    mt = 1.0 - t
    x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
    y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
    return x, y


def swoosh_points(scale, ox=0.0, oy=0.0):
    pts = []
    for seg in STROKE:
        for i in range(65):
            t = i / 64.0
            x, y = bezier(*[tuple(c * scale + o for c, o in zip(p, (ox, oy)))
                            for p in seg], t)
            pts.append((x, y))
    return pts


def gradient_canvas(size, stops):
    """Horizontal three-stop gradient image at the given pixel size."""
    return three_stop_strip(*stops).resize((size, size), Image.BILINEAR)


def draw_swoosh(n, tile=None):
    """The mark on its own (RGBA): gradient stroke + echo + glow + hot core."""
    s = n / float(SIZE)
    layer = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(layer)

    # supersaw-style echoes: two offset, thinner, fainter strokes underneath
    for off, w_mul, alpha in ((14, 0.55, 70), (-14, 0.55, 70)):
        echo = Image.new("L", (n, n), 0)
        ImageDraw.Draw(echo).line(swoosh_points(s, 0, off * s),
                                  fill=alpha, width=int(STROKE_W * w_mul * s),
                                  joint="curve")
        layer = Image.composite(Image.new("L", (n, n), 255), layer,
                                echo.point(lambda v: v and alpha))
        d = ImageDraw.Draw(layer)

    # main stroke
    d.line(swoosh_points(s), fill=255, width=int(STROKE_W * s), joint="curve")

    # round caps on both ends
    r = STROKE_W * s / 2.0
    pts = swoosh_points(s)
    for p in (pts[0], pts[-1]):
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=255)

    # outer glow: blurred copy, screen-ish under the stroke
    glow = layer.filter(ImageFilter.GaussianBlur(n * 0.045))

    out = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    grad = gradient_canvas(n, (VIOLET, TEAL, MAGENTA))
    out.paste(grad, (0, 0), glow)                       # soft halo
    out.paste(grad, (0, 0), layer)                      # crisp stroke

    # white-hot core along the stroke for glow depth
    core = Image.new("L", (n, n), 0)
    ImageDraw.Draw(core).line(swoosh_points(s), fill=110,
                              width=int(STROKE_W * 0.22 * s), joint="curve")
    for p in (pts[0], pts[-1]):
        rc = STROKE_W * 0.22 * s / 2.0
        ImageDraw.Draw(core).ellipse([p[0] - rc, p[1] - rc, p[0] + rc, p[1] + rc],
                                     fill=110)
    out.paste(Image.new("RGBA", (n, n), (255, 255, 255, 255)), (0, 0),
              core.filter(ImageFilter.GaussianBlur(n * 0.006)))
    return out


def rounded_mask(n, radius):
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, n - 1, n - 1], radius=radius, fill=255)
    return m


def draw_install_badge(tile, n):
    """Teal disc + dark down-arrow, bottom-right: 'this one installs'.

    Drawn on the finished tile (over the rim corner, like an app shortcut
    badge), sized in 512-space so it scales with the render.
    """
    s = n / float(SIZE)
    d = ImageDraw.Draw(tile)

    cx = cy = n * 0.80
    r = 74 * s                      # disc radius
    ring = 10 * s                   # dark ring so it reads on any background

    d.ellipse([cx - r - ring, cy - r - ring, cx + r + ring, cy + r + ring],
              fill=TILE_BOT + (255,))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=TEAL + (255,))

    # down arrow: stem + triangular head, dark on teal
    stem_w = 34 * s
    d.rectangle([cx - stem_w / 2, cy - 58 * s, cx + stem_w / 2, cy - 6 * s],
                fill=TILE_BOT + (255,))
    d.polygon([(cx - 52 * s, cy - 6 * s), (cx + 52 * s, cy - 6 * s),
               (cx, cy + 62 * s)], fill=TILE_BOT + (255,))


def tile_backdrop(n):
    """Purple-black vertical gradient + frozen swirl glows (the editor's backdrop)."""
    s = n / float(SIZE)
    g = Image.linear_gradient("L").resize((n, n))       # black top -> white bottom
    base = Image.composite(Image.new("RGB", (n, n), TILE_BOT),
                           Image.new("RGB", (n, n), TILE_TOP), g)

    # two counter-rotating log-spiral echoes as radial glows (UV + teal)
    glow = Image.new("L", (n, n), 0)
    dg = ImageDraw.Draw(glow)
    for cx, cy, r, a in ((n * 0.28, n * 0.30, n * 0.52, 46),   # violet, upper left
                         (n * 0.78, n * 0.76, n * 0.46, 34)):  # teal, lower right
        dg.ellipse([cx - r, cy - r, cx + r, cy + r], fill=a)
    glow = glow.filter(ImageFilter.GaussianBlur(n * 0.10))
    tinted = Image.composite(gradient_canvas(n, (VIOLET, TEAL, MAGENTA)),
                             base, glow)
    return Image.blend(base, tinted, 0.9)


def make_icon(n):
    """The rounded-tile app icon at n px."""
    s = n / float(SIZE)
    ss = Image.new("RGBA", (n, n), (0, 0, 0, 0))

    # backdrop + swoosh, masked to a rounded square (iOS-style squircle radius)
    back = tile_backdrop(n).convert("RGBA")
    mark = draw_swoosh(n)
    back.alpha_composite(mark)
    mask = rounded_mask(n, int(112 * s))
    ss.paste(back, (0, 0), mask)

    # thin violet rim so the tile reads on white DAW backgrounds
    d = ImageDraw.Draw(ss)
    rim = rounded_mask(n, int(112 * s))
    inner = rounded_mask(n, int(112 * s - max(2, 3 * s)))
    d.bitmap((0, 0), Image.composite(Image.new("L", (n, n), 0), inner, rim),
             fill=VIOLET + (90,))
    return ss


def _wordmark_mask(W, H, ss, base_px=270):
    """GOA (bold) + SYNTH (light) as a text mask, auto-fit to the space right
    of the mark so any font fallback still fits the canvas."""
    x0 = 640 * ss
    avail = W - x0 - 8 * ss
    gap = 60 * ss
    px = base_px * ss
    bold = _scaled_font(FONT_BOLD, px)
    light = _scaled_font(FONT_LIGHT, px)
    total = bold.getlength("GOA") + gap + light.getlength("SYNTH")
    if total > avail:
        k = avail / total
        bold = _scaled_font(FONT_BOLD, int(px * k))
        light = _scaled_font(FONT_LIGHT, int(px * k))
    mask = Image.new("L", (W, H), 0)
    dm = ImageDraw.Draw(mask)
    x = x0
    baseline = 330 * ss
    dm.text((x, baseline), "GOA", font=bold, fill=255, anchor="ls")
    x += int(bold.getlength("GOA")) + gap
    dm.text((x, baseline), "SYNTH", font=light, fill=255, anchor="ls")
    return mask


def _fill_wordmark(canvas, mask, wordmark):
    """Paint the text mask: the accent gradient (default) or flat white."""
    bbox = mask.getbbox()
    if not bbox:
        return
    crop = mask.crop(bbox)
    if wordmark == "white":
        fill = Image.new("RGB", (bbox[2] - bbox[0], bbox[3] - bbox[1]), TEXT_BRIGHT)
    else:
        # the accent gradient runs across the wordmark only: GOA starts violet,
        # SYNTH ends magenta (same fill family as the header's GOA text)
        fill = three_stop_strip(VIOLET, TEAL, MAGENTA).resize(
            (bbox[2] - bbox[0], bbox[3] - bbox[1]), Image.BILINEAR)
    canvas.paste(fill, bbox[:2], crop)


def make_png_logo(width=1560, height=512, wordmark="gradient"):
    """Raster lockup: the mark at left, GOA (bold) + SYNTH (light) wordmark in
    the accent gradient - the same fill the plugin header's GOA text uses.
    Rendered at 2x and downsampled for crisp edges."""
    ss = 2
    W, H = width * ss, height * ss
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # mark in the left 512*ss square, vertically as designed
    canvas.alpha_composite(draw_swoosh(512 * ss), (0, 0))

    mask = _wordmark_mask(W, H, ss)
    _fill_wordmark(canvas, mask, wordmark)

    return canvas.resize((width, height), Image.LANCZOS)


def make_png_logo_square(size=1024):
    """Stacked square lockup: mark above, GOASYNTH wordmark below, both
    centred - avatar / square-profile use."""
    ss = 2
    W = size * ss
    mark_px = 560 * ss                      # swoosh drawn at this pixel size
    gap = 90 * ss
    canvas = Image.new("RGBA", (W, W), (0, 0, 0, 0))

    # wordmark line, centred on the full width
    text_px = 180 * ss
    bold = _scaled_font(FONT_BOLD, text_px)
    light = _scaled_font(FONT_LIGHT, text_px)
    gap_word = 34 * ss
    total = bold.getlength("GOA") + gap_word + light.getlength("SYNTH")
    target = 680 * ss
    if total > target:
        k = target / total
        bold = _scaled_font(FONT_BOLD, int(text_px * k))
        light = _scaled_font(FONT_LIGHT, int(text_px * k))
        total = bold.getlength("GOA") + gap_word + light.getlength("SYNTH")
    asc, _desc = bold.getmetrics()
    text_h = asc

    # vertically centre mark + gap + text as one composition
    content_h = mark_px + gap + text_h
    top = (W - content_h) // 2

    canvas.alpha_composite(draw_swoosh(mark_px), ((W - mark_px) // 2, top))

    mask = Image.new("L", (W, W), 0)
    dm = ImageDraw.Draw(mask)
    x = int((W - total) // 2)
    baseline = top + mark_px + gap + text_h
    dm.text((x, baseline), "GOA", font=bold, fill=255, anchor="ls")
    x += int(bold.getlength("GOA")) + gap_word
    dm.text((x, baseline), "SYNTH", font=light, fill=255, anchor="ls")
    _fill_wordmark(canvas, mask, "gradient")

    return canvas.resize((size, size), Image.LANCZOS)


def write_svg_mark(path):
    """Vector version of the mark (transparent background)."""
    seg = "M 44 330 C 128 176, 216 176, 256 300 C 296 424, 378 448, 448 138"
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <!-- GoaSynth mark - the sine swoosh in the UV GOA palette. Generated by tools/make-brand.py -->
  <defs>
    <linearGradient id="uv" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#9b6cff"/><stop offset=".5" stop-color="#36d6c3"/><stop offset="1" stop-color="#ff5f9e"/>
    </linearGradient>
    <filter id="blur" x="-30%" y="-30%" width="160%" height="160%">
      <feGaussianBlur stdDeviation="18"/>
    </filter>
  </defs>
  <path d="{seg}" fill="none" stroke="url(#uv)" stroke-width="32" stroke-linecap="round" opacity=".28" filter="url(#blur)" transform="translate(0 14)"/>
  <path d="{seg}" fill="none" stroke="url(#uv)" stroke-width="32" stroke-linecap="round" opacity=".28" filter="url(#blur)" transform="translate(0 -14)"/>
  <path d="{seg}" fill="none" stroke="url(#uv)" stroke-width="58" stroke-linecap="round"/>
</svg>
'''
    with open(path, "w", newline="\n") as f:
        f.write(svg)


def write_svg_logo(path):
    """Lockup: mark + GOASYNTH wordmark (system-font geometric caps)."""
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1520 512" width="1520" height="512">
  <!-- GoaSynth logo lockup - generated by tools/make-brand.py -->
  <defs>
    <linearGradient id="uv" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#9b6cff"/><stop offset=".5" stop-color="#36d6c3"/><stop offset="1" stop-color="#ff5f9e"/>
    </linearGradient>
    <linearGradient id="word" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#eceffb"/><stop offset="1" stop-color="#94a0c4"/>
    </linearGradient>
  </defs>
  <use href="#mark"/>
  <g transform="translate(640 0)">
    <text x="0" y="330" font-family="'Segoe UI', 'Helvetica Neue', Arial, sans-serif" font-size="270" font-weight="800" letter-spacing="6" fill="url(#word)">GOA</text>
    <text x="500" y="330" font-family="'Segoe UI', 'Helvetica Neue', Arial, sans-serif" font-size="270" font-weight="300" letter-spacing="6" fill="url(#word)">SYNTH</text>
  </g>
  <defs>
    <g id="mark">
      <path d="M 44 330 C 128 176, 216 176, 256 300 C 296 424, 378 448, 448 138" fill="none" stroke="url(#uv)" stroke-width="32" stroke-linecap="round" opacity=".28" transform="translate(0 14)"/>
      <path d="M 44 330 C 128 176, 216 176, 256 300 C 296 424, 378 448, 448 138" fill="none" stroke="url(#uv)" stroke-width="32" stroke-linecap="round" opacity=".28" transform="translate(0 -14)"/>
      <path d="M 44 330 C 128 176, 216 176, 256 300 C 296 424, 378 448, 448 138" fill="none" stroke="url(#uv)" stroke-width="58" stroke-linecap="round"/>
    </g>
  </defs>
</svg>
'''
    with open(path, "w", newline="\n") as f:
        f.write(svg)


def main():
    os.makedirs(ASSETS, exist_ok=True)

    print("rendering at %dx%d supersample..." % (SIZE * SS, SIZE * SS))
    big = make_icon(SIZE * SS)

    for name, px in (("icon_512.png", 512), ("icon_256.png", 256), ("icon_32.png", 32)):
        out = big.resize((px, px), Image.LANCZOS)
        out.save(os.path.join(ASSETS, name))
        print("  Assets/%s" % name)

    # installer variant: same face + install badge (GoaSynth-Setup's ICON_*).
    setup_big = make_icon(SIZE * SS)
    draw_install_badge(setup_big, SIZE * SS)
    for name, px in (("setup-icon_512.png", 512), ("setup-icon_32.png", 32)):
        setup_big.resize((px, px), Image.LANCZOS).save(os.path.join(ASSETS, name))
        print("  Assets/%s" % name)

    # favicon: 256 render downsampled into a multi-size .ico for the website
    fav_src = big.resize((256, 256), Image.LANCZOS)
    fav_src.save(os.path.join(DOCS, "favicon.ico"),
                 sizes=[(16, 16), (32, 32), (48, 48)])
    print("  docs/favicon.ico")

    # the site's screenshot folder is docs/assets (lowercase on the published
    # host), so the touch icon is copied there alongside the og:image assets
    fav_src.save(os.path.join(DOCS, "assets", "icon_256.png"))
    print("  docs/assets/icon_256.png")

    logo = make_png_logo()
    logo.save(os.path.join(ASSETS, "goasynth-logo.png"))
    print("  Assets/goasynth-logo.png (%d KB)" % (
        os.path.getsize(os.path.join(ASSETS, "goasynth-logo.png")) // 1024))

    white = make_png_logo(wordmark="white")
    white.save(os.path.join(ASSETS, "goasynth-logo-white.png"))
    print("  Assets/goasynth-logo-white.png (%d KB)" % (
        os.path.getsize(os.path.join(ASSETS, "goasynth-logo-white.png")) // 1024))

    square = make_png_logo_square()
    square.save(os.path.join(ASSETS, "goasynth-logo-square.png"))
    print("  Assets/goasynth-logo-square.png (%d KB)" % (
        os.path.getsize(os.path.join(ASSETS, "goasynth-logo-square.png")) // 1024))

    write_svg_mark(os.path.join(ASSETS, "goasynth-mark.svg"))
    write_svg_logo(os.path.join(ASSETS, "goasynth-logo.svg"))
    print("  Assets/goasynth-mark.svg, Assets/goasynth-logo.svg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
