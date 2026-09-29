"""Steady-state hardware counters for the isolated scan-order experiment."""
import pathlib,subprocess,os,time,json
r=pathlib.Path.cwd();e=r/'artifacts/broad-pass';out=e/'scan-counters';out.mkdir();cases=json.loads((e/'evidence/cases.json').read_text());rows=[]
for name in ['even12','held-islands12-late']:
 c=next(c for c in cases if c['id']==name)
 for n,label in enumerate(['baseline','row-scans','row-scans','baseline']):
  d=out/f'{name}-{n}-{label}';d.mkdir();fifo=d/'control';os.mkfifo(fifo);fd=os.open(fifo,os.O_RDWR|os.O_NONBLOCK);binary=r/'artifacts/pr-final/candidate' if label=='baseline' else e/'row-scans'
  cmd=['sudo','-n','perf','stat','--delay=-1','--control=fifo:'+str(fifo),'-e','instructions:u,cycles:u,cache-misses:u','-x',',','-o',str(d/'counters.csv'),'--','taskset','-c','8,10','stdbuf','-oL',str(binary),'--run-game','--load-game',str(e/'evidence'/c['save']),'--ticks',str(c['tick']+1000),'--gradient-workers','1','--gradient-delay','8','--output-dir',str(d)]
  (d/'counters.csv').touch()
  (d/'command.json').write_text(json.dumps(cmd))
  with (d/'stdout.log').open('w') as log,(d/'stderr.log').open('w') as err:
   proc=subprocess.Popen(cmd,stdout=log,stderr=err);enabled=False
   while proc.poll() is None:
    if not enabled and 'nox::game started' in (d/'stdout.log').read_text():os.write(fd,b'enable\n');enabled=True
    time.sleep(.01)
   assert proc.returncode==0 and enabled
  os.close(fd);fifo.unlink();values={}
  for line in (d/'counters.csv').read_text().splitlines():
   fields=line.split(',')
   if len(fields)>2 and fields[2] in ['instructions:u','cycles:u','cache-misses:u']:values[fields[2]]=int(fields[0])
  assert len(values)==3,values
  row={'case':name,'variant':label,'repeat':n,**values};rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2));print(row,flush=True)
