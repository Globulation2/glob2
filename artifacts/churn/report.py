import collections
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
binary = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else root / 'build/darwin/client/release/src/glob2'

def read(path):
    out = {}
    for line in Path(path).read_text().splitlines()[1:]:
        m = re.match(r'SITE calls=(\d+) bytes=(\d+)', line)
        if not m:
            continue
        frames = []
        for f in line.split(' |')[1:]:
            name, base, addr = f.rsplit(':', 2)
            frames.append((name, int(addr, 16) - int(base, 16), int(base, 16), int(addr, 16)))
        key = tuple((f[0], f[1]) for f in frames)
        out[key] = (int(m[1]), int(m[2]), frames)
    return out

early = read(sys.argv[1] if len(sys.argv) > 1 else root / 'artifacts/churn/1000.tsv')
late = read(sys.argv[2] if len(sys.argv) > 2 else root / 'artifacts/churn/5000.tsv')
rows = []
for key in early.keys() | late.keys():
    a = early.get(key, (0, 0, []))
    b = late.get(key, (0, 0, []))
    dc, db = b[0]-a[0], b[1]-a[1]
    if dc > 0 or db > 0:
        rows.append((dc, db, b[2] or a[2]))

for order in ('bytes', 'calls'):
    print('\nTOP BY', order)
    for dc, db, frames in sorted(rows, key=lambda r: r[1 if order == 'bytes' else 0], reverse=True)[:25]:
        game = [f for f in frames if f[0] == str(binary)]
        if game:
            base = hex(game[0][2])
            addrs = [hex(f[3]) for f in game]
            cmd = ['atos', '-o', str(binary), '-l', base, *addrs]
            names = subprocess.run(cmd, capture_output=True, text=True).stdout.strip().splitlines()
        else:
            names = [f[0].split('/')[-1] for f in frames]
        print(f'{dc:8} calls {db:10} bytes : {" <- ".join(names[:4])}')
