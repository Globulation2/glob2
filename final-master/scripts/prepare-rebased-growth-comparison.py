from pathlib import Path
import json,shutil
r=Path.cwd();b=r/'artifacts/resource-growth/final-master';old=r/'artifacts/resource-growth/remaining';manifest=json.loads((old/'manifest.json').read_text());(b/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
plan={'output':'paired','repeats':10,'purpose':'Rebased production shared growth,delay8,versus exact master68aa1075; one warmup and ten alternating pairs per fixed fixture.','cases':[{'id':'master-vs-shared8','same_behavior':False,'scenarios':[s['id'] for s in manifest['scenarios']],'reference':{'name':'master','binary':str(b/'master-src/build/linux/client/release/src/glob2'),'cwd':str(b/'master-src'),'args':['--compute-threads','4']},'candidate':{'name':'rebased-shared8','binary':str(r/'build/linux/client/release/src/glob2'),'cwd':str(r),'args':['--compute-threads','4','--resource-growth-delay','8','--resource-growth-execution','shared']}}]}
(b/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
for src,dest in [('run-growth-final-pairs.py','run-rebased-growth-pairs.py'),('summarize-growth-final-pairs.py','summarize-rebased-growth-pairs.py')]:
 s=(r/'docs/.work'/src).read_text().replace('artifacts/resource-growth/remaining','artifacts/resource-growth/final-master');(r/'docs/.work'/dest).write_text(s)
