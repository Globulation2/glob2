from pathlib import Path
import json,os,subprocess,time,hashlib,sys
r=Path.cwd();binary=Path(sys.argv[1]).resolve();base=r/'artifacts/bulgarian/native-captures/v9-final/probes';base.mkdir(exist_ok=True);source=r;kit=r/'artifacts/bulgarian/v9-probe-input/render-input/bg';jobs=[]
assert len(sys.argv)==4 and sys.argv[2]=='--handoff'
from run_phase import guard
guard(binary,sys.argv[3])
for name,w,h in [('desktop',1280,1024),('phone',480,960)]:
 output=base/name;assert not output.exists(),'Do not overwrite probe evidence';output.mkdir();profile=output/'profile';profile.mkdir(exist_ok=True);env=dict(os.environ,LD_LIBRARY_PATH=str(r/'artifacts/bulgarian/sdl3/prefix/lib')+':'+os.environ.get('LD_LIBRARY_PATH',''),GLOB2_USER_DATA_DIR=str(profile/'user'),XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'),SDL_AUDIODRIVER='dummy');cmd=[str(binary),'matrix',str(source),str(kit),str(output),str(w),str(h)];start=time.monotonic();print('START',name,flush=True)
 with (output/'capture.log').open('w')as log:
  try:rc=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600).returncode
  except subprocess.TimeoutExpired:rc='timeout'
 jobs.append({'presentation':name,'command':cmd,'returncode':rc,'seconds':round(time.monotonic()-start,2)});(base/'execution.json').write_text(json.dumps({'identity':'Synthetic font diagnostic, not translated catalog','binary':str(binary),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'jobs':jobs},indent=2)+'\n');print('END',name,rc,flush=True)
print('COMPLETE',sum(x['returncode']==0 for x in jobs),'/',len(jobs),flush=True)
