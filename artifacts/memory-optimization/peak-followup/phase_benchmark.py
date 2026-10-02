import json,os,sys,subprocess,hashlib
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
from memory_benchmark import run_pair,confidence,digest
root=Path.cwd();out=root/'artifacts/memory-optimization/peak-followup';binary=out/'phase-probe';save=root/'artifacts/memory-profile/game/checkpoint-45000.game.gz'
summary={'binary_sha256':digest(binary),'fixture_sha256':digest(save),'condition':'0.1 second interleaved processes; same engine, contiguous versus chunked deferred capture','pairs':[]}
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env.pop('MallocStackLogging',None)
for pair in range(21):
 directories={label:out/f'phase-pair-{pair}-{label}' for label in ['baseline','candidate']}
 commands={}
 for label,d in directories.items():
  d.mkdir(exist_ok=True);commands[label]=[str(binary),str(save),str(d/'final.game.gz')]
  if label=='baseline':commands[label].append('legacy')
 order=('baseline','candidate') if pair%2==0 else ('candidate','baseline')
 run_pair(commands,directories,order,env,.1)
 values={}
 for label,d in directories.items():
  phases=[json.loads(line[6:]) for line in (d/'run.log').read_text().splitlines() if line.startswith('PHASE ')]
  values[label]={p['phase']:p for p in phases}
  values[label]['save_sha256']=digest(d/'final.game.gz');values[label]['command']=commands[label]
  values[label]['total_cpu_ns']=sum(values[label][p]['cpu_ns'] for p in ['capture','hash','compression'])
 if values['baseline']['save_sha256']!=values['candidate']['save_sha256']:raise RuntimeError('phase snapshot bytes differ')
 summary['pairs'].append(values)
 summary['cpu']={phase:confidence([p['candidate'][phase]['cpu_ns']/p['baseline'][phase]['cpu_ns'] for p in summary['pairs']]) for phase in ['capture','hash','compression']}
 summary['cpu']['total']=confidence([p['candidate']['total_cpu_ns']/p['baseline']['total_cpu_ns'] for p in summary['pairs']])
 summary['capture_wall']=confidence([p['candidate']['capture']['wall_ns']/p['baseline']['capture']['wall_ns'] for p in summary['pairs']])
 summary['passed']=summary['cpu']['total']['upper_95_percent']<=2 and summary['cpu']['capture']['upper_95_percent']<=2 and summary['capture_wall']['upper_95_percent']<=2
 (out/'phase-comparison.json').write_text(json.dumps(summary,indent=2));print(pair+1,summary['cpu'],summary['capture_wall'],flush=True)
 if pair:
  for d in directories.values():(d/'final.game.gz').unlink()
 if pair>=6 and summary['passed']:break
sys.exit(0 if summary['passed'] else 1)
