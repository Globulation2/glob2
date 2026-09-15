#!/usr/bin/env python3
"""Recreate selected bulk requests as saved maps and previews on the same platform."""
import json,os,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[3]
indices=[45,29,105,168,476,477,464,492]
labels=['small-many-omitted','small-normal','wide-many-omitted','tall-many-omitted',
        'small-zero','small-maximum','large-default','weakest-fairness']
if len(sys.argv)!=3:raise SystemExit('usage: make_samples.py BULK_RESULTS OUT')
results=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(exist_ok=False,parents=True)
requests=json.loads((results/'requests.json').read_text())
for index,label in zip(indices,labels):
 case=requests[index];dest=out/label;dest.mkdir()
 cmd=[str(root/'build/src/glob2'),'--generate-map','savannah','-d',str(root),
      '--seed',str(case['seed']),'--output',str(dest/'map.map'),
      '--preview',str(dest/'preview.png'),'--json',str(dest/'report.json')]
 for key,value in case['settings'].items():cmd+=['--set',key+'='+value]
 with tempfile.TemporaryDirectory(prefix='glob2-savannah-sample-') as profile:
  env=dict(os.environ,GLOB2_USER_DIR=profile)
  with (dest/'stdout.log').open('wb') as stdout,(dest/'stderr.log').open('wb') as stderr:
   r=subprocess.run(cmd,cwd=profile,env=env,stdout=stdout,stderr=stderr,timeout=120)
 prior=json.loads((results/f'attempt-{index:04}'/'report.json').read_text())
 saved=json.loads((dest/'report.json').read_text()) if (dest/'report.json').exists() else {}
 equal={k:prior.get(k)==saved.get(k) for k in ['terrain','underlying_terrain','resources',
                                                 'space','fertility','canonical_quality','movement']}
 manifest=dict(index=index,label=label,seed=case['seed'],request=case['settings'],
               command=cmd,returncode=r.returncode,repeatability=equal,
               map=str(dest/'map.map'),preview=str(dest/'preview.png'))
 (dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
 print(label,'exit',r.returncode,'same-report-sections',all(equal.values()))
 if r.returncode or not all(equal.values()):raise RuntimeError(label+' reproduction mismatch')
