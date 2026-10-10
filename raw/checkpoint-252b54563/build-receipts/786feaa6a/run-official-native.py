import json, subprocess, hashlib, datetime
from pathlib import Path
root=Path.cwd(); directory=root/'artifacts/gpu-offload/candidates/786feaa6a'
commands=json.loads((directory/'native-commands.json').read_text())
expected=json.loads((directory/'expected-native-inventory.json').read_text())['expected_names']
rows=[]
for entry in commands:
 family=entry['family']; output=root/('artifacts/gpu-offload/native-786feaa6a-'+family); output.mkdir(exist_ok=False)
 with (output/'runner.log').open('w') as logfile: result=subprocess.run(entry['command'],cwd=root,stdout=logfile,stderr=subprocess.STDOUT)
 inventory=json.loads((output/'inventory.json').read_text()) if (output/'inventory.json').exists() else {}
 eligible=inventory.get('eligible',[]); assigned=inventory.get('assigned',[])
 row={'family':family,'exit_code':result.returncode,'command':entry['command'],'expected_count':len(expected[family]),'eligible_count':len(eligible),'assigned_count':len(assigned),'assigned_matches_expected':sorted(assigned)==sorted(expected[family]),'eligible_matches_expected':sorted(eligible)==sorted(expected[family]),'inventory_sha256':hashlib.sha256((output/'inventory.json').read_bytes()).hexdigest() if (output/'inventory.json').exists() else None,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 row['valid']=row['exit_code']==0 and row['eligible_matches_expected'] and row['assigned_matches_expected']; rows.append(row); (output/'official-receipt.json').write_text(json.dumps(row,indent=2)+'\n'); print(json.dumps(row),flush=True)
(directory/'official-native-summary.json').write_text(json.dumps(rows,indent=2)+'\n'); raise SystemExit(0 if all(r['valid'] for r in rows) else 1)
