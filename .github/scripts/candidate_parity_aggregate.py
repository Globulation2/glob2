"""Evidence-only exact-run archive extraction and complete trace comparison."""
import hashlib, importlib.util, json, os, subprocess, tempfile, zipfile
from pathlib import Path
RUN=37570683268
REV='4c8297c5c17974bbcc9f1fedbfbb43064095e04c'
OUT=Path('artifacts/candidate-parity-aggregation'); OUT.mkdir(parents=True,exist_ok=True)
def api(path):return json.loads(subprocess.check_output(['gh','api',path],text=True))
selected=json.loads((Path(os.environ['RUNNER_TEMP'])/'candidate_parity_artifacts.json').read_text())
assert len({a['id'] for a in selected})==len(selected)
(OUT/'selected-artifacts.json').write_text(json.dumps(selected,indent=2)+'\n')
source=json.loads(subprocess.check_output(['python3','test/build_provenance.py'],text=True));assert source['revision']==REV and source['dirty'] is False
(OUT/'checkout-source.json').write_text(json.dumps(source,indent=2)+'\n')
inventories={}; acquired=[]
for a in selected:
 metadata=api(f"repos/Globulation2/glob2/actions/artifacts/{a['id']}")
 assert metadata['workflow_run']['id']==RUN and metadata['name']==a['name'] and metadata['digest']==a['digest'],metadata
 (OUT/'artifact-metadata').mkdir(exist_ok=True);(OUT/'artifact-metadata'/f"{a['id']}.json").write_text(json.dumps(metadata,indent=2)+'\n')
 name=a['name']; target=OUT/('traces' if name.startswith('browser-determinism-') else 'native-cases')/name
 with tempfile.TemporaryFile() as tmp:
  subprocess.run(['gh','api',f"repos/Globulation2/glob2/actions/artifacts/{a['id']}/zip"],stdout=tmp,check=True)
  tmp.seek(0); digest=hashlib.file_digest(tmp,'sha256').hexdigest();assert a['digest']=='sha256:'+digest,(name,digest,a['digest']);tmp.seek(0)
  with zipfile.ZipFile(tmp) as z:
   inventories[name]=[{'path':i.filename,'bytes':i.file_size} for i in z.infolist()]
   for i in z.infolist():
    path=Path(i.filename)
    if path.is_absolute() or '..' in path.parts:raise ValueError('Unsafe artifact member')
    keep=name.startswith('browser-determinism-') or name=='ci-observation-selection' or path.name in ('seeded-compositions.trace','build-provenance.json')
    if keep and not i.is_dir():
     dest=target/path;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(z.read(i))
  acquired.append({'id':a['id'],'name':name,'verified_archive_sha256':digest})
  (OUT/'verified-archives.json').write_text(json.dumps(acquired,indent=2)+'\n')
  (OUT/'archive-inventories.json').write_text(json.dumps(inventories,indent=2)+'\n')
platforms=['ubuntu-24.04','ubuntu-22.04','windows','macos'];native_origins={}
for platform in platforms:
 archives=[OUT/'native-cases'/a['name'] for a in selected if a['name'].startswith('native-test-results-'+platform) or platform=='macos' and a['name']=='native-macos-validation']
 traces=[p for d in archives for p in d.rglob('seeded-compositions.trace')];assert len(traces)==1,(platform,[str(p) for p in traces])
 producers=[p for d in archives for p in d.rglob('build-provenance.json')];assert producers,(platform,'missing compiled provenance')
 values=[json.loads(p.read_text()) for p in producers]
 for v in values:
  for key in ('revision','dirty','sourceTreeSha256'):assert v[key]==source[key],(platform,key,v[key],source[key])
 dest=OUT/'traces'/('browser-determinism-'+platform)/'resources/native';dest.mkdir(parents=True,exist_ok=True)
 dest.joinpath('seeded-compositions.trace').write_bytes(traces[0].read_bytes())
 manifest={'producer':values[0],'adapter':'Collected from original native test artifacts; no simulation executed in this aggregation','trace_origin':str(traces[0]),'producer_origins':[str(p) for p in producers]}
 dest.joinpath('manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');native_origins[platform]=manifest
spec=importlib.util.spec_from_file_location('compare',Path(os.environ['RUNNER_TEMP'])/'ci_compare_evidence.py');compare=importlib.util.module_from_spec(spec);spec.loader.exec_module(compare)
stock=compare.validate_traces(OUT/'traces',platforms,{'chromium','firefox','webkit'})
composition=compare.validate_resource_compositions(OUT/'traces',platforms,{'chromium','firefox','webkit'},Path('test/fixtures/resources/seeded-compositions.trace'))
gcs_spec=importlib.util.spec_from_file_location('gcs',Path('test/compare_save_continuation.py'));gcs=importlib.util.module_from_spec(gcs_spec);gcs_spec.loader.exec_module(gcs)
reference=list((OUT/'traces/browser-determinism-ubuntu-24.04').rglob('native.replay.checksums'));assert len(reference)==1
stock_ticks=sum(1 for _ in gcs.records(reference[0].read_bytes()));assert stock_ticks==1500,stock_ticks
selection_paths=list((OUT/'native-cases/ci-observation-selection').rglob('ci-selection.json'));assert len(selection_paths)==1
selection=json.loads(selection_paths[0].read_text());assert selection['sha']==REV,selection['sha']
browser_matrix=selection['inventory']['browsers']
match=list((OUT/'traces').rglob('verify-match.checksums.txt'));match_identities={}
for p in match:
 relative=p.relative_to(OUT/'traces');archive=relative.parts[0]
 if archive.startswith('browser-determinism-wasm-'):
  project=browser_matrix[int(archive.rsplit('-',1)[1])]['browsers']
  assert relative.parts[1:] in [('wasm','verify-match.checksums.txt'),('wasm','verify-threaded','verify-match.checksums.txt')],str(relative)
  identity=(project,'threaded' if 'verify-threaded' in relative.parts else 'serial')
 else:identity=(archive.removeprefix('browser-determinism-'),'native')
 assert identity not in match_identities,identity
 match_identities[identity]=str(relative)
expected_identities={(p,'native') for p in platforms}|{(p,v) for p in ('chromium','firefox','webkit') for v in ('serial','threaded')}
assert set(match_identities)==expected_identities,(set(match_identities),expected_identities)
expected=Path('test/fixtures/multiplayer/FourSquares1.verify-trace.txt').read_bytes().replace(b'\r\n',b'\n');assert len(expected.splitlines())>600
for p in match:assert p.read_bytes().replace(b'\r\n',b'\n')==expected,str(p)
result={'revision':REV,'stock_trace_count':stock,'stock_ticks_each':stock_ticks,'match_trace_count':len(match),'match_identities':{':'.join(k):v for k,v in match_identities.items()},'match_rows_each':len(expected.splitlines()),'composition_trace_count':composition,'composition_rows_each':150,'native_origins':native_origins,'result':'all complete traces equal; artifacts only, no engine rerun'}
(OUT/'comparison.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='native_origins'},indent=2))
