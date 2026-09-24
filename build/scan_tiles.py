import os, sys
from PIL import Image

base = r'C:\Users\lgrhd\Desktop\Random\assets\sprites\Tiles'
outdir = r'C:\Users\lgrhd\Desktop\Random\build\finger'
os.makedirs(outdir, exist_ok=True)
T = 16

def classify(r, g, b):
    refs = [
        ('B', (10, 45, 75)), ('b', (60, 110, 155)), ('c', (130, 175, 200)),
        ('E', (40, 62, 32)), ('G', (72, 108, 55)), ('g', (115, 145, 88)),
        ('y', (150, 168, 100)), ('t', (168, 152, 118)), ('s', (205, 185, 145)),
        ('D', (125, 88, 62)), ('n', (75, 52, 35)), ('W', (150, 100, 60)),
        ('o', (180, 115, 55)), ('R', (150, 62, 52)), ('P', (205, 130, 150)),
        ('a', (122, 122, 122)), ('A', (176, 176, 176)), ('k', (62, 62, 62)),
        ('Z', (232, 232, 232)), ('X', (18, 18, 18)), ('M', (90, 110, 100)),
        ('u', (140, 90, 60)),
    ]
    best, bd = '?', 1e9
    for ch, (rr, gg, bb) in refs:
        d = (r-rr)**2 + (g-gg)**2 + (b-bb)**2
        if d < bd:
            bd, best = d, ch
    return best

def process(path):
    img = Image.open(path).convert('RGBA')
    w, h = img.size
    cols, rows = w // T, h // T
    all_tiles = []
    for r in range(rows):
        for c in range(cols):
            t = img.crop((c*T, r*T, c*T+T, r*T+T))
            # count by hue bucket
            counts = {}
            total = 0
            grid = []
            for g in range(8):
                row = ''
                for f in range(8):
                    x0 = int(f*T/8); x1 = int((f+1)*T/8)
                    y0 = int(g*T/8); y1 = int((g+1)*T/8)
                    ar = ag = ab = 0; n = 0
                    for yy in range(y0, y1):
                        for xx in range(x0, x1):
                            p = t.getpixel((xx, yy))
                            if p[3] > 40:
                                ar += p[0]; ag += p[1]; ab += p[2]; n += 1
                    if n:
                        ch = classify(ar//n, ag//n, ab//n)
                        counts[ch] = counts.get(ch, 0) + 1
                        total += 1
                        row += ch
                    else:
                        row += '.'
                grid.append(row)
            dom = sorted(counts.items(), key=lambda kv: -kv[1])
            all_tiles.append((r*cols+c, dom, total, grid))
    return cols, rows, all_tiles

if __name__ == '__main__':
    for fn in sorted(os.listdir(base)):
        if not fn.lower().endswith('.png'):
            continue
        p = os.path.join(base, fn)
        cols, rows, tiles = process(p)
        lines = [f'== {fn}  cols={cols} rows={rows}']
        for idx, dom, total, grid in tiles:
            doms = ' '.join(f'{ch}x{n}' for ch, n in dom[:4])
            lines.append(f'tile{idx:3d} cover={total:2d} {doms}')
        with open(os.path.join(outdir, fn + '.txt'), 'w') as f:
            f.write('\n'.join(lines))
        print(fn, 'done')