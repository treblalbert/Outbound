import os, sys
base = r'C:\Users\lgrhd\Desktop\Random\assets\sprites'
root = os.path.join(base, sys.argv[1])
for r, d, fs in os.walk(root):
    for f in sorted(fs):
        p = os.path.join(r, f)
        rel = os.path.relpath(p, base)
        print(rel)