from pathlib import Path
import os,subprocess,json,hashlib,importlib.util
root=Path.cwd();out=root/'artifacts/bengali-localization-v3';build='build/linux/client/debug/dev-dev_fast-true-linker-auto'
s=importlib.util.spec_from_file_location('bp',root/'test/build_provenance.py');bp=importlib.util.module_from_spec(s);s.loader.exec_module(bp);identity=bp.source_identity(root)
assert identity['revision']=='2afe891014845d0c44447e09db08a2a67754323d' and not identity['dirty']
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
