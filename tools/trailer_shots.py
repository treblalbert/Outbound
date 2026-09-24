"""Takes the marketing screenshots (1920x1080) with the game's dev flags.

    python tools/trailer_shots.py

Writes promo/shots/NN_name.png. The co-op one runs two instances over LAN.
"""
import os
import subprocess
import sys
import time

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BIN = os.path.join(ROOT, "bin")
OUT = os.path.join(ROOT, "promo", "shots")
EXE = os.path.join(BIN, "Outbound.exe")
TMP = os.path.join(BIN, "_shot.bmp")

SHOTS = [
    ("01_night_horde", ["--raid", "--promo", "--athatch", "--time=1338", "--godmode"], 4.0),
    ("02_catacombs",   ["--raid", "--promo", "--crypt=door", "--time=1200", "--weather=2", "--godmode"], 3.0),
    ("07_crypt_inside", ["--raid", "--promo", "--crypt=seen", "--godmode", "--day=12"], 3.0),
    ("03_crafter",     ["--base", "--panel=crafter", "--elite", "--money=9000"], 2.5),
    ("04_rain",        ["--raid", "--promo", "--weather=7", "--atbuilding", "--time=1150", "--godmode", "--enemies"], 4.0),
    ("05_horde_day",   ["--raid", "--promo", "--horde", "--zombies=90", "--godmode"], 11.5),
]

COOP = ("06_coop", ["--lan-host", "--go-out=3", "--godmode"], ["--lan-join", "--go-out=3", "--godmode", "--bot"], 10.0)


def save(name):
    if not os.path.exists(TMP):
        print("   ", name, "FAILED")
        return False
    os.makedirs(OUT, exist_ok=True)
    Image.open(TMP).save(os.path.join(OUT, name + ".png"))
    os.remove(TMP)
    print("   ", name, "ok")
    return True


def shot(name, args, at, window="--window=1920x1080"):
    if os.path.exists(TMP):
        os.remove(TMP)
    subprocess.run([EXE, "--mute", "--nograin", window, *args, "--shot=_shot.bmp@%g" % at], cwd=BIN,
                   timeout=at + 90, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return save(name)


def shot_coop():
    name, host_args, guest_args, at = COOP
    if os.path.exists(TMP):
        os.remove(TMP)
    host = subprocess.Popen([EXE, "--mute", "--nograin", "--window=1920x1080", *host_args, "--shot=_shot.bmp@%g" % at],
                            cwd=BIN, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3)
    guest = subprocess.Popen([EXE, "--mute", "--window=640x360", *guest_args],
                             cwd=BIN, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        host.wait(timeout=at + 90)
    finally:
        for p in (guest, host):
            if p.poll() is None:
                p.terminate()
    return save(name)


if __name__ == "__main__":
    only = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, args, at in SHOTS:
        if not only or only in name:
            shot(name, args, at)
    if not only or only in COOP[0]:
        shot_coop()
