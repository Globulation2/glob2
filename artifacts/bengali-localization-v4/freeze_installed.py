from pathlib import Path
import json,hashlib,importlib.util,subprocess
root=Path.cwd();kit=root/'artifacts/bengali-localization-v4';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
p=module('tp',root/'tools/localization/translation_program.py');bp=module('bp',root/'test/build_provenance.py');identity=bp.source_identity(root);assert identity['revision']=='fda5e076eafc103b59b18136c137abdb6262d981' and not identity['dirty']
m=json.loads((kit/'manifest.json').read_text());assert p.sources()[4]==m['source_sha256']
paths=p.catalog_paths(kit,'bn');hashes={s:sha(f) for s,f in paths.items()};assert sha(root/'data/texts.bn.txt')==hashes['native']=='6a08cb16cf5a19f8858e59b7e54147d3d48f7a6121d395c01cbaa4d945c08201';assert sha(root/'platform/packages/i18n/locales/bn.json')==hashes['online']=='5776ba1f7d336c335c408096eb680ab9e551677a100b2f38a991c3ccb7ef1f29'
(kit/'expected-producer.json').write_text(json.dumps({'revision':identity['revision'],'code_identity':identity,'source_sha256':m['source_sha256'],'catalog_sha256':hashes},indent=2)+'\n')
(kit/'installed-source-binding.json').write_text(json.dumps({'installed_revision':identity['revision'],'code_identity':identity,'parents':subprocess.check_output(['git','show','-s','--format=%P','HEAD'],text=True).split(),'source_sha256':m['source_sha256'],'source_files':m['source_files'],'catalog_sha256':hashes,'original_snapshot_provenance_sha256':sha(kit/'source-provenance.json'),'snapshot_note':'Frozen exact native masterb34 plus branch7f online sources now installed in clean integrated merge d7fa and final WebGL-only alpha fix fda5. Actual code/build identities are recorded separately.'},indent=2)+'\n');print(json.dumps(identity,indent=2))
