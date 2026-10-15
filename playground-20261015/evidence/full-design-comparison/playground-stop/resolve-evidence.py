#!/usr/bin/env python3
"""AFTER PG448 stop only: resolve compact evidence/current source overlays. Never copy/package."""
from pathlib import Path
import argparse,hashlib,json,os,sys
F=Path(__file__).resolve().parents[1];O=Path(__file__).resolve().parent

def read(p):return json.loads(Path(p).read_text())
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def alive_native():
 result=[]
 for p in Path('/proc').glob('[0-9]*'):
  try:s=(p/'stat').read_text().split(') ',1)[1].split()[0];c=(p/'comm').read_text().strip()
  except OSError:continue
  if s!='Z'and c.startswith(('glob2','cc1','clang','gcc','g++','lto1')):result.append(p.name)
 return result

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--resolve-after-completion',action='store_true');ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();assert a.resolve_after_completion and not a.output.exists()
 proof=read(O/'complete.json');result=read(O/'results/receipt.json');assert proof['state']=='stopped_after_complete_playground'and proof['runs']==448 and result['state']=='playground448_exported_stopped_awaiting_user'and result['runs']==448 and result['stop_proof_sha256']==sha(O/'complete.json')
 assert not alive_native(),'Resolve only after all native/compiler work stops'
 plan=read(F/'campaign/plan.json');assert proof['plan_sha256']==sha(F/'campaign/plan.json');qual=read(F/'qualification-complete.json');assert qual['state']=='qualified_with_known_baseline_failures'and len(qual['roles'])==14
 entries={};missing=[];conflicts=[];source_checks=[];overlays={};contexts=[];optional_absent=[]
 def add(path,alias=None,expected=None,category='metadata',verify_small=False,mandatory=True):
  p=Path(path).resolve()
  if alias is None:
   try:alias='evidence/full-design-comparison/'+p.relative_to(F).as_posix()
   except ValueError:raise ValueError('External selected file needs explicit portable alias: '+str(p))
  rel=Path(alias);assert not rel.is_absolute()and '..'not in rel.parts
  if not p.is_file():
   (missing if mandatory else optional_absent).append({'source':str(p),'portable':alias,'category':category});return
  assert not any(x in p.parts for x in ('.git','profile','profiles','__pycache__'))
  assert p.suffix not in ('.wasm','.o','.a','.so','.exe','.dll')and not p.name.startswith(('glob2-unit-tests','glob2-engine-tests'))
  size=p.stat().st_size;record={'source':str(p),'portable':alias,'bytes':size,'declared_sha256':expected,'category':category,'source_aliases':[str(p)]}
  if verify_small:
   assert size<=8*1024*1024,('Unexpectedly large current source/fixture',p,size)
   actual=sha(p);source_checks.append({'path':str(p),'declared_sha256':expected,'actual_sha256':actual,'matched':actual==expected})
   if actual!=expected:conflicts.append(source_checks[-1])
   record['verified_current_sha256']=actual
  if alias in entries:
   prev=entries[alias]
   if p.as_posix()!=prev['source']:
    if not expected or expected!=prev['declared_sha256']or size!=prev['bytes']:conflicts.append({'portable_collision':alias,'sources':[prev['source'],str(p)]})
    else:prev['source_aliases'].append(str(p))
   elif expected and prev['declared_sha256']and expected!=prev['declared_sha256']:conflicts.append({'manifest_hash_conflict':str(p),'hashes':[expected,prev['declared_sha256']]})
   elif expected:prev['declared_sha256']=expected
  else:entries[alias]=record
 def metadata(rel,mandatory=True):add(F/rel,mandatory=mandatory)
 # Exact final raw inventory; receipt/stdout hashes come from verified stop/analyzer proofs.
 assert len(proof['receipt_hashes'])==448 and len(proof['wave_hashes'])==448
 for rel,h in proof['receipt_hashes'].items():
  p=F/'campaign/serial'/rel;add(p,expected=h,category='PG timing receipt');r=read(p);add(p.parent/'stdout.log',expected=r['stdout_sha256'],category='PG compact stdout')
 for name,h in proof['wave_hashes'].items():
  p=F/'campaign/serial'/name;add(p,expected=h,category='PG single-game window');add(p.with_suffix('.binding.json'),category='PG window binding')
 add(F/'campaign/serial/pairs.json',expected=proof['pairs_sha256'],category='416 PG baseline pairs')
 for name,h in result['outputs'].items():add(O/'results'/name,'results/'+name,h,'PG result/audit/figure')
 add(O/'results/receipt.json','results/receipt.json',category='PG export receipt')
 core=['request.json','campaign/plan.json','campaign/resolved-variants.json','campaign/runner.py','campaign/verify.py','campaign/resolve-plan.py','campaign/root-approved.json','preparation.json','weighted-preparation.json','weighted-preparation-freeze.json','weighted-preparation.py','weighted-preparation-tests.py','built-variants.json','builds-complete.json','build-all.py','rebuild-harnesses.py','harness-slot-repair.json','source-preparation.json','staged-fixed-maps.json','first-steal-source.patch','local-source.patch','priority-source.patch','weighted-source.patch','weighted-independent-review.json','qualification-complete.json','qualification-independent-review.json','matched-baseline-failures-review.json','baseline-parity-reuse.json','recover-qualification.py','recovery-qualification-independent-review.json','recovery-final-gate-review.json','supplemental-focused-complete.json','focused-gap-checks/gap-checks-complete.json','weighted-preparation-bare-diagnostic/summary.json','weighted-preparation-bare-diagnostic/direct-suite.json','weighted-preparation-bare-diagnostic/direct-suite.log','weighted-preparation-bare-diagnostic/isolated-cases.json','weighted-preparation-bare-diagnostic/repeated-suite.json','weighted-preparation-bare-diagnostic/parallel-waves.json','recovery-1/qualification/weighted-bare-steal/focused-unit/stdout.log','recovery-1/qualification/weighted-bare-steal/focused-unit/junit.xml']
 for rel in core:metadata(rel)
 for p in O.glob('*.json'):add(p,mandatory=True) # Guard/analysis gates, source reviews, preflight failures, stop proof.
 for name in ('watch.py','analyze-playground.py','preflight.py','resolve-evidence.py','analysis-console.log','watch-console.log'):add(O/name)
 add(F/'campaign/serial/Oazis',category='Regular administrative sentinel, not Oazis result')
 # CURRENT source snapshots are authoritative overlays. Port/original patch files
 # and Harness79→84 repair receipts remain separate historical reconstruction steps.
 for manifest_name in ('preparation.json','weighted-preparation.json'):
  for source in read(F/manifest_name)['sources']:
   label=source['label'];overlays.setdefault(label,[])
   for relative,h in source['source_hashes'].items():
    path=Path(source['cwd'])/relative;alias='source/current/'+label+'/'+relative;add(path,alias,h,'Current source overlay (after Harness repair)',True);overlays[label].append({'repository_path':relative,'portable':alias,'sha256':h})
 # Cross-check current build manifests against the source overlay declarations.
 for b in read(F/'builds-complete.json')['sources']:
  label=b['source']['label']
  for rel,h in b['source']['source_hashes'].items():add(Path(b['source']['cwd'])/rel,'source/current/'+label+'/'+rel,h,'Built-current source overlay',True)
  for rel in (f'builds/{label}/complete.json',f'builds/{label}/receipt.json',f'builds/{label}/build.log',f'recovery-1/unit-rebuild-{label}/receipt.json',f'recovery-1/unit-rebuild-{label}/build.log'):metadata(rel)
  if b.get('original_build_receipt'):add(b['original_build_receipt'])
 for rel in ('recovery-1/builds-complete.json','recovery-1/preparation.json','recovery-1/rebuild-launch.json','recovery-1/rebuild-controller.log'):metadata(rel)
 controls=F.parent/'confirmation/latest-wake/ready.json';add(controls,'evidence/control-source/ready.json')
 controls_ready=read(controls)
 for name,h in controls_ready['source_hashes'].items():
  path=Path(name)
  if '/src/'in name:
   root=next(v for v in ('baseline','wake-only')if '/'+v+'/'in name);relative='src/'+name.split('/src/',1)[1];alias='source/current/'+root+'/'+relative;add(path,alias,h,'Baseline/W exact current source',True);overlays.setdefault(root,[]).append({'repository_path':relative,'portable':alias,'sha256':h})
  elif path.name=='Playground.map':add(path,'fixtures/'+h+'/Playground.map',h,'Exact fixed PG map bytes',True)
 for name,h in plan['fixtures'].items():
  if Path(name).name=='Playground.map':add(name,'fixtures/'+h+'/Playground.map',h,'Exact timing PG map source alias',True)
 # Follow ACTUAL trace command paths, including reused baseline contexts under
 # recovery-1/qualification/baseline/parity; never reconstruct guessed locations.
 for role,ref in qual['roles'].items():
  rolefile=Path(ref['path']);add(rolefile,expected=ref['sha256'],category='Qualified role manifest');r=read(rolefile);assert r['role']==role and len(r['traces'])==20
  for trace in r['traces']:
   command=Path(trace['command_receipt']);d=command.parent;add(command,category='Qualification exact command')
   receipt=Path(trace.get('reuse_source_receipt',d/'receipt.json'));add(receipt,expected=trace.get('reuse_source_receipt_sha256'),category='Compact per-tick qualification receipt')
   for name in ('stdout.log','native/result.json','native/artifacts.json'):add(d/name,category='Compact qualification metadata',expected=trace.get('result_sha256')if name=='native/result.json'else None)
   contexts.append({'role':role,'case':trace['case'],'command_receipt':str(command),'receipt':str(receipt),'reused':trace.get('reused_qualification',False),'sidecar_provenance':trace.get('sidecar_details',{}),'body_hashes':trace['checksums'],'body_policy':'GCS/replay/world bodies omitted; metadata+hashes retained, not full portable checksum-body revalidation.'})
  for focus in r['focused_tests']:
   command=Path(focus['command_receipt']);d=command.parent;add(command,category='Focused qualification command');add(d/'stdout.log',category='Focused raw output')
   if focus.get('junit_sha256'):add(d/'junit.xml',expected=focus['junit_sha256'],category='Focused JUnit incl matched failures')
   if focus.get('fresh_supplemental_metadata'):
    x=focus['fresh_supplemental_metadata'];add(x['path'],expected=x['sha256'],category='Fresh gradient/weighted metadata')
 assert len(contexts)==280 and len({(x['role'],x['case'])for x in contexts})==280
 # Keep every extant administrative failure/repair snapshot compactly, not fulltrees.
 for pattern in ('recovery-*/qualification/*/failure.json','recovery-*/qualification/*/focused-*/junit.xml','playground-stop/rejected-stop-draft/*.json','playground-stop/rejected-stop-draft/*.py'):
  for p in F.glob(pattern):add(p,category='Historical failure/repair evidence')
 output={'state':'resolved'if not missing and not conflicts else'blocked_missing_or_conflicting_mandatory_provenance','scope':'PG448 only; no copies/compression/publication. Source overlays verified small files only after complete stop; bulk content verification belongs separate root materialization gate.','files':sorted(entries.values(),key=lambda x:x['portable']),'file_count':len(entries),'bytes':sum(x['bytes']for x in entries.values()),'missing_mandatory':missing,'manifest_conflicts':conflicts,'optional_absent':optional_absent,'current_source_checks':source_checks,'qualification_contexts':contexts,'qualification_context_count':280,'source_reconstruction':{'base_revision':plan['base_revision'],'order':['Checkout exact054base','Keep original port.patch files and Harness repair receipt ashistory','Overlay listed CURRENTsource files LAST byrepository_path (authoritative, notpatch-alone)','Build matchingrecordedcompiler/flags/dependencies/HISTORYadapters','Select exactroleenv andfixedPGmapbytes percommands'],'overlays':overlays},'limits':['448telemetryOffgames provide winner/tick agreement, not full checksum proof.280boundedcontexts separate; massive bodies deliberately omitted with provenance.','Known2baselinefailures and unexplained180sbaretimeout retained; no allgreen/universaloptimality/platform claim.','Futurepublic URLs/fullhistoricalc9efaa commit must be resolved beforepublishing, not inferred fromprefix.']}
 a.output.parent.mkdir(parents=True,exist_ok=True)
 with a.output.open('x')as f:f.write(json.dumps(output,indent=2)+'\n')
 print(output['state'],len(entries),'files',len(missing),'missing',len(conflicts),'conflicts')
if __name__=='__main__':
 try:main()
 except Exception as e:
  # A missing critical manifest prevents dynamic resolution, so still retain
  # an explicit blocked receipt rather than silently producing a partial list.
  if '--resolve-after-completion'in sys.argv and '--output'in sys.argv:
   output=Path(sys.argv[sys.argv.index('--output')+1])
   if not output.exists():
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x')as f:f.write(json.dumps({'state':'blocked_critical_input_or_validation_error','error':repr(e),'no_copy_compression_or_publication':True},indent=2)+'\n')
  raise

