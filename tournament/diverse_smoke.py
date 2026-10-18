from pathlib import Path
import json,subprocess,sys,os
W=Path('/dev/shm/glob2-adaptive-official');A=Path('/home/bradley/glob2/artifacts/gradient-official-validation');B=W/'build/gradient-official/src/glob2'
sys.path.insert(0,str(W));from tools.resource_gradient_model import generate_map
plan=json.loads((A/'diverse-plan.json').read_text())['games']
root=A/'diverse-generation-smoke-v2';root.mkdir(exist_ok=True)
report=[]
for fmt in sorted({s['format'] for s in plan}):
 s=next(s for s in plan if s['format']==fmt);directory=root/fmt;directory.mkdir(exist_ok=True)
 actual,attempts=generate_map(B,s,directory,os.environ.copy())
 report.append(dict(spec=actual,attempts=len(attempts)))
 (root/'results.json').write_text(json.dumps(report,indent=2))
 print(fmt,actual['generator'],actual.get('width'),actual.get('height'),len(attempts),flush=True)
smoke=[min([s for s in plan if s['format']==fmt],key=lambda s:s['width']*s['height']) for fmt in ['1v1','2v2','3ffa','4ffa']]
(A/'diverse-collection-plan-v2.json').write_text(json.dumps(smoke,indent=2))
with (A/'diverse-collection-v2.log').open('w') as log:
 subprocess.run(['python3','tools/resource_gradient_model.py','run','--binary',str(B),'--plan',str(A/'diverse-collection-plan-v2.json'),'--output',str(A/'diverse-collection-smoke-v2'),'--ticks','4096','--jobs','1','--threads','1'],cwd=W,stdout=log,stderr=subprocess.STDOUT,check=True)
print('Four real collection games passed',flush=True)
