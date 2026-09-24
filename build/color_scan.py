import os, sys
from PIL import Image

base = r'C:\Users\lgrhd\Desktop\Random\assets\sprites\Tiles'
T = 16

def mean_stats(t):
    rs=gs=bs=0; n=0
    for y in range(T):
        for x in range(T):
            p = t.getpixel((x,y))
            if p[3] > 40:
                rs+=p[0]; gs+=p[1]; bs+=p[2]; n+=1
    if n==0: return None
    return (rs//n, gs//n, bs//n)

def chan(v): return max(0, min(255, v))

if __name__ == '__main__':
    fn = sys.argv[1]
    p = os.path.join(base, fn)
    img = Image.open(p).convert('RGBA')
    w,h = img.size
    cols,rows = w//T, h//T
    print(f'== {fn} cols={cols} rows={rows}')
    for r in range(rows):
        for c in range(cols):
            mx = (r*cols + c)
            t = img.crop((c*T, r*T, c*T+T, r*T+T))
            m = mean_stats(t)
            if not m:
                print(f'tile{mx:3d} EMPTY')
                continue
            R,G,B = m
            sat = max(R,G,B) - min(R,G,B)
            lum = (R+G+B)//3
            # classify
            if B > R+15 and B > G+5:
                kind='WATER'
            elif G > R+10 and G >= B:
                kind='GRASS'
            elif R > B+6:
                kind = 'ORANGE' if sat > 40 else 'TAN'
            elif abs(R-G) < 8 and abs(G-B) < 8:
                kind = 'BRIGHT' if lum > 150 else 'GRAY'
            else:
                kind='MIX'
            print(f'tile{mx:3d} rgb({R:3d},{G:3d},{B:3d}) sat={sat:2d} lum={lum:3d} -> {kind}')