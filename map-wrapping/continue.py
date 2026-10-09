from pathlib import Path
import subprocess,os,json,hashlib,sys
root=Path.cwd();out=root/'artifacts/map-wrapping';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2');name=sys.argv[1]
fixtures=json.loads((root/'artifacts/serial-audit/fixtures.json').read_text())['scenarios']
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
for scenario in fixtures:
 for n in (1,4):
  p=out/f'continuations/{name}/{scenario["id"]}-{n}';p.mkdir(parents=True,exist_ok=True)
  command=[str(out/f'{name}-glob2'),'--run-game','--load-game',scenario['fixture'],'--ticks',str(scenario['tick']+4096),'--compute-threads',str(n),'--benchmark-warmup','0','--output-dir',str(p),'--profile','map-wrap','--telemetry','checksums','--replay','true','--save','final']
  (p/'command.json').write_text(json.dumps(command,indent=2))
  with (p/'engine.log').open('w') as f:subprocess.run(command,cwd=tree,env=os.environ|{'GLOB2_USER_DATA_DIR':str(p/'userdata')},stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
  hashes={k:sha(p/k) for k in ('game.replay.checksums','game.replay','final.game.gz')}
  result={'fixture_sha256':sha(Path(scenario['fixture'])),'hashes':hashes}
  if name=='candidate':
   base=out/f'continuations/baseline/{scenario["id"]}-{n}'
   for k,v in hashes.items():assert v==sha(base/k),(scenario['id'],n,k)
   a=json.loads((p/'result.json').read_text());b=json.loads((base/'result.json').read_text())
   for k in ('initialChecksum','finalChecksum','ticks','gradient_delay','growth_delay'):assert a[k]==b[k],k
   result['baseline_bytes_match']=True
  (p/'verification.json').write_text(json.dumps(result,indent=2))
  print('PASS',name,scenario['id'],n,flush=True)
