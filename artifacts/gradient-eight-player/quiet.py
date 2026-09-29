from pathlib import Path
import subprocess,json,random,concurrent.futures,hashlib,struct,gzip
R=Path.cwd();D=R/'artifacts/gradient-eight-player';P=R/'artifacts/gradient-production';cases=json.loads((D/'cases.json').read_text());states=[];manifest=[]
for c in cases:
 if c['base_seed']!=10501:continue
 for origin in ['baseline','candidate']:
  p=D/'games'/c['id']/origin/'checkpoint-32768.game.gz'
  if not p.exists():continue
  raw=gzip.decompress(p.read_bytes());offset=8+struct.unpack('>I',raw[:4])[0];version=struct.unpack('>I',raw[offset:offset+4])[0];assert version in [119,120]
  copy=D/'quiet-inputs'/f'{c["id"]}-{origin}.game';copy.parent.mkdir(exist_ok=True)
  # Identical 119/120 byte layout; only lower the header admission gate on a copy.
  patched=raw[:offset]+struct.pack('>I',119)+raw[offset+4:];copy.write_bytes(patched)
  manifest.append({'case':c['id'],'origin':origin,'original_sha256':hashlib.sha256(raw).hexdigest(),'compatible_sha256':hashlib.sha256(patched).hexdigest(),'version_offset':offset,'original_version':version})
  states.append((c['id']+'-'+origin,copy,32768,c['generator']))
(D/'quiet-input-manifest.json').write_text(json.dumps(manifest,indent=2))
# One worker per physical core, without SMT siblings. Paired variants share a core.
cores=[0,2,4,6];blocks=[[] for _ in cores]
for i,state in enumerate(states):blocks[i%len(cores)].append(state)
def core_run(args):
 core,ss=args;rows=[]
 def execute(state,v,end,tag):
  label,path,start,family=state;out=D/'quiet'/f'{label}-{tag}-{v}';out.mkdir(parents=True,exist_ok=True)
  cmd=['taskset','-c',str(core),str(P/f'glob2-{v}'),'--run-game','--load-game',str(path),'--ticks',str(end),'--output-dir',str(out),'--replay','false']
  (out/'command.json').write_text(json.dumps(cmd))
  with (out/'stdout.log').open('w') as f:subprocess.run(['/usr/bin/time','-f','%U %S %e %M','-o',str(out/'cpu.txt'),*cmd],stdout=f,stderr=f,check=True)
  result=json.loads((out/'result.json').read_text());cpu=list(map(float,(out/'cpu.txt').read_text().split()))
  return {'state':label,'family':family,'variant':v,'core':core,'tag':tag,'ticks':result['ticks']-start,'cpu_seconds':sum(cpu[:2]),'rss_kib':cpu[3],'end':result['ticks']}
 for state in ss:
  variants=['baseline','candidate'];random.Random(state[0]+'pilot').shuffle(variants)
  pilots=[execute(state,v,state[2]+8192,'pilot') for v in variants];end=min(r['end'] for r in pilots)
  if end<=state[2]:continue
  for repeat in range(4):
   variants=['baseline','candidate'];random.Random(state[0]+str(repeat)).shuffle(variants)
   for v in variants:
    row=execute(state,v,end,str(repeat));assert row['end']==end;rows.append(row)
   (D/f'quiet-core{core}.json').write_text(json.dumps(rows,indent=2));print('QUIET',core,state[0],repeat,flush=True)
 return rows
with concurrent.futures.ThreadPoolExecutor(max_workers=len(cores)) as pool:rows=sum(pool.map(core_run,zip(cores,blocks)),[])
(D/'quiet-results.json').write_text(json.dumps(rows,indent=2));print('QUIET_DONE',len(states),len(rows),flush=True)
