import pathlib,subprocess,json,os,re
r=pathlib.Path('artifacts/who-ate-the-map').resolve();out=r/'performance';rows=[];env=dict(os.environ,GLOB2_USER_DIR=str(r/'profile'))
for seed in [1,7,11]:
 for trace in [False,True]:
  for version,generator in [('v10','who-ate-the-map'),('final','who-ate-the-map'),('final','continents')]:
   args=[str(r/version/'glob2'),'--generate-map',generator,'--width','512','--height','512','--teams','8','--seed',str(seed),'--output',str(out/'bench.map')]
   if trace:args+=['--json',str(out/'bench.json')]
   p=subprocess.run(['/usr/bin/time','-p']+args,capture_output=True,text=True,env=env);row=dict(seed=seed,json_report=trace,version=version,generator=generator,rc=p.returncode,timing=dict(re.findall(r'^(real|user|sys)\s+(\S+)',p.stderr,re.M)));rows.append(row);print(row,flush=True)
(out/'largest.json').write_text(json.dumps(rows,indent=2));(out/'bench.map').unlink(missing_ok=True);(out/'bench.json').unlink(missing_ok=True)
