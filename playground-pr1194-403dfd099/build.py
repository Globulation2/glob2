from pathlib import Path
import json,os,subprocess,hashlib,datetime,shutil,platform
O=Path(__file__).resolve().parent;R=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2')
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(n,v):(O/n).write_text(json.dumps(v,indent=2)+'\n')
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def git(*a):return subprocess.check_output(['git',*a],cwd=R,text=True).strip()
assert git('rev-parse','HEAD')=='403dfd0999a2511cbd84f9869403c8960b991f26' and not git('status','--porcelain','--untracked-files=no')
env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(CCACHE='1',CCACHE_MAXSIZE='12G',GLOB2_SDL3_PREFIX='/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci/prefix',GLOB2_RENDER_SHADER_INPUT='/home/bradley/.codex/worktrees/execution-resource-profiles/glob2/artifacts/resource-profiles/validation/renderer-bundle-v2/shaders')
cmd=['scons','--build=build/wake-control','release=1','CC=gcc-13','CXX=g++-13','LINKFLAGS=-L/usr/lib/gcc/x86_64-linux-gnu/15','-j32','build/wake-control/src/glob2']
r={'revision':git('rev-parse','HEAD'),'cwd':str(R),'command':cmd,'env':{k:v for k,v in env.items() if k.startswith(('GLOB2_','CCACHE'))},'started':now(),'source_status':git('status','--porcelain','--untracked-files=no'),'staged_fixture_status':git('status','--porcelain'),'os':platform.platform(),'cpu_allocation':sorted(os.sched_getaffinity(0)),'compiler':subprocess.check_output(['g++-13','--version'],text=True),'input_hashes':{p:sha(R/p) for p in ['SConstruct','src/SConscript','vcpkg.json','scons/sdl3-versions.json','src/game/SimRevision.h','src/app/Version.h','src/common/ComputeExecutor.h','src/common/BitArray.h','libgag/include/Stream.h','src/game/GameChecksum.cpp'] if (R/p).exists()}}
save('build-started.json',r)
with (O/'build.log').open('w') as f:rc=subprocess.run(cmd,cwd=R,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
r.update(returncode=rc,finished=now());assert rc==0,r
binary=O/'glob2';shutil.copy2(R/'build/wake-control/src/glob2',binary);r.update(binary=str(binary),binary_sha256=sha(binary),build_log_sha256=sha(O/'build.log'))
ldd=subprocess.check_output(['ldd',str(binary)],text=True);libs={}
for line in ldd.splitlines():
 for word in line.split():
  if word.startswith('/') and Path(word).is_file():libs[str(Path(word).resolve())]=sha(word)
r.update(ldd=ldd,shared_library_hashes=libs);assert git('rev-parse','HEAD')==r['revision'] and not git('status','--porcelain','--untracked-files=no');save('build-complete.json',r)
