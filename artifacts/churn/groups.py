import collections
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
binary = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else str(root / 'build/darwin/client/release/src/glob2')

def load(path):
    result = {}
    for line in Path(path).read_text().splitlines()[1:]:
        m = re.match(r'SITE calls=(\d+) bytes=(\d+)', line)
        if not m: continue
        frames = []
        for f in line.split(' |')[1:]:
            name, base, addr = f.rsplit(':', 2)
            frames.append((name, int(addr,16)-int(base,16), int(base,16), int(addr,16)))
        result[tuple((a,b) for a,b,_,_ in frames)] = (int(m[1]), int(m[2]), frames)
    return result

early, late = load(sys.argv[1]), load(sys.argv[2])
rows = []
addresses = {}
base = None
for key in early.keys() | late.keys():
    a = early.get(key, (0,0,[])); b = late.get(key, (0,0,[]))
    dc, db = b[0]-a[0], b[1]-a[1]
    if dc <= 0 and db <= 0: continue
    frames = b[2] or a[2]
    game = [f for f in frames if f[0] == binary]
    for f in game:
        addresses[f[1]] = f[3]
        base = f[2]
    rows.append((dc,db,game))

names = {}
offsets = list(addresses)
for start in range(0,len(offsets),500):
    batch = offsets[start:start+500]
    cmd = ['atos','-o',binary,'-l',hex(base),*[hex(base+x) for x in batch]]
    out = subprocess.run(cmd,capture_output=True,text=True,check=True).stdout.splitlines()
    names.update(zip(batch,out))

groups = collections.defaultdict(lambda: [0,0])
for dc,db,frames in rows:
    stack = ' '.join(names.get(f[1], '') for f in frames)
    if 'Cortex' in stack or 'AICortex' in stack:
        if 'scanWheatForbidden' in stack: label='Cortex wheat scan'
        elif 'assessAmphibious' in stack or 'assessSwim' in stack: label='Cortex water/reachability'
        elif 'placeCandidatesImpl' in stack or 'PlacementGeometry' in stack: label='Cortex placement'
        else: label='Cortex other'
    elif 'BuildingGradient' in stack or 'roundTripGradient' in stack or 'prepareBuildingGradient' in stack or 'updateGlobalGradient' in stack:
        label='Building gradients'
    elif 'compute_defense_flag_positioning' in stack: label='Nicowar defense scan'
    elif 'Team::updateAllBuildingTasks' in stack: label='Building tasks'
    elif 'gatherAndAdvanceOrders' in stack or 'getOrder' in stack or 'NetEngine::advanceStep' in stack:
        label='Order path/other AI'
    elif 'TeamStats::step' in stack: label='Team statistics'
    elif 'Heap::collectGarbage' in stack: label='Script garbage collection'
    else: label='Other'
    groups[label][0] += dc; groups[label][1] += db

total = [sum(v[i] for v in groups.values()) for i in (0,1)]
print('total site delta:', total)
for name,(calls,bytes_) in sorted(groups.items(), key=lambda kv:kv[1][1],reverse=True):
    print(f'{name:30} {calls:9} calls {bytes_:11} bytes {bytes_/total[1]*100:5.1f}%')
