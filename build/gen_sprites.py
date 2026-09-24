#!/usr/bin/env python3
# gen_sprites.py
# Turns TheLazyStone "Post Apocalypse" pixel art pack into the 16x16 sprite
# overrides that Outbound loads from assets/sprites/<name>.png.
import os, shutil, math, random
from PIL import Image, ImageDraw

random.seed(20240916)
AS = r'C:\Users\lgrhd\Desktop\Random\assets\sprites'
T = 16

# ---------------------------------------------------------------- helpers
def load(rel):
    return Image.open(os.path.join(AS, rel)).convert('RGBA')

def newcell():
    return Image.new('RGBA', (T, T), (0, 0, 0, 0))

def tile(rel, idx):
    img = load(rel)
    cols = img.size[0] // T
    x = (idx % cols) * T
    y = (idx // cols) * T
    return img.crop((x, y, x + T, y + T))

def paste_fit(dst, src, allow_up=True, pad=0, bottom=True, max_scale=None):
    src = src.crop(src.getbbox())
    w, h = src.size
    if w <= 0 or h <= 0:
        return
    maxw = maxh = T - pad * 2
    if allow_up:
        s = min(maxw / w, maxh / h)
        if max_scale is not None:
            s = min(s, max_scale)
    else:
        s = min(1.0, maxw / w, maxh / h)
    nw = max(1, int(round(w * s)))
    nh = max(1, int(round(h * s)))
    img = src.resize((nw, nh), Image.NEAREST)
    x = (T - nw) // 2
    y = (T - nh) if bottom else (T - nh) // 2
    dst.paste(img, (x, y), img)

def save(name, img):
    img.save(os.path.join(AS, name + '.png'))
    print('wrote', name)

def save_anim(name, frames):
    for i, fr in enumerate(frames):
        fr.save(os.path.join(AS, '%s_%d.png' % (name, i)))
    frames[0].save(os.path.join(AS, name + '.png'))
    print('wrote anim', name)

# Palette tuned close to the pack so synthy sprites don't clash.
C = dict(
    red=(170, 60, 52), dred=(120, 42, 36), green=(92, 130, 92), dgreen=(58, 88, 60),
    tan=(198, 172, 142), dtan=(150, 122, 95), gray=(132, 132, 138), dgray=(72, 72, 78),
    lgray=(192, 192, 198), wood=(152, 100, 60), dwood=(108, 68, 44), metal=(120, 132, 142),
    dmetal=(58, 68, 78), yellow=(212, 182, 82), orange=(202, 120, 50), blue=(92, 122, 172),
    white=(238, 238, 238), cream=(212, 206, 196), black=(26, 26, 26), darkblue=(40, 62, 96),
    teal=(96, 140, 150), wine=(120, 46, 64),
)

def P(c):
    return C[c]

def save_cell(name, drawfn):
    c = newcell()
    d = ImageDraw.Draw(c)
    drawfn(d)
    save(name, c)

# ---------------------------------------------------------------- terrain (tilesets)
BG_G  = 'Tiles/Background_Green_TileSet.png'
BG_DG = 'Tiles/Background_Dark-Green_TileSet.png'
BG_BY = 'Tiles/Background_Bleak-Yellow_TileSet.png'
ROOF  = 'Tiles/Roof_TileSet.png'
BRICK = 'Tiles/Brick-Wall_TileSet.png'
GARB  = 'Tiles/Garbage_TileSet.png'
WFENCE= 'Tiles/Wire-Fence/Wire-Fence_TileSet.png'
BD_D  = 'Tiles/Buildings/Buildings_dark_TileSet.png'
BD_G  = 'Tiles/Buildings/Buildings_gray_TileSet.png'

save('grass0', tile(BG_G, 0))
save('grass1', tile(BG_G, 2))
save('grass2', tile(BG_G, 5))
save('dirt', tile(BG_G, 21))
save('sand', tile(BG_BY, 8))
save('road', tile(BG_G, 80))
save('bridge', tile(ROOF, 9))
save('floor_wood', tile(ROOF, 8))
save('floor_concrete', tile(BG_G, 169))
save('floor_tile', tile(ROOF, 0))
save('rubble', tile(GARB, 8))
save('base_floor', tile(BG_G, 169))
save('wall_brick', tile(BRICK, 0))
save('wall_concrete', tile(BG_G, 169))
save('bunker_wall', tile(BD_D, 0))
save('boundary', tile(BD_D, 0))

# ---------------------------------------------------------------- helpers for objects
def fit_obj(rel, pad=0, bottom=True, allow_up=True, max_scale=None):
    c = newcell()
    img = load(rel)
    paste_fit(c, img, allow_up=allow_up, pad=pad, bottom=bottom, max_scale=max_scale)
    return c

def fit_icon(rel, pad=0, bottom=True, allow_up=True, max_scale=2):
    return fit_obj(rel, pad=pad, bottom=bottom, allow_up=allow_up, max_scale=max_scale)

# Wooden wall from the Buildable wall straight panel.
def wooden_wall():
    img = load(os.path.join('Objects', 'Buildable', 'Wooden', 'Wooden-wall_Horizontal.png'))
    c = newcell()
    paste_fit(c, img)
    return c
save('wall_wood', wooden_wall())

# ---------------------------------------------------------------- nature

save('tree', fit_obj(os.path.join('Objects','Nature','Green','Tree_1_Spruce_Green.png'), pad=0, bottom=True))
# A bushy broadleaf tree shades nicer - use small-oak trimmed to fit
save('bush', fit_obj(os.path.join('Objects','Nature','Green','Bush_2_Green.png'), pad=0, bottom=True))
save('rock', fit_obj(os.path.join('Objects','Nature','Flowers_Mashrooms_Other-nature-stuff','Rocks','Rock_3.png'), pad=0))
save('stump', fit_obj(os.path.join('Objects','Nature','Green','Tree-trunk_2_grass_Green.png'), pad=0, bottom=True))

# Sandbags - synthesised stack that reads clearly at 16px.
def sandbag():
    c = newcell()
    d = ImageDraw.Draw(c)
    tb, ob, hi = (210, 196, 150), (170, 150, 105), (120, 100, 70)
    def lump(x0, y0, w, h):
        d.rounded_rectangle([x0, y0, x0 + w - 1, y0 + h - 1], radius=2, fill=ob, outline=hi)
        d.rounded_rectangle([x0 + 1, y0, x0 + w - 2, y0 + h - 2], radius=2, fill=tb, outline=hi)
        d.rectangle([x0 + 2, y0 + h - 3, x0 + w - 3, y0 + h - 2], fill=hi)
    lump(1, 10, 6, 5)
    lump(8, 9, 7, 6)
    d.line([(8, 9), (9, 6)], fill=hi)
    return c
save('sandbag', sandbag())

# Fence - chain-link panel (base perimeter fences).
def fence():
    img = tile(WFENCE, 13)
    c = newcell()
    paste_fit(c, img, allow_up=False)
    return c
save('fence', fence())

# Crate - wooden crate covers the destructible world crates.
def crate():
    c = newcell()
    d = ImageDraw.Draw(c)
    w, dk, lk = (172, 128, 82), (110, 76, 48), (216, 178, 128)
    d.rectangle([2, 3, 13, 14], fill=w, outline=lk)
    for y in range(5, 13, 4):
        d.line([(0, y - 2), (15, y + 1)], fill=dk)
    d.rectangle([3, 4, 12, 13], outline=dk)
    d.line([(2, 3), (13, 12)], fill=dk)
    d.line([(13, 3), (2, 12)], fill=dk)
    return c
save('crate', crate())

# ---------------------------------------------------------------- containers
save('c_crate', fit_obj(os.path.join('Objects','Pickable','Ammo-crate_Green.png'), pad=1, allow_up=True))
save('c_locker', fit_obj(os.path.join('Objects','Container','Container_1_Gray_Vertical.png'), pad=0, bottom=True))
save('c_cabinet', fit_obj(os.path.join('Objects','Container','Container_3_Gray_Horizontal.png'), pad=0, bottom=True))
save('c_milcrate', fit_obj(os.path.join('Objects','Pickable','Ammo-crate_Red.png'), pad=1, allow_up=True))
# Ammo crates are small sprites; upscale x2 for a chunky box in the world.
def supply_crate():
    img = load(os.path.join('Objects','Pickable','Ammo-crate_Green.png'))
    img = img.resize((img.size[0]*2, img.size[1]*2), Image.NEAREST)
    c = newcell()
    paste_fit(c, img, allow_up=False)
    return c
save('c_bag', fit_obj(os.path.join('Objects','Trash-bag_2.png'), pad=0, bottom=True))
# Corpse: zombie "first death" frame - a fallen body.
def corpse():
    sheet = load('Enemies/Zombie_Small/Zombie_Small_Side-left_First-Death-Sheet6.png')
    fr = sheet.crop((0, 0, sheet.size[0] // 6, sheet.size[1]))
    c = newcell()
    paste_fit(c, fr, allow_up=False)
    return c
save('c_corpse', corpse())
save('c_toolbox', fit_obj(os.path.join('Objects','Cardboard_2.png'), pad=0, bottom=True))

# ---------------------------------------------------------------- base furniture
def bed():
    c = newcell()
    d = ImageDraw.Draw(c)
    fr, mt, pl, wh = (92, 60, 44), (150, 170, 150), (225, 228, 220), (245, 210, 170)
    d.rectangle([1, 8, 14, 14], fill=fr)          # frame
    d.rectangle([2, 9, 13, 13], fill=mt)          # mattress
    d.rectangle([6, 8, 14, 12], fill=pl)          # pillow
    d.rectangle([7, 9, 13, 11], fill=wh)          # pillow fold
    d.line([(2, 13), (13, 13)], fill=fr)
    d.line([(1, 10), (14, 10)], fill=(110, 75, 55))
    return c
save('bed', bed())
save('stash', fit_obj(os.path.join('Objects','Pickable','Ammo-crate_Blue.png'), pad=1, allow_up=True))
save('terminal', fit_obj(os.path.join('Objects','Vending-machine_Blue.png'), pad=0, bottom=True))
# Workbench: wooden bench with a small tool silhouette.
def workbench():
    c = newcell()
    d = ImageDraw.Draw(c)
    w, dw, tool = (146, 96, 58), (104, 66, 40), (196, 190, 180)
    d.rectangle([1, 9, 14, 11], fill=w, outline=dw)   # top
    d.rectangle([3, 12, 6, 14], fill=dw)              # legs
    d.rectangle([10, 12, 13, 14], fill=dw)
    d.polygon([(3, 4), (5, 4), (5, 6), (12, 6), (12, 8), (3, 8), (3, 4)], fill=tool, outline=dw)  # hammer
    d.polygon([(7, 3), (8, 3), (8, 2), (9, 2), (9, 3), (10, 3), (10, 8), (7, 8), (7, 3)], fill=tool)
    return c
save('workbench', workbench())
# Lamp: bottom "down" lantern of the street lamp.
def lamp():
    img = load('Objects/Street-Light_3_Down.png')
    w, h = img.size
    seg = img.crop((0, max(0, h - 16), w, h))
    c = newcell()
    paste_fit(c, seg, allow_up=False)
    return c
save('lamp', lamp())
save('table', fit_obj(os.path.join('Objects','Pallet_1.png'), pad=0, bottom=True))
def rug():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rounded_rectangle([2, 4, 13, 11], radius=2, fill=(150, 52, 64), outline=(196, 150, 120))
    d.line([(2, 7), (13, 7)], fill=(196, 150, 120))
    d.line([(2, 8), (13, 8)], fill=(150, 52, 64))
    d.rectangle([6, 5, 9, 10], fill=(120, 40, 50))
    return c
# ---------------------------------------------------------------- characters (6-frame idle animation)
def anim_from_sheet(rel, outname, stride=None):
    img = load(rel)
    w, h = img.size
    n = 6
    if stride is None:
        stride = w // n
    frames = []
    for i in range(n):
        x0 = i * stride
        fr = img.crop((x0, 0, min(x0 + stride, w), h))
        c = newcell()
        paste_fit(c, fr, allow_up=False, bottom=True)
        frames.append(c)
    save_anim(outname, frames)
    return frames

anim_from_sheet('Character/Main/Idle/Character_side_idle-Sheet6.png', 'player')
anim_from_sheet('Enemies/Zombie_Small/Zombie_Small_Side_Idle-Sheet6.png', 'scav')
anim_from_sheet('Enemies/Zombie_Axe/Zombie_Axe_Side_Idle-Sheet6.png', 'bandit')
anim_from_sheet('Enemies/Zombie_Big/Zombie_Big_Side_Idle-Sheet6.png', 'heavy')
anim_from_sheet('Character/Guns/Gun/Gun_side_idle-and-run-Sheet6.png', 'sniper')
anim_from_sheet('Character/Bat/Bat_side_idle-and-run-Sheet6.png', 'shade')
# Trader = the player character (leans on the terminal).
def trader():
    img = load('Character/Main/Idle/Character_side_idle-Sheet6.png').crop((0, 0, 12, 16))
    c = newcell()
    c.paste(img, (2, 0), img)
    return c
save('trader', trader())

# ---------------------------------------------------------------- effects / world bits
def white_tile():
    c = newcell()
    ImageDraw.Draw(c).rectangle([0, 0, 15, 15], fill=(255, 255, 255, 255))
    return c
save('white', white_tile())

def circle():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([2, 2, 13, 13], fill=(255, 255, 255, 255), outline=(0, 0, 0, 0))
    return c
save('circle', circle())

def ring():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([1, 1, 14, 14], outline=(255, 255, 255, 255))
    return c
save('ring', ring())

def bullet():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([4, 6, 10, 9], fill=(212, 196, 130), outline=(120, 90, 50))
    d.polygon([(4, 6), (2, 7), (2, 8), (4, 9)], fill=(212, 196, 130), outline=(120, 90, 50))
    return c
save('bullet', bullet())

def grenade():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([4, 6, 13, 13], fill=(96, 128, 96), outline=(40, 60, 42))
    d.rectangle([7, 3, 10, 5], fill=(120, 120, 128), outline=(50, 50, 56))
    d.rectangle([7, 4, 9, 4], fill=(160, 160, 168))
    d.line([(10, 4), (13, 6)], fill=(120, 120, 128))
    return c
save('grenade', grenade())

def rocket():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([6, 5, 14, 9], fill=(150, 160, 168), outline=(70, 78, 86))
    d.polygon([(14, 5), (15, 7), (14, 9)], fill=(196, 60, 50), outline=(110, 34, 30))
    d.polygon([(6, 5), (4, 3), (6, 7)], fill=(140, 148, 156))
    d.polygon([(6, 7), (4, 11), (6, 9)], fill=(140, 148, 156))
    return c
save('rocket', rocket())

def particle():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([7, 7, 8, 8], fill=(255, 255, 255, 255))
    d.rectangle([6, 6, 9, 9], fill=(255, 255, 255, 160))
    return c
save('particle', particle())

def blood(seed):
    c = newcell()
    d = ImageDraw.Draw(c)
    random.seed(seed)
    r = random.randint(0, 3)
    for _ in range(9):
        x, y = random.randint(1, 14), random.randint(1, 14)
        s = random.choice([2, 3, 3, 4])
        d.rectangle([x, y, min(15, x + s - 1), min(15, y + s - 1)], fill=(140, 40, 40))
        if random.random() < 0.4:
            d.rectangle([x, y, min(15, x + s - 1), min(15, y + s - 1)], fill=(90, 26, 26))
    return c
save('blood0', blood(11))
save('blood1', blood(22))

def scorch():
    c = newcell()
    d = ImageDraw.Draw(c)
    random.seed(99)
    for i in range(30):
        x, y = random.randint(3, 12), random.randint(3, 12)
        v = 40 + i * 3
        d.rectangle([x, y, x, y], fill=(v, v, v, 230))
    return c
save('scorch', scorch())

def crosshair():
    c = newcell()
    d = ImageDraw.Draw(c)
    wcol = (255, 255, 255, 255)
    for i in range(5):
        d.rectangle([7, i, 8, i], fill=wcol)
        d.rectangle([7, 15 - i, 8, 15 - i], fill=wcol)
        d.rectangle([i, 7, i, 8], fill=wcol)
        d.rectangle([15 - i, 7, 15 - i, 8], fill=wcol)
    d.rectangle([7, 7, 8, 8], fill=wcol)
    return c
save('crosshair', crosshair())

def arrow():
    c = newcell()
    d = ImageDraw.Draw(c)
    col = (255, 255, 255, 255)
    d.polygon([(3, 2), (14, 8), (3, 14), (6, 8)], fill=col)
    return c
save('arrow', arrow())

def home_icon():
    c = newcell()
    d = ImageDraw.Draw(c)
    col = (238, 238, 238, 255)
    d.polygon([(8, 2), (14, 7), (2, 7)], fill=col)
    d.rectangle([4, 7, 12, 13], fill=col)
    d.rectangle([6, 10, 9, 13], fill=(40, 40, 40, 255))
    return c
save('home_icon', home_icon())

def sun():
    c = newcell()
    d = ImageDraw.Draw(c)
    col = (250, 224, 120, 255)
    d.ellipse([3, 3, 12, 12], fill=col)
    for i in range(8):
        a = math.pi * 2 * i / 8
        x0 = int(8 + math.cos(a) * 7); y0 = int(8 + math.sin(a) * 7)
        x1 = int(8 + math.cos(a) * 6); y1 = int(8 + math.sin(a) * 6)
        d.line([(x1, y1), (x0, y0)], fill=(250, 224, 120, 255))
    return c
save('sun', sun())

def moon():
    c = newcell()
    d = ImageDraw.Draw(c)
    col = (232, 232, 210, 255)
    d.ellipse([3, 3, 12, 12], fill=col)
    d.ellipse([6, 2, 14, 11], fill=(28, 26, 34, 255))
    return c
save('moon', moon())

def cracks(seed):
    c = newcell()
    d = ImageDraw.Draw(c)
    random.seed(seed)
    x, y = 3, random.randint(3, 12)
    col = (40, 38, 40, 210)
    for _ in range(5):
        d.line([(x, y), (x + random.randint(1, 3), y + random.randint(0, 2))], fill=col)
        x += random.randint(1, 3); y += random.randint(0, 2)
        if random.random() < 0.4:
            d.line([(x, y), (x - 2, y + 2)], fill=col)
    return c
save('crack1', cracks(3))
save('crack2', cracks(7))
save('crack3', cracks(13))
save('rug', rug())
save('plant', fit_obj(os.path.join('Objects','Nature','Green','Bush_1_Green.png'), pad=0, bottom=True))
save('exit_ladder', fit_obj(os.path.join('Objects','Buildings','Hatch_1_Open.png'), pad=0, bottom=True))

# Extraction hatch on the ground - the open bunker hatch sprite.
save('hatch', fit_obj(os.path.join('Objects','Buildings','Hatch_1_Open.png'), pad=0, bottom=True))

# ---------------------------------------------------------------- water (synthesized, pack-neighbouring)
def make_water(deep):
    c = newcell()
    d = ImageDraw.Draw(c)
    base = (52, 84, 116) if deep else (78, 116, 148)
    light = (118, 158, 184)
    d.rectangle([0, 0, 15, 15], fill=base)
    random.seed(7 if deep else 21)
    for _ in range(10):
        x = random.randint(0, 14)
        y = random.randint(0, 14)
        d.rectangle([x, y, x, y], fill=light)
    for _ in range(4):
        x = random.randint(1, 13)
        y = random.randint(1, 13)
        d.rectangle([x, y, x + 1, y], fill=light)
    # shore edge hint
    return c
save('water0', make_water(False))
save('water1', make_water(True))
# ---------------------------------------------------------------- item icons
UI_OBJ = os.path.join('UI', 'Inventory', 'Objects')

save('i_scrap', fit_icon(os.path.join('Objects','Metal-Plates.png'), pad=0))

def i_wires():
    c = newcell()
    d = ImageDraw.Draw(c)
    for j, col in enumerate([(170,128,70), (214,160,90), (170,128,70)]):
        y = 4 + j * 4
        d.ellipse([3, y, 12, y + 3], outline=col)
    d.line([(4, 4), (11, 11)], fill=(170, 128, 70))
    d.line([(4, 8), (11, 15)], fill=(214, 160, 90))
    return c
save('i_wires', i_wires())

def i_bolts():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([3, 3, 6, 5], fill=(196,196,202), outline=(110,110,116))
    d.rectangle([4, 1, 5, 7], fill=(110,110,116))
    d.rectangle([9, 9, 13, 10], fill=(196,196,202), outline=(110,110,116))
    d.rectangle([10, 7, 12, 12], fill=(110,110,116))
    return c
save('i_bolts', i_bolts())

def i_tape():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([3, 3, 12, 12], fill=(96,96,102), outline=(50,50,56))
    d.ellipse([5, 5, 10, 10], fill=(60,60,66))
    d.ellipse([7, 7, 8, 8], fill=(30,30,34))
    return c
save('i_tape', i_tape())

def i_battery():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([5, 2, 10, 4], fill=(150,120,70), outline=(90,70,40))
    d.rectangle([3, 4, 12, 13], fill=(66,96,66), outline=(30,50,34))
    d.rectangle([6, 5, 9, 12], fill=(96,128,92))
    d.rectangle([5, 11, 11, 12], fill=(170,170,178))
    d.rectangle([6, 7, 9, 10], fill=(200,200,210))
    return c
save('i_battery', i_battery())

def i_circuit():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([3, 3, 12, 12], fill=(42,66,44), outline=(20,26,20))
    d.line([(5, 12), (5, 5), (10, 5)], fill=(150,172,90))
    d.line([(8, 8), (12, 8)], fill=(150,150,156))
    d.rectangle([9, 3, 11, 5], fill=(30,30,34))
    d.rectangle([5, 10, 7, 12], fill=(30,30,34))
    d.rectangle([8, 3, 9, 4], fill=(100,120,66))
    return c
save('i_circuit', i_circuit())

def i_watch():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([4, 3, 11, 10], fill=(206,160,80), outline=(140,100,50))
    d.ellipse([5, 4, 10, 9], fill=(236,222,190))
    d.line([(6, 6), (8, 6)], fill=(60,40,20))
    d.line([(8, 6), (8, 8)], fill=(60,40,20))
    d.rectangle([5, 11, 10, 13], fill=(150,110,55), outline=(110,80,40))
    return c
save('i_watch', i_watch())

def i_gpu():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([2, 4, 13, 11], fill=(30,40,48), outline=(10,14,18))
    d.rectangle([3, 5, 12, 10], fill=(52,70,84))
    d.rectangle([10, 5, 12, 7], fill=(130,140,150))
    d.rectangle([11, 6, 12, 7], fill=(30,30,34))
    d.line([(4, 12), (12, 14)], fill=(180,180,150))
    d.polygon([(2, 4), (13, 4), (13, 2), (2, 2)], fill=(70,88,100))
    return c
save('i_gpu', i_gpu())

def i_jewelry():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([3, 6, 12, 12], outline=(226,214,190), width=2)
    d.polygon([(7, 2), (9, 2), (8, 5)], fill=(196,90,160), outline=(150,60,120))
    d.polygon([(6, 3), (10, 3), (8, 5), (6, 5)], fill=(120,50,100))
    return c
save('i_jewelry', i_jewelry())

save('i_food', fit_icon(os.path.join('Objects','Pickable','Canned-food.png'), pad=0))
save('i_medsup', fit_icon(os.path.join('Objects','Pickable','Canned-soup.png'), pad=0))

def i_fuel():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([4, 3, 12, 6], fill=(150,60,50), outline=(90,32,28))
    d.rectangle([5, 5, 11, 14], fill=(196,80,60), outline=(110,40,32))
    d.rectangle([6, 6, 10, 12], fill=(200,96,74))
    d.line([(5, 5), (4, 3)], fill=(90,32,28))
    d.rectangle([6, 2, 9, 3], fill=(198,160,90))
    return c
save('i_fuel', i_fuel())

def i_gunparts():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([3, 3, 12, 5], fill=(150,160,168), outline=(70,78,86))
    d.rectangle([9, 6, 13, 10], fill=(120,130,138), outline=(60,66,74))
    d.rectangle([5, 8, 8, 12], fill=(100,100,106), outline=(50,50,56))
    d.rectangle([5, 6, 8, 7], fill=(110,110,116))
    d.rectangle([6, 13, 7, 14], fill=(90,90,96))
    return c
save('i_gunparts', i_gunparts())

def i_intel():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([5, 2, 10, 13], fill=(70,90,130), outline=(30,42,60))
    d.rectangle([7, 3, 9, 7], fill=(150,200,230))
    d.line([(6, 14), (9, 14)], fill=(60,80,110))
    d.rectangle([4, 9, 11, 10], fill=(50,66,96))
    return c
save('i_intel', i_intel())

save('i_bandage', fit_icon(os.path.join('Objects','Pickable','Bandage.png'), pad=0))
save('i_medkit', fit_icon(os.path.join(UI_OBJ,'Icon_First-Aid-Kit_Red.png'), pad=0))
save('i_grenade', grenade())
save('i_rocket', rocket())
save('i_ammo_light', fit_icon(os.path.join(UI_OBJ,'Icon_Bullet-box_Blue.png'), pad=0))
save('i_ammo_shell', fit_icon(os.path.join(UI_OBJ,'Icon_Bullet-box_Red.png'), pad=0))
save('i_ammo_rifle', fit_icon(os.path.join(UI_OBJ,'Icon_Bullet-box_Green.png'), pad=0))
def i_ammo_sniper():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([2, 3, 13, 12], fill=(120,118,108), outline=(70,68,62))
    d.rectangle([3, 4, 12, 11], fill=(150,146,134))
    d.line([(4, 5), (12, 8)], fill=(80,78,72))
    d.line([(5, 5), (11, 8)], fill=(80,80,76))
    return c
save('i_ammo_sniper', i_ammo_sniper())

save('i_pistol', fit_icon(os.path.join(UI_OBJ,'Icon_Pistol.png'), pad=0))
save('i_smg', fit_icon(os.path.join(UI_OBJ,'Icon_Gun.png'), pad=0))
save('i_shotgun', fit_icon(os.path.join(UI_OBJ,'Icon_Shotgun.png'), pad=0))
save('i_rifle', fit_icon(os.path.join('Objects','Pickable','Gun.png'), pad=0))

def i_sniper():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.line([(10, 5), (15, 5)], fill=(90,96,104))
    d.rectangle([12, 4, 15, 6], fill=(140,150,156), outline=(90,96,104))
    d.rectangle([1, 4, 11, 8], fill=(140,120,90), outline=(90,70,50))
    d.rectangle([4, 6, 10, 8], fill=(110,94,70), outline=(90,70,50))
    d.polygon([(1, 4), (4, 3), (4, 9), (1, 8)], fill=(150,160,168), outline=(70,78,86))
    return c
save('i_sniper', i_sniper())

def i_launcher():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([1, 6, 10, 10], fill=(70,82,90), outline=(34,40,46))
    d.rectangle([9, 5, 15, 9], fill=(110,122,132), outline=(56,62,70))
    d.rectangle([10, 6, 14, 8], fill=(140,150,158))
    d.rectangle([4, 6, 6, 10], fill=(50,58,64))
    d.rectangle([2, 8, 3, 10], fill=(196,80,60))
    return c
save('i_launcher', i_launcher())

def i_vest_light():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([2, 3, 13, 13], fill=(92,120,92), outline=(40,60,42))
    d.rectangle([5, 4, 10, 6], fill=(60,84,60))
    d.rectangle([4, 8, 6, 12], fill=(60,84,60))
    d.rectangle([9, 8, 11, 12], fill=(60,84,60))
    d.rectangle([6, 9, 9, 12], fill=(40,58,40))
    return c
save('i_vest_light', i_vest_light())

def i_vest_heavy():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([1, 2, 14, 14], fill=(72,78,84), outline=(30,34,38))
    d.rectangle([2, 4, 5, 12], fill=(130,140,148), outline=(70,78,86))
    d.rectangle([10, 4, 13, 12], fill=(120,128,138), outline=(70,74,82))
    d.rectangle([3, 7, 4, 9], fill=(60,66,76))
    d.rectangle([11, 7, 12, 9], fill=(60,64,74))
    return c
save('i_vest_heavy', i_vest_heavy())

def i_pack_small():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([4, 5, 11, 12], fill=(150,110,70), outline=(90,66,44))
    d.rectangle([5, 6, 10, 9], fill=(170,130,84))
    d.rectangle([6, 2, 9, 5], fill=(110,80,52), outline=(80,58,38))
    d.line([(4, 3), (3, 6)], fill=(110,80,52))
    d.line([(11, 3), (12, 6)], fill=(110,80,52))
    return c
save('i_pack_small', i_pack_small())

def i_pack_large():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.rectangle([2, 4, 13, 13], fill=(92,96,80), outline=(50,54,44))
    d.rectangle([3, 5, 12, 12], fill=(112,118,96))
    d.rectangle([5, 3, 10, 5], fill=(70,74,60))
    d.rectangle([6, 7, 9, 10], fill=(70,74,60))
    d.rectangle([2, 8, 13, 9], fill=(60,64,54))
    d.line([(3, 4), (4, 6)], fill=(60,64,54))
    d.line([(12, 4), (11, 6)], fill=(60,64,54))
    return c
save('i_pack_large', i_pack_large())

def i_coin():
    c = newcell()
    d = ImageDraw.Draw(c)
    d.ellipse([3, 3, 12, 12], fill=(206,156,60), outline=(140,100,40))
    d.ellipse([5, 5, 10, 10], fill=(226,186,90))
    d.rectangle([7, 4, 8, 11], fill=(160,120,50))
    return c
save('i_coin', i_coin())

print('ALL DONE')