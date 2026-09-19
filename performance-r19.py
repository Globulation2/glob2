import subprocess,resource,json,time,os
root=os.path.abspath('artifacts/portage-lakes')
with open(root+'/performance-r19.jsonl','w') as f:
 for size,teams in [(256,4),(512,12)]:
  for name,generator in [('candidate19','portage-lakes'),('candidate19','hedgerow-country'),('candidate19','drumlin-field')]:
   for telemetry in [False,True]:
    cmd=[root+'/'+name+'-glob2','--generate-map',generator,'--seed','1','--width',str(size),'--height',str(size),'--teams',str(teams),'--set','workers=8']
    if not telemetry:cmd+=['--output',root+'/performance-r19-scratch.map']
    if telemetry:cmd+=['--json',root+'/performance-'+name+'-'+generator+'-'+str(size)+'.json']
    before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.monotonic();p=subprocess.run(cmd,capture_output=True,text=True);after=resource.getrusage(resource.RUSAGE_CHILDREN)
    row=dict(binary=name,generator=generator,size=size,teams=teams,seed=1,telemetry=telemetry,wall=time.monotonic()-start,cpu=after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime,ok=p.returncode==0)
    if p.returncode:row['detail']=(p.stdout+p.stderr)[-2000:]
    f.write(json.dumps(row)+'\n');f.flush();print(row,flush=True)
