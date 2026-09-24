import os
from PIL import Image

roots = [r'C:\Users\lgrhd\Desktop\Random\assets\sprites\Tiles',
         r'C:\Users\lgrhd\Desktop\Random\assets\sprites\Objects']
T = 16

for root in roots:
    for r, d, fs in os.walk(root):
        for f in sorted(fs):
            if not f.lower().endswith('.png'):
                continue
            p = os.path.join(r, f)
            try:
                img = Image.open(p).convert('RGBA')
            except Exception:
                continue
            w, h = img.size
            cols, rows = max(1, w // T), max(1, h // T)
            for ry in range(rows):
                for cx in range(cols):
                    x0, y0 = cx*T, ry*T
                    t = img.crop((x0, y0, x0+T, y0+T))
                    rs=gs=bs=n=0
                    for y in range(T):
                        for x in range(T):
                            px = t.getpixel((x, y))
                            if px[3] > 40:
                                rs+=px[0]; gs+=px[1]; bs+=px[2]; n+=1
                    if n == 0:
                        continue
                    R, G, B = rs//n, gs//n, bs//n
                    if B > R + 20 and B > 80 and B > G - 10:
                        rel = os.path.relpath(p, r'C:\Users\lgrhd\Desktop\Random\assets\sprites')
                        print(f'{rel} tile({cx},{ry}) rgb({R},{G},{B})')