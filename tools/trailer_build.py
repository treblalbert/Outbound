"""Edits the recorded clips into the one-minute reveal trailer.

    python tools/trailer_build.py

Reads promo/trailer/clips/*.mp4 (see trailer_record.py) plus one still, letters the
captions in the game's own font, lays the music and a few of the game's own sound
effects over it, and writes promo/trailer/outbound_trailer.mp4 (1920x1080, 30 fps).
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from trailer_font import text_image

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CLIPS = os.path.join(ROOT, "promo", "trailer", "clips")
SHOTS = os.path.join(ROOT, "promo", "shots")
WORK = os.path.join(ROOT, "promo", "trailer", "work")
OUT = os.path.join(ROOT, "promo", "trailer", "outbound_trailer.mp4")
MUSIC = os.path.join(ROOT, "assets", "Music", "Vestiges Of A Silent Mall.mp3")
SFX = os.path.join(ROOT, "assets", "Darkworld Audio - Survival Effects [Free .ogg]")
W, H, FPS = 1920, 1080, 30
CREAM = (255, 247, 228)

# The cut: clip ("@name" = a still), where in it to start, how long, the caption.
# The captions are the pitch, in the order a new player needs to hear it.
CUT = [
    ("dawn.mp4",      0.2, 4.0, "A WORLD REBUILT EVERY DAY"),
    ("loot.mp4",      0.3, 3.2, "TAKE WHAT IS LEFT OF IT"),
    ("firefight.mp4", 0.4, 4.0, "SOMEONE ALWAYS WANTS IT BACK"),
    ("rain.mp4",      0.3, 3.2, None),
    ("sealed.mp4",    0.2, 3.3, "THE HATCH SEALS AT 22:00"),
    ("night.mp4",     0.5, 4.0, "AT NIGHT THE DEAD CANNOT DIE"),
    ("bunker.mp4",    0.2, 3.7, "GET HOME. THAT IS THE WHOLE JOB."),
    ("crafter.mp4",   0.3, 3.6, "UPGRADE EVERY GUN YOU FIND"),
    ("defense.mp4",   0.3, 3.7, "BUILD THE DEFENCES"),
    ("horde.mp4",     0.2, 6.3, "THEN HOLD THE LINE"),
    ("crypt.mp4",     0.3, 3.7, "GO UNDERGROUND FOR MORE"),
    ("gate.mp4",      0.3, 2.8, None),
    ("@06_coop.png",  0.0, 5.0, "UP TO EIGHT PLAYERS, ONE WORLD"),
]
# Some shots need framing help: the bunker and its panels are drawn small in the
# middle of a big screen, and the catacombs are almost black by design.
EXTRA = {
    "bunker.mp4":  "crop=1180:664:370:200",
    "crafter.mp4": "crop=1360:765:280:150",
    "defense.mp4": "crop=1780:1000:70:40",
    "crypt.mp4":   "eq=brightness=0.05:gamma=1.18",
    "gate.mp4":    "eq=brightness=0.05:gamma=1.18",
}
FLURRY = [("firefight.mp4", 2.6), ("horde.mp4", 4.5), ("night.mp4", 2.0), ("gate.mp4", 1.4)]
FLURRY_LEN = 0.45
END_LEN = 7.5

# Where the game's own sounds land (seconds, volume).
HITS = [
    ("Combat/DesignedGunshot_Pistol2_Reverb.ogg", 7.6, 0.55),
    ("Combat/DesignedGunshot_Pistol3_Reverb.ogg", 8.4, 0.50),
    ("Combat/DesignedGunshot_Pistol1_Reverb.ogg", 9.3, 0.45),
    ("Human/HumanBreathingOut2.ogg", 14.4, 0.70),
    ("Destruction/WoodSnap3.ogg", 21.2, 0.50),
    ("Combat/DesignedGunshot_Pistol4_Reverb.ogg", 34.2, 0.55),
    ("Combat/DesignedGunshot_Pistol2_Reverb.ogg", 35.1, 0.50),
    ("Destruction/DesignedCarCrash1.ogg", 37.0, 0.40),
    ("Destruction/LargeGlassMirrorCrunch1.ogg", 45.6, 0.45),
    ("Destruction/DesignedCarCrash2.ogg", 53.4, 0.50),
]


class Job:
    """Collects ffmpeg inputs and filters, keeping the input numbering straight."""

    def __init__(self):
        self.inputs = []
        self.n = 0
        self.filters = []

    def add(self, *args):
        self.inputs.extend(args)
        self.n += 1
        return self.n - 1


def caption_png(text, path):
    """The caption, lettered like the game, on a soft dark band."""
    img = Image.new("RGBA", (W, 150), (10, 10, 14, 150))
    t = text_image(text, scale=5, color=CREAM)
    img.alpha_composite(t, ((W - t.width) // 2, (150 - t.height) // 2))
    img.save(path)


def end_card(path):
    """The cover's wordmark over black, the line, and where to get it."""
    card = Image.new("RGBA", (W, H), (12, 12, 16, 255))
    cover = Image.open(os.path.join(ROOT, "ItchIoCover.png")).convert("RGBA")
    mark = cover.crop((138, 18, 512, 104))
    mark = mark.resize((mark.width * 3, mark.height * 3), Image.NEAREST)
    card.alpha_composite(mark, ((W - mark.width) // 2, 290))
    for text, scale, color, y in [
        ("LOOT BY DAY.  HIDE BY NIGHT.", 4, (217, 200, 191), 600),
        ("FREE ON ITCH.IO", 5, CREAM, 700),
        ("BY ALBERT FREEMAN", 3, (140, 130, 150), 850),
    ]:
        t = text_image(text, scale=scale, color=color)
        card.alpha_composite(t, ((W - t.width) // 2, y))
    card.convert("RGB").save(path)


def build_video(job):
    labels = []
    total = 0.0

    def segment(src, ss, dur, still=False, zoom=False, caption=None, extra=None):
        nonlocal total
        if still:
            idx = job.add("-loop", "1", "-t", "%g" % dur, "-i", src)
        else:
            idx = job.add("-ss", "%g" % ss, "-t", "%g" % dur, "-i", src)
        f = "[%d:v]" % idx
        if extra:
            f += extra + ","
        f += "scale=%d:%d:flags=neighbor,setsar=1,fps=%d" % (W, H, FPS)
        if zoom:      # a slow push in, so a still does not sit dead on screen
            f += (",zoompan=z='min(1.0001+0.0004*on,1.07)':d=1:x='iw/2-(iw/zoom/2)'"
                  ":y='ih/2-(ih/zoom/2)':s=%dx%d:fps=%d" % (W, H, FPS))
        f += ",trim=duration=%g,setpts=PTS-STARTPTS[v%d]" % (dur, idx)
        job.filters.append(f)
        out = "[v%d]" % idx
        if caption:
            png = os.path.join(WORK, "cap%02d.png" % idx)
            caption_png(caption, png)
            cidx = job.add("-i", png)
            job.filters.append("%s[%d:v]overlay=0:%d:enable='between(t,0.35,%g)'[c%d]"
                               % (out, cidx, H - 160, dur - 0.3, idx))
            out = "[c%d]" % idx
        labels.append(out)
        total += dur

    for name, ss, dur, caption in CUT:
        still = name.startswith("@")
        src = os.path.join(SHOTS, name[1:]) if still else os.path.join(CLIPS, name)
        segment(src, ss, dur, still=still, zoom=still, caption=caption, extra=EXTRA.get(name))
    for name, ss in FLURRY:
        segment(os.path.join(CLIPS, name), ss, FLURRY_LEN, extra=EXTRA.get(name))
    card = os.path.join(WORK, "end.png")
    end_card(card)
    segment(card, 0, END_LEN, still=True)

    job.filters.append("%sconcat=n=%d:v=1:a=0[vcat]" % ("".join(labels), len(labels)))
    job.filters.append("[vcat]fade=t=in:st=0:d=1.2,fade=t=out:st=%g:d=1.5[vout]" % (total - 1.5))
    return total


def build_audio(job, total):
    labels = []
    idx = job.add("-ss", "95", "-t", "%g" % total, "-i", MUSIC)
    job.filters.append("[%d:a]volume=0.5,afade=t=in:st=0:d=2.5,afade=t=out:st=%g:d=3,"
                       "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[m]"
                       % (idx, total - 3))
    labels.append("[m]")
    for i, (path, at, vol) in enumerate(HITS):
        full = os.path.join(SFX, path)
        if not os.path.exists(full) or at > total:
            continue
        sidx = job.add("-i", full)
        job.filters.append("[%d:a]volume=%g,adelay=%d|%d,"
                           "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo[s%d]"
                           % (sidx, vol, int(at * 1000), int(at * 1000), i))
        labels.append("[s%d]" % i)
    job.filters.append("%samix=inputs=%d:normalize=0:duration=first,"
                       "loudnorm=I=-14:TP=-1.5:LRA=11[aout]"
                       % ("".join(labels), len(labels)))


if __name__ == "__main__":
    os.makedirs(WORK, exist_ok=True)
    job = Job()
    total = build_video(job)
    build_audio(job, total)
    print("trailer length: %.1fs" % total)
    cmd = ["ffmpeg", "-y", "-loglevel", "error", *job.inputs,
           "-filter_complex", ";".join(job.filters),
           "-map", "[vout]", "-map", "[aout]",
           # The game's film grain is expensive to encode; crf 21 keeps it honest
           # at a size that uploads.
           "-c:v", "libx264", "-preset", "slow", "-crf", "21", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-b:a", "192k", "-r", str(FPS), "-movflags", "+faststart", OUT]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stderr[-4000:])
        raise SystemExit("ffmpeg failed")
    print("wrote", OUT)
