from pathlib import Path
import json,subprocess,time,hashlib
c=Path(__file__).parent;root=Path('/Users/bradley/.codex/worktrees/adaptive-final-integration/glob2');out=c/'final-mac-save-continuity';out.mkdir(exist_ok=True)
while json.loads((c/'final-performance/status.json').read_text())['phase']!='complete':time.sleep(10)
binary=c/'bin/glob2-final-ca6365e83'
for name,source,ticks,threads in [('released173',c/'released173-save/final.game.gz',1024,1),('resaved174',out/'released173/final.game.gz',1280,1),('resume2048',c/'final-sim80-parity-mac/one/checkpoint-2048.game.gz',4096,4)]:
 d=out/name;d.mkdir(exist_ok=True);cmd=[str(binary),'game','run','--load-game',str(source),'--ticks',str(ticks),'--compute-threads',str(threads),'--gpu','off','--save','final','--telemetry','checksums','--output-dir',str(d)];(d/'command.json').write_text(json.dumps(cmd,indent=2))
 with (d/'run.log').open('w') as f:subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
 print(name,'complete',flush=True)
for run in ['released173','resaved174']:
 for name in ['world.checksums','snapshot.checksums','game.replay.checksums']:
  assert (out/run/name).read_bytes()==(c/'linux-final-save-continuity'/run/name).read_bytes(),(run,name,'cross-platform')
for name in ['world.checksums','snapshot.checksums']:
 def rows(p):return {int(line.split()[0]):line for line in p.read_text().splitlines()}
 a=rows(c/'final-sim80-parity-mac/one'/name);b=rows(out/'resume2048'/name);overlap=set(a)&set(b)
 assert len(overlap)>=2048,(name,len(overlap))
 assert all(a[t]==b[t] for t in overlap),(name,'resume mismatch')
print('PASS real173->174cross-platform and2048tick overlap1->4thread save resume',flush=True)
(out/'status.json').write_text(json.dumps({'status':'passed','production':'ca6365e83900e2668ff2348044a89ba3b2d70c43'}))
