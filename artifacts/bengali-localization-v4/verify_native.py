"""Execute only after the integrated source freeze; produce real build receipts."""
from pathlib import Path
import os,subprocess,json,hashlib,importlib.util
root=Path.cwd();out=root/'artifacts/bengali-localization-v4';prior=out.with_name('bengali-localization-v3');build='build/linux/client/debug/dev-dev_fast-true-linker-auto'
spec=importlib.util.spec_from_file_location('bp',root/'test/build_provenance.py');bp=importlib.util.module_from_spec(spec);spec.loader.exec_module(bp);identity=bp.source_identity(root)
expected=json.loads((out/'expected-producer.json').read_text());assert identity['revision']==expected['revision'] and not identity['dirty']
env=dict(os.environ,SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',GLOB2_SDL3_PREFIX=str(root/'build/sdl3-ci/prefix'),GLOB2_RECORDING_PREFIX=str(prior/'recording-prefix'),LD_LIBRARY_PATH=str(root/'build/sdl3-ci/prefix/lib'))
def run(name,args):
 assert bp.source_identity(root)==identity
 with (out/(name+'.log')).open('w') as f:r=subprocess.run(args,env=env,stdout=f,stderr=subprocess.STDOUT)
 result={'code_identity':identity,'revision':identity['revision'],'command':args,'exit_code':r.returncode,'profile':'Linux GCC13.4 full-feature fast development, default H264 recording; not a shipping release build.','dependencies':{'sdl_prefix':env['GLOB2_SDL3_PREFIX'],'recording_prefix':env['GLOB2_RECORDING_PREFIX'],'prior_recording_reuse_proof':'../bengali-localization-v3/native-recording-prefix-reuse.json'}}
 assert bp.source_identity(root)==identity
 if name=='native-app-build' and r.returncode==0:result['executables']=[{'path':str(root/build/p),'sha256':hashlib.sha256((root/build/p).read_bytes()).hexdigest()} for p in ['src/glob2','test/TranslationPreviewHarness']]
 (out/(name+'.json')).write_text(json.dumps(result,indent=2)+'\n');print(name,r.returncode,flush=True)
 if r.returncode:raise SystemExit(r.returncode)
run('native-app-build',['python3','tools/dev_build.py','CXX=g++-13','-j8',build+'/src/glob2','translation-preview-harness'])
run('native-tests-build',['python3','tools/dev_build.py','CXX=g++-13','-j8','unit-tests','engine-tests'])
args=['python3','test/run_tests.py','--build-dir',build,'--binary','all','--exclude-tag','display']
for group in ['FontKerning/*','TextMetrics/*','UILayout/*','SystemLanguage/*','Settings/username byte limits*','OnlineWording/*','PlatformRoom/*','MapCatalog/*','InviteLink/*']:args+=['--filter',group]
args+=['--artifacts',str(out/'native-tests'),'--junit',str(out/'native-tests.xml')]
run('native-focused',args)
