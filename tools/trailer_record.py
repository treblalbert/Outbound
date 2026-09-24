"""Records the trailer's raw clips by driving the game with its dev flags.

    python tools/trailer_record.py            # every clip
    python tools/trailer_record.py horde      # only clips whose name matches

Each clip runs the game headless-ish at 1920x1080, steps at a fixed 30 fps
(--record) and pipes its frames into ffmpeg. Output: promo/trailer/clips/NN_name.mp4
"""
import os
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BIN = os.path.join(ROOT, "bin")
OUT = os.path.join(ROOT, "promo", "trailer", "clips")
EXE = os.path.join(BIN, "Outbound.exe")
WIN = "--window=1920x1080"

# name, args, when to start recording (seconds of game time), how long to record.
# NOTE: in the bunker G.realTime runs at double speed, so a bunker clip asks for twice
# the seconds it ends up with.
CLIPS = [
    ("dawn",      ["--raid", "--promo", "--nohud", "--bot-far", "--time=400", "--weather=6"], 3.0, 4.0),
    ("loot",      ["--raid", "--promo", "--nohud", "--atbuilding", "--time=700", "--godmode", "--bot"], 3.0, 3.5),
    ("firefight", ["--raid", "--promo", "--nohud", "--enemies", "--bot", "--time=760"], 4.0, 4.5),
    ("rain",      ["--raid", "--promo", "--nohud", "--weather=5", "--atpuddle", "--bot"], 3.5, 3.5),
    ("night",     ["--raid", "--promo", "--nohud", "--time=1340", "--godmode", "--bot"], 4.0, 4.5),
    ("bunker",    ["--base", "--tutstep=-1", "--squad", "--money=9000"], 3.0, 8.0),
    ("crafter",   ["--base", "--panel=crafter", "--elite", "--money=9000"], 2.5, 8.0),
    ("defense",   ["--defense"], 2.5, 4.0),
    ("horde",     ["--raid", "--promo", "--nohud", "--horde", "--zombies=90", "--godmode"], 9.3, 6.5),
    ("crypt",     ["--raid", "--promo", "--nohud", "--crypt=gate", "--godmode", "--bot"], 3.0, 4.0),
    ("gate",      ["--raid", "--promo", "--nohud", "--crypt=gateopen"], 2.2, 3.2),
    ("sealed",    ["--raid", "--promo", "--nohud", "--athatch", "--time=1338", "--godmode"], 3.0, 3.5),
]

# The co-op clip needs two instances. The guest records (it sees the host's name tag
# over them); the host wanders about in front of it.
COOP = ("coop", ["--lan-host", "--nohud", "--go-out=3", "--godmode"], ["--lan-join", "--go-out=3", "--godmode", "--bot"], 9.5, 4.0)


def run(name, args, start, length, extra_wait=60):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".mp4")
    cmd = [EXE, "--mute", "--nograin", WIN, *args, "--record=%s@%g@%g" % (path.replace("\\", "/"), start, length)]
    print("  ", name, " ".join(args))
    t0 = time.time()
    subprocess.run(cmd, cwd=BIN, timeout=start + length + extra_wait,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ok = os.path.exists(path) and os.path.getsize(path) > 10000
    print("     %s (%.0fs)" % ("ok" if ok else "FAILED", time.time() - t0))
    return ok


def run_coop():
    name, host_args, guest_args, start, length = COOP
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".mp4")
    # Half size: recording slows the host's frames, and too slow drops the guest.
    host = subprocess.Popen([EXE, "--mute", "--nograin", "--window=960x540", *host_args,
                             "--record=%s@%g@%g" % (path.replace("\\", "/"), start, length)],
                            cwd=BIN, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    guest = subprocess.Popen([EXE, "--mute", "--window=640x360", *guest_args],
                             cwd=BIN, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        host.wait(timeout=start + length + 90)
    finally:
        for p in (guest, host):
            if p.poll() is None:
                p.terminate()
    ok = os.path.exists(path) and os.path.getsize(path) > 10000
    print("   coop %s" % ("ok" if ok else "FAILED"))
    return ok


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, args, start, length in CLIPS:
        if only and only not in name:
            continue
        run(name, args, start, length)
    if not only or only in "coop":
        run_coop()
