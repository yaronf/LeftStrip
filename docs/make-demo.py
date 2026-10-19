"""Abstract LeftStrip loop. Renders PNG frames for ffmpeg to encode."""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 960, 588
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
FONT = ImageFont.truetype(r"C:\Windows\Fonts\segoeui.ttf", 22)

# About 15 characters per second, slower than Netflix's 17. The picture waits.
CAPTIONS = [
    (0.15, 1.95, "Two ordinary windows"),
    (2.15, 4.75, "A strip is reserved on the left"),
    (4.95, 6.95, "One window moves into it"),
    (7.15, 10.15, "The space beside it is now the whole screen"),
    (10.35, 13.35, "Minimize and maximize stay inside that area"),
    (13.55, 16.15, "The docked window stays on top"),
]


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


def segment(t, keys):
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, r0), (t1, r1) in zip(keys, keys[1:]):
        if t <= t1:
            return rect_lerp(r0, r1, span(t, t0, t1))
    return keys[-1][1]


def caption_alpha(t, start, end):
    fade = 0.18
    if t < start or t > end:
        return 0.0
    alpha = 1.0
    if t < start + fade:
        alpha = (t - start) / fade
    if t > end - fade:
        alpha = min(alpha, (end - t) / fade)
    return max(0.0, min(1.0, alpha))


def draw_caption(base, t):
    shown = None
    for start, end, line in CAPTIONS:
        alpha = caption_alpha(t, start, end)
        if alpha > 0:
            shown = (line, alpha)
            break
    if shown is None:
        return base
    line, alpha = shown
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    bbox = d.textbbox((0, 0), line, font=FONT)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    x = (W - tw) / 2 - bbox[0]
    y = 528 - bbox[1]
    pad_x, pad_y = 18, 10
    ink = int(235 * alpha)
    d.rounded_rectangle(
        (x + bbox[0] - pad_x, y + bbox[1] - pad_y, x + bbox[2] + pad_x, y + bbox[3] + pad_y),
        radius=14,
        fill=(28, 34, 46, ink),
    )
    d.text((x, y), line, font=FONT, fill=(255, 255, 255, int(255 * alpha)))
    return Image.alpha_composite(base, layer)


def frame_at(t):
    img = Image.new("RGBA", (W, H), DESK)
    d = ImageDraw.Draw(img)
    sx, sy, sw, sh = SCREEN
    strip_p = span(t, 2.25, 3.00)
    move_a = span(t, 5.05, 6.35)

    a0 = (sx + 36, sy + 48, 250, 300)
    b0 = (sx + 330, sy + 78, 470, 280)
    a1 = (sx + 14, sy + 16, STRIP_W - 28, sh - 32)
    # The remaining area is the whole screen once the strip is reserved.
    b1 = (sx + STRIP_W + 18, sy + 16, sw - STRIP_W - 36, sh - 32)
    b_min = (sx + STRIP_W + 18, sy + sh - 72, 230, 46)
    # Slide over the strip; the docked window is drawn later, so it stays on top.
    b_over = (sx + 70, sy + 70, 420, 250)

    if strip_p > 0:
        grow = (sx, sy + 10, STRIP_W * strip_p, sh - 20)
        rr(d, grow, 18, STRIP)

    b_rect = segment(t, [
        (0.0, b0),
        (7.25, b0),
        (8.35, b1),
        (10.45, b1),
        (11.15, b_min),
        (11.85, b_min),
        (12.75, b1),
        (13.65, b1),
        (14.45, b_over),
        (15.15, b_over),
        (15.90, b1),
    ])
    img = window(img, b_rect, accent=False)
    img = window(img, rect_lerp(a0, a1, move_a), accent=True)
    img = draw_caption(img, t)
    return img.convert("RGB")


def main():
    os.makedirs(OUT, exist_ok=True)
    duration = 16.6
    n = int(duration * FPS)
    for i in range(n):
        frame_at(i / FPS).save(os.path.join(OUT, f"f{i:04d}.png"))
    print(f"{n} frames -> {OUT}")


if __name__ == "__main__":
    main()
