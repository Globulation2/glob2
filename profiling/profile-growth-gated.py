import subprocess,os,json,time
from pathlib import Path
root=Path.cwd();out=root/'artifacts/resource-growth/profiling/gated';out.mkdir(exist_ok=False)
scenarios=json.load(open(root/'artifacts/resource-growth/controlled-fixtures/manifest.json'))['scenarios']+json.load(open(root/'artifacts/resource-growth/fixtures/manifest-256.json'))['scenarios'];commands=[]
for name in ['dense','multi','ai512','disabled512']:
 s=next(x for x in scenarios if x['id']==name)
 for rep in range(3):
  for mode in (['legacy','shared'] if rep%2==0 else ['shared','legacy']):
   d=out/name/str(rep)/mode;d.mkdir(parents=True)
   exe=root/'artifacts/resource-growth/profiling'/('legacy-gated' if mode=='legacy' else 'candidate-gated')
   cwd=root/'artifacts/resource-growth/baseline-src' if mode=='legacy' else root
   args=list(s['args']);args[args.index('--ticks')+1]='1024'
   cmd=[str(exe),'--run-game',*args,'--compute-threads','4','--benchmark-warmup','0','--output-dir',str(d)]
   if mode!='legacy':cmd+=['--resource-growth-delay','8','--resource-growth-execution',mode]
   env=['LD_LIBRARY_PATH=/tmp/glob2-sdl3/prefix/lib']
   for prefix in ['STAT','RECORD']:
    for kind in ['CTL','ACK']:
     p=d/(prefix+'-'+kind);os.mkfifo(p);env.append(f'GLOB2_PERF_{prefix}_{kind}={p}')
   child=['sudo','-n','-u',f'#{os.getuid()}','--','env',*env,*cmd]
   perf=['sudo','-n','perf','stat','-D','-1','--control',f'fifo:{d}/STAT-CTL,{d}/STAT-ACK','-x,','-o',str(d/'counters.csv'),'-e','task-clock,cycles,instructions,context-switches,cpu-migrations,page-faults,cache-references,cache-misses','--','perf','record','-q','-D','-1','--control',f'fifo:{d}/RECORD-CTL,{d}/RECORD-ACK','-F','499','-g','--call-graph','dwarf,8192','-o',str(d/'perf.data'),'--',*child]
   with (d/'engine.log').open('w') as f:subprocess.run(perf,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
   for p in d.iterdir():
    if p.name.startswith(('STAT-','RECORD-')):p.unlink()
   for label,flags in [('flat',['--no-children']),('cumulative',['--children'])]:
    with (d/(label+'.txt')).open('w') as f:subprocess.run(['sudo','-n','perf','report','--stdio','--stdio-color','never','--percent-limit','0.05','-g','none',*flags,'-i',str(d/'perf.data')],stdout=f,stderr=subprocess.STDOUT,check=True)
   commands.append({'cwd':str(cwd),'command':perf});print(name,rep,mode,'profiled',flush=True)
   (out/'commands.json').write_text(json.dumps(commands,indent=2))
