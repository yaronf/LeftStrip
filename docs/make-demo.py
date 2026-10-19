"""Abstract LeftStrip loop. Renders PNG frames for ffmpeg to encode."""
import os
from PIL import Image, ImageDraw

W, H = 960, 540
FPS = 20
OUT = os.path.join(os.path.dirname(__file__), "_frames")

DESK = (214, 220, 228, 255)
STRIP = (28, 34, 46, 255)
CYAN = (42, 212, 242, 255)
CARD = (255, 255, 255, 255)
LINE = (226, 231, 237, 255)
DOT = (186, 194, 204, 255)
SHADOW = (30, 40, 55, 28)

SCREEN = (48, 36, 864, 468)  # x, y, w, h
STRIP_W = 196


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def rect_lerp(a, b, t):
    return tuple(lerp(x, y, t) for x, y in zip(a, b))


def span(t, start, end):
    if t <= start:
        return 0.0
    if t >= end:
        return 1.0
    return ease((t - start) / (end - start))


def rr(draw, rect, radius, fill):
    x, y, w, h = rect
    draw.rounded_rectangle((x, y, x + w, y + h), radius=radius, fill=fill)


def window(base, rect, accent=False):
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x, y, w, h = rect
    d.rounded_rectangle((x + 3, y + 8, x + w + 3, y + h + 8), radius=18, fill=SHADOW)
    d.rounded_rectangle((x, y, x + w, y + h), radius=16, fill=CARD, outline=(206, 214, 224, 255), width=2)
    if accent:
        d.rounded_rectangle((x, y, x + 14, y + h), radius=8, fill=CYAN)
        d.rectangle((x + 8, y, x + 14, y + h), fill=CYAN)
    inset = 28 if accent else 16
    for i, width in enumerate((0.72, 0.46, 0.58)):
        yy = y + 22 + i * 28
        if yy + 12 > y + h - 16:
            break
        d.rounded_rectangle(
            (x + inset, yy, x + inset + max(24, (w - inset - 18) * width), yy + 10),
            radius=5,
            fill=LINE,
        )
    d.ellipse((x + w - 42, y + 14, x + w - 32, y + 24), fill=DOT)
    d.ellipse((x + w - 28, y + 14, x + w - 18, y + 24), fill=DOT)
    return Image.alpha_composite(base, layer)


def frame_at(t):
    img = Image.new("RGBA", (W, H), DESK)
    d = ImageDraw.Draw(img)
    sx, sy, sw, sh = SCREEN
    strip_p = span(t, 0.7, 1.35)
    move_a = span(t, 1.35, 2.45)
    move_b = span(t, 2.35, 3.35)

    a0 = (sx + 36, sy + 48, 250, 300)
    b0 = (sx + 330, sy + 78, 470, 280)
    a1 = (sx + 14, sy + 16, STRIP_W - 28, sh - 32)
    b1 = (sx + STRIP_W + 18, sy + 16, sw - STRIP_W - 36, sh - 32)

    if strip_p > 0:
        grow = (sx, sy + 10, STRIP_W * strip_p, sh - 20)
        rr(d, grow, 18, STRIP)

    img = window(img, rect_lerp(b0, b1, move_b), accent=False)
    img = window(img, rect_lerp(a0, a1, move_a), accent=True)
    return img.convert("RGB")


def main():
    os.makedirs(OUT, exist_ok=True)
    duration = 4.6
    n = int(duration * FPS)
    for i in range(n):
        frame_at(i / FPS).save(os.path.join(OUT, f"f{i:04d}.png"))
    print(f"{n} frames -> {OUT}")


if __name__ == "__main__":
    main()
