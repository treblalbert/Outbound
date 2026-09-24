import os, sys
from PIL import Image

base = r'C:\Users\lgrhd\Desktop\Random\assets\sprites\Character\Main'
T = 16

def frame_profile(p):
    img = Image.open(p).convert('RGBA')
    w, h = img.size
    # column alpha sum
    empty_cols = []
    for x in range(w):
        s = sum(1 for y in range(h) if img.getpixel((x, y))[3] > 10)
        empty_cols.append(s == 0)
    # find runs of non-empty (vertical separator analysis)
    runs = []
    start = None
    for x in range(w):
        e = empty_cols[x]
        if not e and start is None:
            start = x
        elif e and start is not None:
            runs.append((start, x - 1))
            start = None
    if start is not None:
        runs.append((start, w - 1))
    print(f'{os.path.basename(p)}: {w}x{h}  runs={runs}')

if __name__ == '__main__':
    for r, d, fs in os.walk(base):
        for f in sorted(fs):
            if f.endswith('.png') and 'run' in f.lower() or (r.endswith('Idle') and f.endswith('.png')) or (r.endswith('Run') and f.endswith('.png')):
                frame_profile(os.path.join(r, f))