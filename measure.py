import os,json,subprocess,hashlib,re,statistics,random,sys,time
from pathlib import Path
ROOT=Path.cwd(); OUT=ROOT/'artifacts/serial-opt'; AUDIT=ROOT/'artifacts/serial-audit'
scenarios=json.loads((AUDIT/'fixtures.json').read_text())['scenarios']
variants=['hiring','stats','queues','players','vectors','combined']
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(variant,s,threads,mode,location=None,affinity=None):
 dest=OUT/variant/(location or f"profiles/{s['id']}-{threads}-{mode}")
 if (dest/'result.json').exists():return json.loads((dest/'result.json').read_text())
 dest.mkdir(parents=True,exist_ok=True)
 binary=OUT/variant/'glob2'; prefix=[]
 args=['--run-game','--load-game',s['fixture'],'--ticks',str(s['tick']+(512 if mode=='callgrind' else 4096)),'--compute-threads',str(threads),'--compute-experiments','ai','--benchmark-warmup','0','--output-dir',str(dest),'--profile','serial-opt']
 if mode=='verify':args+=['--telemetry','checksums','--replay','true','--save','final']
 env=os.environ|{'GLOB2_USER_DATA_DIR':str(dest/'userdata')}
 if mode=='heaptrack':prefix=['heaptrack','-o',str(dest/'heaptrack')]
 if mode=='callgrind':prefix=['valgrind','--tool=callgrind','--instr-atstart=no','--separate-threads=yes',f'--callgrind-out-file={dest}/callgrind.out.%p']
 if mode in ('perf','counters'):
  control=dest/'control';ack=dest/'ack'
  for p in (control,ack):
   if p.exists():p.unlink()
   os.mkfifo(p,0o666);p.chmod(0o666)
  prefix=['sudo','-n','perf','record' if mode=='perf' else 'stat','--delay=-1',f'--control=fifo:{control},{ack}']
  if mode=='perf':prefix+=['-F','499','-e','cycles:u','--call-graph','dwarf,16384','-o',str(dest/'perf.data')]
  else:prefix+=['-e','cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u','-x',',','-o',str(dest/'counters.csv')]
  prefix+=['--','runuser','-u','bradley','--','env',f'GLOB2_AUDIT_CONTROL={control}',f'GLOB2_AUDIT_ACK={ack}',f"GLOB2_USER_DATA_DIR={dest/'userdata'}"]
 if mode in ('timing','perf','counters'):
  import psutil
  announced=False
  while any(p.info['name'] in ('cc1plus','cc1','wasm-opt','lto1') and p.info['status'] != psutil.STATUS_STOPPED for p in psutil.process_iter(['name','status'])):
   if not announced:print('Waiting for unrelated compilers before native measurement',flush=True);announced=True
   time.sleep(5)
 command=(['taskset','-c',affinity] if affinity else [])+prefix+[str(binary)]+args
 (dest/'command.json').write_text(json.dumps(command,indent=2))
 with (dest/'engine.log').open('w') as log:subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=3600)
 if mode in ('perf','counters'):
  subprocess.run(['sudo','-n','chown','-R',f'{os.getuid()}:{os.getgid()}',str(dest)],check=True)
  control.unlink();ack.unlink()
 if mode=='perf':
  with (dest/'report.txt').open('w') as f:subprocess.run(['perf','report','--stdio','--no-children','--sort','pid,symbol','--call-graph','none','-i',str(dest/'perf.data')],stdout=f,stderr=subprocess.DEVNULL,check=True)
 result=json.loads((dest/'result.json').read_text())
 reference=json.loads((AUDIT/f"profiles/{s['id']}-{threads}-{'callgrind' if mode=='callgrind' else 'verify'}/result.json").read_text())
 for key in ['initialChecksum','finalChecksum','ticks','gradient_delay','growth_delay']:assert result[key]==reference[key],(dest,key,result[key],reference[key])
 assert result['resolved']['rules']==reference['resolved']['rules'],dest
 if mode=='verify':
  for file in ['game.replay.checksums','game.replay','final.game.gz']:assert sha(dest/file)==sha(AUDIT/f"profiles/{s['id']}-{threads}-verify"/file),(dest,file)
 print('PASS',variant,s['id'],threads,mode,location or '',flush=True)
 return result
def timing(p):
 text=(p/'engine.log').read_text();m=re.search(r'owner_cpu_ns=(\d+) process_cpu_ns=(\d+) wall_ns=(\d+)',text);j=re.search(r'join_wait_ns=(\d+)',text)
 return dict(owner=int(m[1]),process=int(m[2]),wall=int(m[3]),join=int(j[1]))
def compare(v,s,n,count):
 values={k:[] for k in ['owner','process','wall','join']};raw=[]
 for i in range(count):
  a=timing(OUT/'baseline'/f"timings/{v}/{s['id']}-{n}-{i}");b=timing(OUT/v/f"timings/{s['id']}-{n}-{i}")
  raw.append({'baseline':a,'candidate':b})
  for k in values:
   if a[k]:values[k].append((b[k]/a[k]-1)*100)
 result={'pairs':count,'raw_ns':raw,'metrics':{}}
 for k,ratios in values.items():
  if not ratios:continue
  rng=random.Random(7349);boots=sorted(statistics.median(rng.choices(ratios,k=len(ratios))) for _ in range(10000))
  result['metrics'][k]={'paired_median_percent':statistics.median(ratios),'ci95_percent':[boots[250],boots[9749]]}
 return result
if __name__=='__main__':
 phase=sys.argv[1]
 if phase=='verify':
  for v in ['baseline']+variants:
   for s in scenarios:
    for n in (1,4):run(v,s,n,'verify')
 if phase=='timings':
  summary={}
  for v in variants:
   summary[v]={}
   for s in scenarios:
    summary[v][s['id']]={}
    for n in (1,4):
     def pairs(start,stop):
      for i in range(start,stop):
       for name in (('baseline',v) if i%2==0 else (v,'baseline')):
        loc=f"timings/{s['id']}-{n}-{i}"
        if name=='baseline':loc=f"timings/{v}/{s['id']}-{n}-{i}"
        run(name,s,n,'timing',loc)
     pairs(0,10);r=compare(v,s,n,10)
     if any(r['metrics'][k]['paired_median_percent']>2 for k in ('owner','wall')):
      pairs(10,20);r=compare(v,s,n,20)
     summary[v][s['id']][str(n)]=r
     (OUT/'paired-timings.json').write_text(json.dumps(summary,indent=2))
 if phase in ('profiles','counts','native-profiles'):
  for v in ['baseline']+variants:
   for s in scenarios:
    for n in (4,1):
     for mode in (('heaptrack','callgrind') if phase=='counts' else ('perf','counters') if phase=='native-profiles' else ('perf','counters','heaptrack','callgrind')):run(v,s,n,mode)
