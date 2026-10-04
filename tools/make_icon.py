"""Builds icon.jpg (NRO) and icon.png (store) from the Android EhViewer launcher
icon with a small Nintendo Switch console tilted 45 degrees in the corner.

Run from SwitchPort/ inside the Ehviewer_CN_SXJ checkout (needs Pillow).
"""
from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
RES = r"../app/src/main/res/mipmap-xxxhdpi"


def launcher_icon():
    bg = Image.open(RES + "/ic_launcher_background.png").convert("RGBA")
    fg = Image.open(RES + "/ic_launcher_foreground.png").convert("RGBA")
    layer = Image.alpha_composite(bg, fg)
    # Adaptive icons show the middle 72/108 of the layer.
    margin = layer.width * 18 // 108
    layer = layer.crop((margin, margin, layer.width - margin, layer.height - margin))
    return layer.resize((SIZE, SIZE), Image.LANCZOS)


def console(width):
    scale = 4
    w, h = 150 * scale, 64 * scale
    image = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    radius, joycon = 16 * scale, 30 * scale
    dark = (40, 40, 44, 255)
    d.rounded_rectangle((0, 0, joycon + radius, h - 1), radius, fill=(255, 69, 84, 255))
    d.rounded_rectangle((w - joycon - radius, 0, w - 1, h - 1), radius, fill=(0, 190, 230, 255))
    d.rectangle((joycon, 0, w - joycon, h - 1), fill=(48, 48, 52, 255))
    d.rectangle((joycon + 5 * scale, 6 * scale, w - joycon - 5 * scale, h - 6 * scale), fill=(22, 22, 26, 255))
    d.rectangle((joycon + 8 * scale, 9 * scale, w - joycon - 8 * scale, h - 9 * scale), fill=(120, 205, 190, 255))

    def dot(cx, cy, r):
        d.ellipse(((cx - r) * scale, (cy - r) * scale, (cx + r) * scale, (cy + r) * scale), fill=dark)

    dot(15, 18, 6)    # left stick
    dot(135, 44, 6)   # right stick
    for cx, cy in ((15, 37), (15, 51), (8, 44), (22, 44)):
        dot(cx, cy, 3)
    for cx, cy in ((135, 11), (135, 25), (128, 18), (142, 18)):
        dot(cx, cy, 3)
    height = width * h // w
    return image.resize((width, height), Image.LANCZOS).rotate(45, resample=Image.BICUBIC, expand=True)


def main():
    icon = launcher_icon()
    badge = console(104)
    alpha = badge.split()[3]
    outline = alpha.filter(ImageFilter.MaxFilter(5))
    shadow = alpha.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.GaussianBlur(3))
    x = y = SIZE - badge.width + 4
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    layer.paste((0, 0, 0, 90), (x + 2, y + 3), shadow)
    layer.paste((255, 255, 255, 255), (x, y), outline)
    layer.paste(badge, (x, y), badge)
    icon = Image.alpha_composite(icon, layer).convert("RGB")
    icon.save("icon.jpg", quality=95)
    icon.save("icon.png")


if __name__ == "__main__":
    main()
