import os,signal,subprocess,json,shlex,sys
from pathlib import Path
root=Path.cwd();out=root/'artifacts/memory-optimization/peak-followup'
rows=[line.strip().split(None,2) for line in subprocess.check_output(['ps','-axo','pid,ppid,command'],text=True).splitlines()[1:]]
parents=[int(pid) for pid,ppid,cmd in rows if 'tools/memory_benchmark.py --baseline artifacts/memory-optimization/peak-followup/baseline/glob2' in cmd and '--output artifacts/memory-optimization/peak-followup/cpu-final' in cmd and Path(shlex.split(cmd)[0]).name.lower().startswith('python')]
if len(parents)!=1:raise RuntimeError(f'expected one owned CPU runner, found {parents}')
parent=parents[0];children=[]
def stop(signum,_):raise SystemExit(128+signum)
signal.signal(signal.SIGTERM,stop)
try:
 os.kill(parent,signal.SIGSTOP)
 rows=[line.strip().split(None,2) for line in subprocess.check_output(['ps','-axo','pid,ppid,command'],text=True).splitlines()[1:]]
 children=[int(pid) for pid,ppid,cmd in rows if int(ppid)==parent]
 for pid in children:os.kill(pid,signal.SIGSTOP)
 commands=json.loads((out/'phase-probe-commands.json').read_text())
 compile=commands['compile'];compile[compile.index('-o')+1]=str(out/'capture_probe.o');compile[-1]=str(out/'capture_probe.cpp')
 link=commands['link'];link[link.index('-o')+1]=str(out/'capture-probe');link=[str(out/'capture_probe.o') if x.endswith('/phase_probe.o') else x for x in link]
 (out/'capture-probe-commands.json').write_text(json.dumps({'compile':compile,'link':link,'paused_parent':parent,'paused_children':children},indent=2))
 with (out/'build-capture-probe.log').open('w') as log:
  subprocess.run(compile,stdout=log,stderr=subprocess.STDOUT,check=True)
  subprocess.run(link,stdout=log,stderr=subprocess.STDOUT,check=True)
 env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env.pop('MallocStackLogging',None)
 with (out/'capture-probe.log').open('w') as log:
  subprocess.run([str(out/'capture-probe'),str(root/'artifacts/memory-profile/game/checkpoint-45000.game.gz')],stdout=log,stderr=subprocess.STDOUT,env=env,check=True)
finally:
 # Leave the children stopped; the runner's normal next slice resumes each one.
 os.kill(parent,signal.SIGCONT)
 print('Resumed paired CPU runner',parent,flush=True)
sys.path.insert(0,str(root/'tools'))
from memory_benchmark import confidence
values=[json.loads(line[8:]) for line in (out/'capture-probe.log').read_text().splitlines() if line.startswith('CAPTURE ')]
summary={'condition':'same loaded engine; 7 alternating triplets; no injected scheduler pauses; reserve included in capture time','raw':values,'cpu':{},'wall':{}}
for label in ['growing','reserved']:
 for key,metric in [('cpu','cpu_ns'),('wall','wall_ns')]:
  summary[key][label]=confidence([next(v[metric] for v in values if v['pair']==p and v['variant']=='chunked')/next(v[metric] for v in values if v['pair']==p and v['variant']==label) for p in range(7)])
summary['passed']=all(c['upper_95_percent']<=2 for kind in ['cpu','wall'] for c in summary[kind].values())
(out/'capture-comparison.json').write_text(json.dumps(summary,indent=2));print(summary,flush=True)
