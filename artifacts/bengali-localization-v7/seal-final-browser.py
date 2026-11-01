"""Seal the actual successful V7 full SCons graph, never a whole old binary carry."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path.cwd();sys.path.insert(0,str(root/'test'));from build_provenance import source_identity
art=root/'artifacts/bengali-localization-v7';output=root/'build/emscripten/client/debug/dev-dev_fast-true-linker-auto';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
state=json.loads((art/'browser-build-state.json').read_text());now=source_identity(root);expected=json.loads((art/'expected-producer.json').read_text())
assert state['exitStatus']==0 and state['sourceStable'] and now==state['sourceAfter']==expected['code_identity'];assert not now['dirty']
log=(art/'browser-game-build.log').read_text();assert 'scons: done building targets.' in log
baseline=root/'artifacts/bengali-localization-v5/browser-final-production-provenance.json';prior=json.loads(baseline.read_text());changed=subprocess.check_output(['git','diff','--name-only',prior['package_production_input_revision'],now['revision']],text=True).splitlines()
objects={}
for variant in ['', 'threaded']:
 for obj in sorted((output/variant/'obj').rglob('*.o')):
  rel=str(obj.relative_to(output/variant/'obj'));src=rel[:-2];source=root/src
  if source.is_file():
   key=str(obj.relative_to(root));objects[key]={'sha256':sha(obj),'sourceSha256':sha(source),'currentCompileCommandSeen':any(key in line and ' -c ' in line for line in log.splitlines())}
assert any(v['currentCompileCommandSeen'] for v in objects.values()),'Expected actual integration object refresh'
runtime=[]
for folder in [output,output/'threaded']:
 runtime.extend(p for p in folder.iterdir() if p.is_file() and (p.suffix in {'.js','.wasm','.data','.html','.css','.map'} or p.name=='identity.json'))
for name in ['assets','fonts','locales','licenses']:runtime.extend(p for p in (output/name).rglob('*') if p.is_file())
runtime=sorted(set(runtime));assert len(runtime)<20000 and sum(p.stat().st_size for p in runtime)<=1024**3
manifest={'sourceRevision':now['revision'],'purpose':'Full-feature fast development review package; not release/performance evidence','files':{str(p.relative_to(output)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in runtime}}
directory=art/'browser-runtime';directory.mkdir(parents=True,exist_ok=True);manifest_path=directory/'package-manifest.json';assert not manifest_path.exists();manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
files=['identity.json','threaded/identity.json','index.html','index.js','index.wasm','index.data','threaded/index.js','threaded/index.wasm','threaded/index.data','asset-manifest.js','i18n.js','locales/bn.json','script-fonts.css','fonts/NotoSansBengali-Regular.ttf']
proof={'cpp_production_input_revision':now['revision'],'package_production_input_revision':now['revision'],'actual_final_validation_worktree_revision':now['revision'],'source_before_final_producer_validation':state['sourceBefore'],'source_after_final_producer_validation':now,'validation_exit_status':0,'command':state['command'],'validation_log_sha256':sha(art/'browser-game-build.log'),'package_directory':str(output),'package_file_sha256':{n:sha(output/n) for n in files},'runtimeManifest':{'path':str(manifest_path),'sha256':sha(manifest_path),'files':len(runtime)},'actualChangedFiles':{n:sha(root/n) for n in changed if (root/n).is_file()},'objectInventory':objects,'dependencySealSha256':state['dependencySealSha256'],'coverage':'Actual successful full current SCons dependency/signature graph for both full-feature fast development variants, including upstream integration. Individual compile observations recorded; no whole old binary/source carry claim.'}
proof['proof_file_sha256']={str(p.relative_to(root)):sha(p) for p in [root/'artifacts/bengali-localization/browser-integrated-dependency-probe.json',root/'artifacts/bengali-localization/browser-integrated-recording-noop.json',root/'artifacts/bengali-localization/readonly-scons-integrated/bin/scons',art/'run-final-browser.py',art/'expected-producer.json']}
encoded=(json.dumps(proof,indent=2)+'\n').encode();digest=hashlib.sha256(encoded).hexdigest();(directory/'producers').mkdir(exist_ok=True);(directory/'producers'/(digest+'.json')).write_bytes(encoded);(art/'browser-final-production-provenance.json').write_bytes(encoded);(art/'browser-source-history.json').write_text(json.dumps({'productionRevision':now['revision'],'source':now,'actualFullCurrentSConsGraph':True},indent=2)+'\n');print(directory/'producers'/(digest+'.json'),flush=True)
