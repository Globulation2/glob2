from pathlib import Path
import subprocess,os,json,hashlib,shutil,datetime,traceback
R=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2')
O=Path('/home/bradley/.codex/worktrees/a630/glob2/artifacts/bounded-wakeups-latest-master')
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p):return hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest()
def git(*a):return subprocess.check_output(['git',*a],cwd=R,text=True).strip()
def write(n,x):(O/n).write_text(json.dumps(x,indent=2)+'\n')
e={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
e.update(CCACHE='1',CCACHE_MAXSIZE='12G',GLOB2_SDL3_PREFIX='/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix',GLOB2_RENDER_SHADER_INPUT='/home/bradley/.codex/worktrees/execution-resource-profiles/glob2/artifacts/resource-profiles/validation/renderer-bundle-v2/shaders')
cmd=['scons','--build=build/wake-control','release=1','CC=gcc-13','CXX=g++-13','LINKFLAGS=-L/usr/lib/gcc/x86_64-linux-gnu/15','-j32','unit-tests','engine-tests','build/wake-control/src/glob2']
roles=[('baseline','origin/master'),('candidate','codex/partial-worker-wake')]
res=[]
try:
 for role,ref in roles:
  assert not git('status','--porcelain')
  subprocess.run(['git','switch','--detach',ref] if role=='baseline' else ['git','switch',ref],cwd=R,check=True)
  d=O/role;d.mkdir(exist_ok=True)
  item={'role':role,'revision':git('rev-parse','HEAD'),'base':git('rev-parse','origin/master'),'cwd':str(R),'command':cmd,'env':{k:v for k,v in e.items() if k.startswith(('CCACHE','GLOB2_'))},'started':now(),'source_hashes':{p:sha(R/p) for p in ['src/common/ComputeExecutor.h','src/common/ComputeExecutorHarness.cpp','SConstruct','test/tests.py','src/game/SimRevision.h']}}
  write('build-progress.json',item)
  with (d/'build.log').open('w') as f:result=subprocess.run(cmd,cwd=R,env=e,stdout=f,stderr=subprocess.STDOUT)
  item['rc']=result.returncode;item['finished']=now()
  assert result.returncode==0,(role,result.returncode)
  item['binaries']={}
  for p in ['src/glob2','test/glob2-unit-tests','test/glob2-engine-tests']:
   source=R/'build/wake-control'/p;target=d/source.name;shutil.copy2(source,target);item['binaries'][p]={'path':str(target),'sha256':sha(target)}
  (d/'build.json').write_text(json.dumps(item,indent=2)+'\n');res.append(item)
 write('builds-complete.json',{'state':'passed','results':res,'finished':now()})
except BaseException:
 write('build-failure.json',{'traceback':traceback.format_exc(),'utc':now()});raise
finally:
 subprocess.run(['git','switch','codex/partial-worker-wake'],cwd=R,check=True)
