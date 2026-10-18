from pathlib import Path
import json
c=Path(__file__).parent;out=c/'final-mac-save-continuity'
def rows(p):return {int(x.split()[0]):x for x in p.read_text().splitlines()}
checks={}
for name in ['world.checksums','snapshot.checksums']:
 a=rows(out/'control174-from512'/name);b=rows(out/'resume2048'/name);ov=set(a)&set(b)
 assert len(ov)>=2048 and all(a[t]==b[t] for t in ov)
 checks[name]={'overlap':len(ov),'divergences':0}
a=rows(c/'final-sim80-parity-mac/one/world.checksums');b=rows(out/'resume2048/world.checksums');expected=173^174
for _ in range(13):expected=((expected>>1)|(expected<<31))&0xffffffff
assert expected==1572864 and all(int(a[t].split()[1])^int(b[t].split()[1])==expected for t in set(a)&set(b))
for run in ['released173','resaved174']:
 for name in ['world.checksums','snapshot.checksums','game.replay.checksums']:
  assert (out/run/name).read_bytes()==(c/'linux-final-save-continuity'/run/name).read_bytes()
print(json.dumps({'status':'passed','matched_header174':checks,'header173_to174_constant_world_xor':expected}))
