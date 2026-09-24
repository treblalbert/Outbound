import os
from PIL import Image, ImageDraw

base = r'C:\Users\lgrhd\Desktop\Random\assets\sprites\Tiles'
outdir = r'C:\Users\lgrhd\Desktop\Random\build\sheet_preview'
os.makedirs(outdir, exist_ok=True)

T = 16
files = []
for root, dirs, fs in os.walk(base):
    for f in sorted(fs):
        if f.lower().endswith('.png'):
            files.append(os.path.join(root, f))

for p in files:
    img = Image.open(p).convert('RGBA')
    w, h = img.size
    cols, rows = w // T, h // T
    # Upscale 4x for readability with grid + index labels
    sc = 4
    canvas = Image.new('RGBA', ((w+2)*sc, (h+2)*sc), (30, 30, 40, 255))
    d = ImageDraw.Draw(canvas)
    for r in range(rows):
        for c in range(cols):
            x0, y0 = (c*T, r*T)
            tile = img.crop((x0, y0, x0+T, y0+T))
            canvas.paste(tile, ((c+1)*sc, (r+1)*sc))
            idx = r*cols + c
            d.text(((c+1)*sc + 2, (r+1)*sc + sc*T + 1), str(idx), fill=(255, 255, 0, 255))
    rel = os.path.relpath(p, base).replace('\\', '_')
    canvas.save(os.path.join(outdir, rel + '.png'))
    print(f'{rel}: cols={cols} rows={rows}')