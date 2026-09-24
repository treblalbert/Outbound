import struct, os, sys
from PIL import Image

base = r'C:\Users\lgrhd\Desktop\Random\assets\sprites'

def png_size(p):
    with open(p, 'rb') as fh:
        data = fh.read(33)
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        return struct.unpack('>I', data[16:20])[0], struct.unpack('>I', data[20:24])[0]
    return None

which = sys.argv[1] if len(sys.argv) > 1 else 'all'
folders = {'objects': 'Objects', 'character': 'Character', 'enemies': 'Enemies', 'tiles': 'Tiles'}
sel = [folders[which]] if which in folders else None

for root, dirs, files in os.walk(base):
    if sel and os.path.relpath(root, base).split(os.sep)[0] not in sel:
        continue
    for f in sorted(files):
        if not f.lower().endswith('.png'):
            continue
        p = os.path.join(root, f)
        size = png_size(p)
        if not size: continue
        rel = os.path.relpath(p, base)
        print(f'{rel}  {size[0]}x{size[1]}')