import subprocess,pathlib,json,re
r=pathlib.Path('artifacts/who-ate-the-map').resolve();out=r/'performance';rows=[]
for round in range(2):
 for version in (['before','after'] if round==0 else ['after','before']):
  cmd=['/usr/bin/time','-p',str(out/(version+'-fixture')),str(out/'bench-profile'),'771','32','who-ate-the-map'];p=subprocess.run(cmd,capture_output=True,text=True);(out/f'{version}-{round}.log').write_text(p.stdout+p.stderr);row=dict(version=version,round=round,rc=p.returncode,timing=dict(re.findall(r'^(real|user|sys)\s+(\S+)',p.stderr,re.M)));rows.append(row);print(row,flush=True)
(out/'benchmark.json').write_text(json.dumps(rows,indent=2))
