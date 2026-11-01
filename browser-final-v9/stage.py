"""Stage only explicit root-authorized, stable final release artifacts. Never builds."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,time
R=Path(__file__).resolve().parents[3];O=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def check(h):
 assert h['authorized_by']=='/root' and h['package_authorized']is True,'Explicit root package handoff required'
 allowed=h.get('authorized_source_transition',{}).get('allowed_files',[])
 changes=subprocess.check_output(['git','diff',h['revision'],'--name-only'],cwd=R,text=True).splitlines()
 assert set(changes)<=set(allowed),('Unauthorized source transition',changes)
 assert not subprocess.check_output(['git','ls-files','--others','--exclude-standard'],cwd=R,text=True).strip(),'Unexpected untracked source'
 assert h['source_files'] and h['build_files'],'Missing source/artifact identities'
 for group in ['source_files','build_files']:
  for name,digest in h[group].items():
   if group=='source_files' and name in allowed:continue
   assert sha(R/name)==digest,(group,name,'changed')
p=argparse.ArgumentParser();p.add_argument('--handoff',type=Path,required=True);a=p.parse_args();h=json.loads(a.handoff.read_text());check(h);assert not(O/'static').exists(),'Never overwrite an earlier package/evidence'
start=time.monotonic();commands=[[sys.executable,'browser/package-static.py','--source','build/emscripten/client/release','--output',str(O/'static')],[sys.executable,'browser/package-static.py','--verify',str(O/'static')]];results=[]
for i,cmd in enumerate(commands):
 with (O/('package-'+str(i)+'.log')).open('w')as f:r=subprocess.run(cmd,cwd=R,stdout=f,stderr=subprocess.STDOUT)
 results.append({'command':cmd,'returncode':r.returncode});assert r.returncode==0,'Package/verify failed'
check(h);files={p.relative_to(O/'static').as_posix():sha(p)for p in sorted((O/'static').rglob('*'))if p.is_file()};record={**h,'execution_authorized':h.get('execution_authorized',False),'package_verified':True,'package_files':files,'package_commands':results,'seconds':time.monotonic()-start};(O/'handoff.json').write_text(json.dumps(record,indent=2)+'\n');print('Verified final staged package; execution authorization:',record['execution_authorized'])
