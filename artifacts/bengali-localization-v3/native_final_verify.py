from pathlib import Path
import os,subprocess,json,hashlib,importlib.util
root=Path.cwd();out=root/'artifacts/bengali-localization-v3';build='build/linux/client/debug/dev-dev_fast-true-linker-auto'
# Wait only for this task's initial core driver; then force the one changed TU.
import time
while Path('/proc/2489856').exists(): time.sleep(5)
original_log=out/'native-app-build.log'
if original_log.exists(): original_log.rename(out/'native-app-build-2afe-unsealed.log')
obj=root/build/'src/hud/GameGUIDialog.o'
record={'reason':'Force fresh final254-byte UI cap compilation after unsealed2afe core build; do not reuse a potentially stale object.','previous_object_present':obj.exists()}
if obj.exists():
 record['previous_sha256']=hashlib.sha256(obj.read_bytes()).hexdigest();saved=out/'objects/GameGUIDialog-before-final.o';saved.parent.mkdir(exist_ok=True);assert not saved.exists();obj.rename(saved)
(out/'native-final-object-refresh.json').write_text(json.dumps(record,indent=2)+'\n')

s=importlib.util.spec_from_file_location('bp',root/'test/build_provenance.py');bp=importlib.util.module_from_spec(s);s.loader.exec_module(bp);identity=bp.source_identity(root)
assert identity['revision']=='1d36a7031bbd14204648b48048f0e01983f7ccc1' and not identity['dirty']
env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_SDL3_PREFIX=str(root/'build/sdl3-ci/prefix'),GLOB2_RECORDING_PREFIX=str(out/'recording-prefix'),LD_LIBRARY_PATH=str(root/'build/sdl3-ci/prefix/lib'))
def run(name,args):
 assert bp.source_identity(root)==identity
 with (out/(name+'.log')).open('w') as f:r=subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT)
 result={'code_identity':identity,'revision':identity['revision'],'command':args,'exit_code':r.returncode,'profile':'Linux GCC13.4 full-feature fast development; current defaultH264 recording; no shipping-release claim.','dependencies':{'sdl_prefix':env['GLOB2_SDL3_PREFIX'],'recording_prefix':env['GLOB2_RECORDING_PREFIX'],'recording_reuse_proof':'native-recording-prefix-reuse.json'}}
 assert bp.source_identity(root)==identity
 if name=='native-app-build' and r.returncode==0:
  result['executables']=[{'path':str(root/build/p),'sha256':hashlib.sha256((root/build/p).read_bytes()).hexdigest()} for p in ['src/glob2','test/TranslationPreviewHarness']]
 (out/(name+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(name,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
run('native-app-build',['python3','tools/dev_build.py','CXX=g++-13','-j8',build+'/src/glob2','translation-preview-harness'])
run('native-tests-build',['python3','tools/dev_build.py','CXX=g++-13','-j8','unit-tests','engine-tests'])
run('native-focused',['python3','test/run_tests.py','--build-dir',build,'--binary','all','--filter','FontKerning/*','--filter','TextMetrics/*','--filter','UILayout/*','--filter','SystemLanguage/*','--filter','Settings/username byte limits*','--filter','OnlineWording/*','--filter','PlatformRoom/*','--filter','MapCatalog/*','--filter','InviteLink/*','--artifacts',str(out/'native-tests'),'--junit',str(out/'native-tests.xml')])
