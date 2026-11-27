"""Title card, end card and caption overlays of the showcase video, drawn with Pillow as 1920x1080 PNGs (the overlays
transparent). Plain type on dark: no logo of anyone, nothing that looks official."""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1920, 1080
FONT = 'C:/Windows/Fonts/bahnschrift.ttf'
WHITE, SOFT, DIM, ACCENT = (240, 246, 252), (170, 188, 206), (120, 136, 152), (86, 190, 255)


def font(size, weight='Light'):
    f = ImageFont.truetype(FONT, size)
    try:
        f.set_variation_by_name(weight)
    except (OSError, ValueError):
        pass
    return f


def text_width(text, f, tracking):
    return sum(f.getlength(c) for c in text) + tracking * max(0, len(text) - 1)


def draw_text(draw, xy, text, f, fill, tracking=0.0, anchor='center'):
    """One line with letter spacing (pixels between glyphs); xy is the line's centre, left or right end at the baseline box top."""
    width = text_width(text, f, tracking)
    x, y = xy
    if anchor == 'center':
        x -= width / 2
    elif anchor == 'right':
        x -= width
    for c in text:
        draw.text((x, y), c, font=f, fill=fill)
        x += f.getlength(c) + tracking
    return width


def backdrop():
    """Near black with a faint cool glow in the middle and a few dim stars (drawn, not a picture of the game)."""
    import random
    image = Image.new('RGB', (W, H), (4, 6, 10))
    glow = Image.new('L', (W, H), 0)
    ImageDraw.Draw(glow).ellipse((W * 0.18, H * 0.05, W * 0.82, H * 0.95), fill=70)
    glow = glow.filter(ImageFilter.GaussianBlur(220))
    image = Image.composite(Image.new('RGB', (W, H), (16, 30, 48)), image, glow)
    rng = random.Random(4)
    # a little noise, so the glow's gradient does not band in the encode
    import numpy
    pixels = numpy.asarray(image).astype(numpy.int16)
    pixels += numpy.random.default_rng(4).integers(-2, 3, size=(H, W, 1), dtype=numpy.int16)
    image = Image.fromarray(pixels.clip(0, 255).astype(numpy.uint8), 'RGB')
    draw = ImageDraw.Draw(image)
    for _ in range(260):
        x, y, v = rng.randrange(W), rng.randrange(H), rng.randrange(40, 150)
        draw.point((x, y), fill=(v, v, min(255, v + 20)))
    return image


def title(path, transparent=False):
    image = Image.new('RGBA', (W, H), (0, 0, 0, 0)) if transparent else backdrop().convert('RGBA')
    draw = ImageDraw.Draw(image)
    draw_text(draw, (W / 2, 410), 'X4 ONLINE', font(132, 'SemiLight'), WHITE, tracking=26)
    draw.rectangle((W / 2 - 90, 578, W / 2 + 90, 580), fill=ACCENT)
    draw_text(draw, (W / 2, 612), 'unofficial fan multiplayer for X4: Foundations', font(38), SOFT, tracking=2.5)
    image.save(path)


def end(path):
    image = backdrop().convert('RGBA')
    draw = ImageDraw.Draw(image)
    draw_text(draw, (W / 2, 372), 'X4 ONLINE', font(96, 'SemiLight'), WHITE, tracking=20)
    draw.rectangle((W / 2 - 70, 500, W / 2 + 70, 502), fill=ACCENT)
    draw_text(draw, (W / 2, 540), 'github.com/Unofficially-Online/X4-Online', font(44, 'Regular'), WHITE, tracking=1.5)
    draw_text(draw, (W / 2, 640), 'unofficial fan project  ·  not affiliated with Egosoft', font(30), SOFT, tracking=2)
    draw_text(draw, (W / 2, 690), 'gameplay: X4: Foundations 9.00', font(24), DIM, tracking=2)
    image.save(path)


def pill(draw, xy, text, size=26, anchor='left', fill=WHITE, tracking=4.0, alpha=150):
    """A small label on a dark rounded box; xy is its left (or right, or centre) top corner."""
    f = font(size, 'Regular')
    width = text_width(text, f, tracking)
    pad_x, pad_y = size * 0.62, size * 0.38
    box_w, box_h = width + 2 * pad_x, size + 2 * pad_y
    x, y = xy
    if anchor == 'right':
        x -= box_w
    elif anchor == 'center':
        x -= box_w / 2
    draw.rounded_rectangle((x, y, x + box_w, y + box_h), radius=6, fill=(6, 10, 16, alpha))
    draw.rectangle((x, y + box_h - 2, x + box_w, y + box_h), fill=ACCENT + (220,))
    draw_text(draw, (x + pad_x, y + pad_y - size * 0.08), text, f, fill, tracking=tracking, anchor='left')


def overlay(path, labels=(), caption=None, divider=(), sub=None):
    """A transparent overlay: labels [(x, y, text, anchor)], a caption at the bottom centre (sub: a smaller second
    line), dividers [x]: thin vertical lines between the pictures of a split screen."""
    image = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for x in divider:
        draw.rectangle((x - 1, 0, x + 1, H), fill=(4, 6, 10, 255))
    for x, y, text, anchor in labels:
        pill(draw, (x, y), text, anchor=anchor)
    if caption:
        f = font(40, 'Regular')
        width = text_width(caption, f, 1.5)
        box = (W / 2 - width / 2 - 34, H - 150, W / 2 + width / 2 + 34, H - 150 + (112 if sub else 72))
        draw.rounded_rectangle(box, radius=8, fill=(6, 10, 16, 170))
        draw_text(draw, (W / 2, H - 138), caption, f, WHITE, tracking=1.5)
        if sub:
            draw_text(draw, (W / 2, H - 88), sub, font(27), SOFT, tracking=1.5)
    image.save(path)


if __name__ == '__main__':
    import sys
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    title(out / 'title.png')
    end(out / 'end.png')
    overlay(out / 'demo.png', labels=[(40, 36, 'PLAYER 1', 'left'), (1000, 36, 'PLAYER 2', 'left')],
            caption='the same battle on two PCs', sub='every ship simulated on both, in step', divider=[960])
