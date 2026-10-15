import os,subprocess,json,hashlib,shutil,datetime,signal
from pathlib import Path
O=Path(__file__).resolve().parent;B=Path('/home/bradley/.codex/worktrees/engine-optimization/glob2');D=B/'build/emscripten/client/release';backup=O/'web-control-candidate-public-backup';header=B/'src/common/ComputeExecutor.h';harness=B/'src/common/ComputeExecutorHarness.cpp'
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=B)
def public():return [p for p in D.rglob('*') if p.is_file() and not any(x in ['obj','tmp','include'] or x.endswith('-obj') for x in p.relative_to(D).parts)]
def write(n,d):(O/n).write_text(json.dumps(d,indent=2)+'\n')
def interrupt(*args):raise KeyboardInterrupt('control interrupted')
signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
assert git('rev-parse','HEAD').decode().strip()=='efb843d8047bba6f546c031614a31c917513ad54' and not git('status','--porcelain')
assert not backup.exists();backup.mkdir();original=header.read_bytes();baseline=git('show','054804590ee1dd109f09642fc05654c324f08344:src/common/ComputeExecutor.h');assert baseline==git('show','HEAD^:src/common/ComputeExecutor.h');hh=sha(harness)
(O/'web-control-candidate-header.h').write_bytes(original);(O/'web-control-original-header.h').write_bytes(baseline)
manifest={str(p.relative_to(D)):sha(p) for p in public()}
for relative in manifest:
 dest=backup/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(D/relative,dest)
assert all(sha(backup/r)==h for r,h in manifest.items());write('web-control-backup.json',{'state':'preserved_verified','files':manifest,'excluded':'obj/tmp/include/*-obj buildinternals only; allotherreleasefiles includingassets/shaders/runtimefiles preserved','candidate_header_sha256':hashlib.sha256(original).hexdigest(),'baseline_header_sha256':hashlib.sha256(baseline).hexdigest(),'harness_sha256':hh})
env=os.environ.copy()
for k in list(env):
 if k in ['EMSDK','EM_CONFIG','GLOB2_DEV_MODE'] or k.startswith('GLOB2_TEST'):env.pop(k,None)
env.update(CCACHE='1',GLOB2_RENDER_SHADER_INPUT='/home/bradley/.codex/worktrees/execution-resource-profiles/glob2/artifacts/resource-profiles/validation/renderer-bundle-v2/shaders')
cmd=['scons','target=web','web_variant=both','release=1','emsdk=/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39','web-tests','build/emscripten/client/release/index.html','-j32'];result={'state':'running','command':cmd,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};write('web-control-build.json',result)
try:
 header.write_bytes(baseline);assert sha(harness)==hh and git('diff','--name-only').decode().splitlines()==['src/common/ComputeExecutor.h'];assert git('diff','054804590ee1dd109f09642fc05654c324f08344','--','src/common/ComputeExecutor.h')==b'';(O/'web-control-reverse-W.patch').write_bytes(git('diff','--','src/common/ComputeExecutor.h'))
 with(O/'web-control-build.log').open('w')as f:p=subprocess.run(cmd,cwd=B,env=env,stdout=f,stderr=subprocess.STDOUT)
 result.update(state='passed'if p.returncode==0 else'failed',returncode=p.returncode);write('web-control-build.json',result);assert p.returncode==0
 write('web-control-built-public.json',{'files':{str(p.relative_to(D)):sha(p) for p in public()},'source_header':sha(header)})
 test=['npx','--no-install','playwright','test','tests/threading.spec.js','--project=firefox','--grep','threaded runtime executes shared compute','--reporter=json'];write('web-control-test-command.json',{'command':test,'env_flags':'unchangedsanitizeddefaults;freshPlaywrightownedserver/profile'})
 with(O/'web-control-playwright-results.json').open('w')as f,(O/'web-control-test.log').open('w')as e:p=subprocess.run(test,cwd=B/'browser',env=env,stdout=f,stderr=e)
 result.update(test_returncode=p.returncode);write('web-control-result.json',result)
 source=B/'build/browser-test-results/threading-threaded-runtime-330b7-ts-and-save-lifecycle-tests-firefox';dest=O/'web-control-firefox-ui';dest.mkdir(exist_ok=True)
 for q in source.glob('*'):
  if q.name!='trace.zip' and q.is_file():shutil.copyfile(q,dest/q.name)
 log=B/'artifacts/browser-determinism/worker-lifecycle/firefox-threaded.log'
 if log.exists():shutil.copyfile(log,O/'web-control-firefox-runtime.log')
except BaseException as e:
 write('web-control-failure.json',{'state':'failed','error':repr(e)});raise
finally:
 header.write_bytes(original)
 for q in public():
  if str(q.relative_to(D)) not in manifest:q.unlink()
 for relative in manifest:
  dest=D/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(backup/relative,dest)
 assert sha(harness)==hh and header.read_bytes()==original and not git('status','--porcelain')
 assert {str(p.relative_to(D)):sha(p) for p in public()}==manifest
 write('web-control-restoration.json',{'state':'restored_verified','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'HEAD':git('rev-parse','HEAD').decode().strip(),'git_clean':True,'candidate_header_sha256':sha(header),'harness_sha256':sha(harness),'all_public_files_restored_sha256':manifest,'objects':'Maynowreflectbaselineheader; SCons mustinvalidateuponfuturebuild. Restoredpublicruntimesareexacttestedcandidatebytes; norebuildclaimed.'})
