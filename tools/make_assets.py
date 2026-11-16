# Generates the font atlas, app icons and launch images.
#   python -I tools/make_assets.py <Resources dir>
# Font: Pillow's built-in default font (Aileron Regular, CC0 public domain).
import json, math, os, random, sys
from PIL import Image, ImageDraw, ImageFont

res = sys.argv[1]
os.makedirs(os.path.join(res, "font"), exist_ok=True)

# ---------------- font atlas (alpha only, 512x256) ----------------
SIZE = 30
font = ImageFont.load_default(size=SIZE)
AW, AH = 512, 256
atlas = Image.new("L", (AW, AH), 0)
d = ImageDraw.Draw(atlas)
d.rectangle([0, 0, 3, 3], fill=255)  # solid white block for filled shapes
ascent, descent = font.getmetrics()
glyphs = {}
x, y, rowh = 6, 0, 0
for code in range(32, 127):
    ch = chr(code)
    l, t, r, b = font.getbbox(ch)
    adv = font.getlength(ch)
    w, h = max(0, r - l), max(0, b - t)
    if x + w + 2 > AW:
        x, y = 0, y + rowh + 2
        rowh = 0
    if w > 0 and h > 0:
        d.text((x - l, y - t), ch, font=font, fill=255)
    glyphs[str(code)] = [x, y, w, h, l, t, round(adv, 2)]
    x += w + 2
    rowh = max(rowh, h)
assert y + rowh <= AH, "atlas too small"
atlas.tobytes()
with open(os.path.join(res, "font", "font.bin"), "wb") as f:
    f.write(atlas.tobytes())
with open(os.path.join(res, "font", "font.json"), "w") as f:
    json.dump({"size": SIZE, "ascent": ascent, "descent": descent, "atlasW": AW, "atlasH": AH,
               "glyphs": glyphs}, f, separators=(",", ":"))

# ---------------- icon ----------------
def icon(px):
    S = 512
    img = Image.new("RGB", (S, S), (24, 52, 92))
    dr = ImageDraw.Draw(img)
    rnd = random.Random(7)
    cols = [(214, 69, 65), (65, 131, 215), (46, 204, 113), (241, 196, 15), (155, 89, 182)]
    cells = 9
    cs = S / cells
    # rough continent blob filled with territory squares
    for gy in range(cells):
        for gx in range(cells):
            cx, cy = (gx + 0.5) * cs - S / 2, (gy + 0.5) * cs - S / 2
            r = math.hypot(cx * 1.0, cy * 1.25)
            if r > S * 0.44 + rnd.uniform(-30, 30):
                continue
            ang = math.atan2(cy, cx)
            c = cols[int((ang + math.pi) / (2 * math.pi) * 5) % 5]
            if r < 70:
                c = (236, 240, 241)
            dr.rectangle([gx * cs + 3, gy * cs + 3, (gx + 1) * cs - 3, (gy + 1) * cs - 3], fill=c)
    # flag in the middle
    dr.rectangle([246, 150, 262, 360], fill=(40, 40, 40))
    dr.polygon([(262, 150), (370, 185), (262, 220)], fill=(214, 69, 65))
    return img.resize((px, px), Image.LANCZOS)

for name, px in [("Icon-76", 76), ("Icon-76@2x", 152), ("Icon-40", 40), ("Icon-40@2x", 80),
                 ("Icon-29", 29), ("Icon-29@2x", 58), ("Icon-60", 60), ("Icon-60@2x", 120),
                 ("Icon-83.5@2x", 167)]:
    icon(px).save(os.path.join(res, name + ".png"))
icon(512).save(os.path.join(res, "..", "tools", "previews", "icon.png"))

# ---------------- launch images (landscape iPad) ----------------
for name, (w, h) in [("Default-Landscape~ipad", (1024, 768)), ("Default-Landscape@2x~ipad", (2048, 1536))]:
    img = Image.new("RGB", (w, h), (18, 28, 44))
    img.save(os.path.join(res, name + ".png"))
print("assets ok, glyph rows end at", y + rowh)
