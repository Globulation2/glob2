import pathlib,json,shutil,sys,hashlib,datetime
C=pathlib.Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2');sys.path.insert(0,str(C))
from tools.tournaments.bundles import inspect_bundle,register_bundle
from tools.tournaments.common import atomic_json,file_hash
R=pathlib.Path('/Users/bradley/glob2/artifacts/gradient-unified-20261019');D=R/'round-003/candidate-gen003';old=R/'round-002/candidate-gen002';request=json.loads((D/'BUILD-REQUEST.json').read_text());source=request['native_source'];binary=C/'build/candidate-release/src/glob2'
assert json.loads((D/'macos-smoke/status.json').read_text())['status']=='passed'
assert 'testsuite' in (D/'goldens-verify.xml').read_text()
import xml.etree.ElementTree as ET
root=ET.parse(D/'goldens-verify.xml').getroot();assert sum(int(e.get('failures',0))+int(e.get('errors',0)) for e in root.iter('testsuite'))==0
manifest=json.loads((old/'mac-bundle.json').read_text());original=old/'bundles'/manifest['id'];manifest=inspect_bundle(original)
staging=D/'macos-package';assert not staging.exists(),'Refuse overwrite existing staged runtime'
shutil.copytree(original,staging);(staging/'bundle.json').unlink();shutil.copy2(binary,staging/'bin/glob2')
options=dict(manifest['build_options']);options.update(source_revision=source,model_sha256=request['model_sha256'],header_sha256=request['header_sha256'])
provenance=dict(options['runtime_provenance']);provenance.update(source_revision=source,actual_production_source=source,actual_binary_sha256=file_hash(binary),tool_source_revision=source,native_source_equivalence='Coefficient/SIM-only gen003. Actualmodel530UBSanparity;1024tick adaptiveobserver/threadsmoke;3goldensverified. Research-only; no stablepromotion.')
options['runtime_provenance']=provenance;atomic_json(staging/'runtime-provenance.json',provenance)
unchanged=[]
for e in manifest['files']:
 if e['path'] in ('bin/glob2','runtime-provenance.json'):continue
 assert file_hash(staging/e['path'])==e['sha256'],e['path'];unchanged.append(e['path'])
new=register_bundle(staging,D/'bundles','bin/glob2',source,options=options)
atomic_json(D/'mac-bundle.json',new)
atomic_json(D/'MAC-FROZEN.json',dict(status='passed',source_revision=source,binary_sha256=file_hash(binary),model_sha256=request['model_sha256'],header_sha256=request['header_sha256'],bundle_id=new['id'],unchanged_file_count=len(unchanged),parent_bundle=manifest['id'],dependency_asset_equivalence=True,ordinary_release=True,dev_fast=0,pch=0,unity=0,stable_promotion=False,utc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
print(new['id'])
