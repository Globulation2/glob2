#!/usr/bin/env python3
"""Reproduce final PR timing/trace checks from the retained inputs."""
import argparse,concurrent.futures,gzip,hashlib,json,os,pathlib,platform,resource,shutil,statistics,struct,subprocess,time
E=pathlib.Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('mode',choices=['timing','verify']);p.add_argument('--baseline',type=pathlib.Path);p.add_argument('--candidate',type=pathlib.Path,required=True);p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--cpus');p.add_argument('--jobs',type=int,default=1);p.add_argument('--repeats',type=int,default=3);a=p.parse_args();a.output=a.output.resolve();a.root=a.root.resolve();a.output.mkdir(parents=True,exist_ok=False)
bins={'candidate':a.candidate.resolve()}
if a.baseline:bins={'baseline':a.baseline.resolve(),**bins}
cases=json.loads((E/'cases.json').read_text());rows=[]
for c in cases:assert hashlib.sha256((E/c['save']).read_bytes()).hexdigest()==c['input_sha256']
metadata={'platform':platform.platform(),'compiler':subprocess.getoutput('c++ --version'),'cpu':subprocess.getoutput('lscpu' if platform.system()=='Linux' else 'sysctl -n machdep.cpu.brand_string'),'binaries':{k:{'path':str(v),'sha256':hashlib.sha256(v.read_bytes()).hexdigest()} for k,v in bins.items()},'revisions':json.loads((E/'revisions.json').read_text()),'argv':__import__('sys').argv}
(a.output/'metadata.json').write_text(json.dumps(metadata,indent=2))
def execute(c,label,name,trace=False,save=None,stop=None):
 out=a.output/name;out.mkdir();ticks=c['verify_ticks'] if trace else c['timing_ticks']
 cmd=(["taskset","-c",a.cpus] if a.cpus else [])+[str(bins[label]),'--run-game','--load-game',str(save or E/c['save']),'--ticks',str(stop or c['tick']+ticks),'--gradient-workers','1','--gradient-delay','8','--output-dir',str(out)]
 if trace:cmd+=['--telemetry','checksums','--replay','true','--save','final','--save',f'every:{c["tick"]+128}']
 (out/'command.json').write_text(json.dumps(cmd));before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic()
 with (out/'stdout.log').open('w') as f:subprocess.run(cmd,cwd=a.root,stdout=f,stderr=subprocess.STDOUT,check=True)
 wall=time.monotonic()-start;after=resource.getrusage(resource.RUSAGE_CHILDREN);r=json.loads((out/'result.json').read_text())
 row={'case':c['id'],'variant':label,'directory':name,'wall_s':wall,'cpu_s':None if trace else after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,'run_s':r['run_ns']/1e9,'ticks':r['ticks'],'gradient_workers':r['gradient_workers'],'gradient_delay':r['gradient_delay']}
 if trace:
  cs=out/'game.replay.checksums';row['trace_sha256']=hashlib.sha256(cs.read_bytes()).hexdigest();data=(out/'game.replay').read_bytes();marker=struct.pack('>HH',0,r['resolved']['save_version']);at=max(-1,len(data)-1048576);valid=[]
  with cs.open('rb') as f:count=struct.unpack_from('<I',f.read(20),12)[0]
  while True:
   at=data.find(marker,at+1)
   if at<0:break
   pos=at+4;steps=0;orders=0
   while pos+14<=len(data):
    delta,size=struct.unpack_from('>II',data,pos)
    if not 1<=size<=1048576 or delta>count or pos+13+size>len(data):break
    typ=data[pos+8];steps+=delta;pos+=13+size;orders+=1
    if pos==len(data) and steps==count and typ==51:valid.append((at+4,orders))
  assert len(valid)==1,(name,valid)
  stream=data[valid[0][0]:];(out/'orders.bin').write_bytes(stream);row.update(order_sha256=hashlib.sha256(stream).hexdigest(),trace_ticks=count,orders=valid[0][1])
  for f in [cs,out/'game.replay']:
   with f.open('rb') as src,gzip.open(str(f)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
   f.unlink()
 (out/'measurement.json').write_text(json.dumps(row,indent=2));return row,r
if a.mode=='verify':
 def verify(c):
  values=[execute(c,label,c['id']+'-'+label,True)[0] for label in bins]
  assert len({v['trace_sha256'] for v in values})==1,c['id'];assert len({v['order_sha256'] for v in values})==1,c['id'];return values
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
  for values in pool.map(verify,cases):rows+=values;print(values[0]['case'],'PASS',flush=True);(a.output/'results.json').write_text(json.dumps(rows,indent=2))
else:
 assert a.baseline and a.jobs==1
 for c in cases:
  if not c['benchmark']:continue
  reference=None
  for repeat in range(-1,a.repeats):
   labels=['baseline','candidate'] if repeat%2 else ['candidate','baseline']
   for label in labels:
    row,r=execute(c,label,f'{c["id"]}-{repeat}-{label}');row['repeat']=repeat
    core={k:r[k] for k in ['ticks','game_seed','termination','resolved','players','teams','winning_teams','winning_alliances','unresolved']}
    if reference is None:reference=core
    assert reference==core,c['id'];rows.append(row);print(row,flush=True);(a.output/'results.json').write_text(json.dumps(rows,indent=2))
