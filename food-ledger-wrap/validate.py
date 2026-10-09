from pathlib import Path
import subprocess,os,time,json,re,hashlib,statistics,random
root=Path.cwd();out=root/'artifacts/food-ledger-wrap';audit=root/'artifacts/serial-audit';tree=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2')
while not (out/'build.exit').exists():time.sleep(2)
assert (out/'build.exit').read_text().strip()=='0'
cmd=['python3','test/run_tests.py','--build-dir',str(tree/'build/serial-opt-gcc13'),'--binary','unit','--filter','Maxima.*/*','--junit',str(out/'tests.xml')]
(out/'test-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'tests.log').open('w') as f:subprocess.run(cmd,cwd=tree,stdout=f,stderr=subprocess.STDOUT,check=True)
print('PASS Maxima unit suites',flush=True)
scenarios={s['id']:s for s in json.loads((audit/'fixtures.json').read_text())['scenarios']}
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(name,fixture,n,mode,i=0):
 s=scenarios[fixture];p=out/f'{mode}/{fixture}-{n}-{i}-{name}';p.mkdir(parents=True,exist_ok=True)
 binary=out/'glob2' if name=='candidate' else root/'artifacts/serial-reanalysis/glob2'
 command=[str(binary),'--run-game','--load-game',s['fixture'],'--ticks',str(s['tick']+4096),'--compute-threads',str(n),'--benchmark-warmup','0','--output-dir',str(p),'--profile','food-wrap']
 if mode=='verify':command+=['--telemetry','checksums','--replay','true','--save','final']
 (p/'command.json').write_text(json.dumps(command,indent=2))
 import psutil
 (p/'host-load.json').write_text(json.dumps({'time':time.time(),'load':os.getloadavg(),'cpu':psutil.cpu_percent(interval=.1,percpu=True),'active_compilers':sum(t.info['name'] in ('cc1plus','cc1','lto1','wasm-opt') and t.info['status']!=psutil.STATUS_STOPPED for t in psutil.process_iter(['name','status']))},indent=2))
 with (p/'engine.log').open('w') as f:subprocess.run(command,cwd=tree,env=os.environ|{'GLOB2_USER_DATA_DIR':str(p/'userdata')},stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
 r=json.loads((p/'result.json').read_text());ref=json.loads((audit/f'profiles/{fixture}-1-verify/result.json').read_text())
 for k in ('initialChecksum','finalChecksum','ticks','gradient_delay','growth_delay'):assert r[k]==ref[k],(p,k)
 if mode=='verify':
  hashes={f:sha(p/f) for f in ('game.replay.checksums','game.replay','final.game.gz')}
  for f,h in hashes.items():assert h==sha(audit/f'profiles/{fixture}-{n}-verify'/f),(p,f)
  (p/'verification.json').write_text(json.dumps({'baseline':'da57b459f1eb20b9b4a08d25eb502711dd81986b','all_bytes_match':True,'hashes':hashes},indent=2))
 log=(p/'engine.log').read_text();m=re.search(r'owner_cpu_ns=(\d+) process_cpu_ns=(\d+) wall_ns=(\d+)',log);j=re.search(r'join_wait_ns=(\d+)',log)
 v={'owner':int(m[1]),'process':int(m[2]),'wall':int(m[3]),'join':int(j[1])};v['worker']=max(0,v['process']-v['owner'])
 print('PASS',mode,name,fixture,n,i,flush=True);return v
for s in ('established','dense'):
 for n in (4,1):run('candidate',s,n,'verify')
pairs=[]
for i in range(10):
 row={}
 for name in (('baseline','candidate') if i%2==0 else ('candidate','baseline')):row[name]=run(name,'established',4,'timing',i)
 pairs.append(row);(out/'pairs.json').write_text(json.dumps(pairs,indent=2))
summary={'fixture':'established','ticks':4096,'participants':4,'pairs':len(pairs),'metrics':{}}
for metric in ('owner','worker','process','wall','join'):
 ratios=[(r['candidate'][metric]/r['baseline'][metric]-1)*100 for r in pairs if r['baseline'][metric]]
 rng=random.Random(7349);boots=sorted(statistics.median(rng.choices(ratios,k=len(ratios))) for _ in range(10000))
 summary['metrics'][metric]={'median_percent':statistics.median(ratios),'ci95_percent':[boots[250],boots[9749]]}
(out/'timing-summary.json').write_text(json.dumps(summary,indent=2))
print('Validation and paired screen complete',flush=True)
