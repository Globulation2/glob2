"""Ignored exact-freeze browser producer. Requires explicit final revision; never auto-starts."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,time
root=Path.cwd();sys.path.insert(0,str(root/'test'));from build_provenance import source_identity
art=root/'artifacts/bengali-localization-v7';old=root/'artifacts/bengali-localization';expected=os.environ['GLOB2_FINAL_REVISION'];before=source_identity(root);assert before['revision']==expected and not before['dirty'],before
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
seal=json.loads((old/'browser-integrated-dependency-probe.json').read_text());sdk=Path('/home/bradley/.local/share/glob2/development/toolchains/emscripten/Linux-x86_64-88a621bc131cd4954413aa39')
for name,digest in seal['actual_sdk']['actual_underlying_tool_sha256'].items():assert sha(sdk/name)==digest
assert sha(sdk/'.glob2-toolchain.json')==seal['actual_sdk']['managed_receipt_sha256']
for dep in seal['dependencies']:
 for name,digest in dep['all_installed_files_sha256'].items():assert sha(Path(dep['shared_prefix'])/name)==digest
# Existing builder no-op proof must be refreshed independently if its source/config changes.
for name in ['scons/recording_dependencies.py','scons/sdl_dependencies.py','scons/opus_dependencies.py','scons/shared_dependencies.py','tools/dev_build.py','browser/toolchain.json']:
 assert not subprocess.check_output(['git','diff','--name-only','7fcb8a2216a8220dd753bba1f6cd04d1f8934fc9','HEAD','--',name],text=True).strip(),name+' requires fresh dependency assessment'
env=dict(os.environ,GLOB2_DEV_MODE='shared',GLOB2_RECORDING_PREFIX='/home/bradley/.local/share/glob2/development/dependencies/build-a67f9f075109cd87f6160634/prefix',GLOB2_ASSET_ENCODER_PYTHON='/home/bradley/.local/share/glob2/development/caches/asset-encoder-runtime-assets-v1-py3.14/venv/bin/python',CCACHE_DIR='/home/bradley/.local/share/glob2/development/caches/ccache',CCACHE_READONLY='1',CCACHE_NOSTATS='1',CCACHE_TEMPDIR=str(old/'browser-compiler-tmp'),EM_FROZEN_CACHE='1',PYTHONDONTWRITEBYTECODE='1');env.pop('CCACHE_SLOPPINESS',None);env['PATH']=str(old/'readonly-scons-integrated/bin')+os.pathsep+str(sdk/'node/24.19.0_64bit/bin')+os.pathsep+env.get('PATH','')
command=['python3','tools/dev_build.py','target=web','web_variant=both','emsdk='+str(sdk),'-j4','web-package'];state={'productionRevision':expected,'sourceBefore':before,'command':command,'dependencySealSha256':sha(old/'browser-integrated-dependency-probe.json'),'status':'building'};log=art/'browser-game-build.log';assert not log.exists(),'Preserve prior producer logs';statepath=art/'browser-build-state.json';statepath.write_text(json.dumps(state,indent=2)+'\n');start=time.monotonic()
with log.open('w') as stream:
 p=subprocess.Popen(command,env=env,stdout=stream,stderr=subprocess.STDOUT);state['ownedChildPid']=p.pid;statepath.write_text(json.dumps(state,indent=2)+'\n');print('Started final browser producer',p.pid,flush=True);status=p.wait()
state.update(exitStatus=status,wallSeconds=time.monotonic()-start,sourceAfter=source_identity(root),buildLogSha256=sha(log));state['sourceStable']=state['sourceBefore']==state['sourceAfter'];state['status']='complete' if status==0 and state['sourceStable'] else 'failed';statepath.write_text(json.dumps(state,indent=2)+'\n');assert state['sourceStable'],'Source changed during build';raise SystemExit(status)
