from pathlib import Path
import subprocess,os,json
r=Path.cwd();b=r/'artifacts/resource-growth/final-master';results={}
for scenario in json.loads((b/'manifest.json').read_text())['scenarios']:
 name=scenario['id'];results[name]={}
 for variant in ['master','owner','shared']:
  d=b/'correctness'/name/variant
  with (d/'inspect.log').open('w') as f:
   subprocess.run([str(b/'final-inspector'),'--test-case=inspect remaining experiment saves*','--no-skip'],env=dict(os.environ,GLOB2_REMAINING_INSPECT_INPUT=str(d/'final.game'),GLOB2_REMAINING_INSPECT_OUTPUT=str(d/'stocks.json')),stdout=f,stderr=subprocess.STDOUT,check=True)
  results[name][variant]=json.loads((d/'stocks.json').read_text())
 assert results[name]['owner']==results[name]['shared'],name
 print(name,results[name],flush=True)
(b/'correctness/accepted-work.json').write_text(json.dumps(results,indent=2)+'\n')
