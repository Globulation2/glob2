from pathlib import Path
import json,hashlib,re,subprocess,collections
root=Path.cwd();b=root/'artifacts/marathi-localization/integration-bc8';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();read=lambda p:json.loads(Path(p).read_text());summary=read(b/'native-final-summary.json');gate=read(b/'native-gate-inputs.json');frozen={str(b/p):sha(b/p) for p in gate['evidence_sha256']};assert all(sha(b/p)==d for p,d in gate['evidence_sha256'].items())
assert gate['current_validation_head']==summary['current_validation_head']=='0e6e243e098cf32afbf303937ba4f55384685f37'
assert sha(root/'data/texts.mr.txt')==summary['catalog_sha256']['native']=='540819f418f880607fad22780bd25692be84c33794fba08cf758ae0acb80d4eb';assert sha(root/'platform/packages/i18n/locales/mr.json')==summary['catalog_sha256']['online']=='0a35ecc3e76fd00b68db89ca5dd18967304524eb7c3c0b8cc81c37b70e5694a4'
lines=(root/'data/texts.mr.txt').read_text().splitlines();assert len(lines)==6822;native=dict(zip(lines[::2],lines[1::2]));assert native==read(b/'previews/mr/native.json');online=read(root/'platform/packages/i18n/locales/mr.json');assert online==read(b/'previews/mr/online.json');values={('native',k,''):v for k,v in native.items()}
for k,v in online.items():
 if isinstance(v,str):values['online',k,'']=v
 else:
  for branch,text in v.items():values['online',k,branch]=text
assert len(native)==3411 and len(online)==2836 and len(values)==6274
runner=read(b/'native-render-clean64/runner.json');assert len(runner['jobs'])==34 and all(j['returncode']==0 for j in runner['jobs']);assert runner['git_revision']=='64aeda1300e3fcb81b074aef1a9a685498edcf63'
recomputed=[]
for vp in ['1280x900','390x844']:
 folder=b/'native-render-clean64'/vp/'matrix';m=read(folder/'font-matrix.json');d=read(folder/'matrix-diagnostics.json');assert sha(folder/'font-matrix.json')==d['report_sha256'];assert m['evidence_identity']['locale']=='mr' and m['evidence_identity']['producer']['revision']==runner['git_revision'] and not m['evidence_identity']['producer']['dirty'];meta=read(b/'previews/mr/metadata.json');assert m['evidence_identity']['catalog_sha256']==meta['preview_catalog_sha256'];assert meta['original_catalog_sha256']==summary['catalog_sha256']
 counts=collections.Counter();missing=[];drawbad=[];raw=[];pages=set();linescount=0
 for s in m['samples']:
  identity=(s['surface'],s['key'],s['branch']);assert identity in values;counts[(s['size'],s['scale'],*identity)]+=1
  assert re.sub(r'\s','',values[identity])==re.sub(r'\s','',''.join(l['text'] for l in s['lines']))
  if not s['has_glyphs']:missing.append(identity)
  for line in s['lines']:
   linescount+=1;pages.add(line['png']);scale=s['scale'];assert line['drawn_fits_width']==(line['drawn_width']<=(m['logical_width']-24)*scale);assert line['drawn_fits_height']==(line['y']+(line['drawn_height']+scale-1)//scale<=m['logical_height'])
   if not line['drawn_fits_width'] or not line['drawn_fits_height']:drawbad.append(identity)
   if not line['fits_width'] or not line['fits_height']:
    assert scale==2 and not line['fits_width'] and line['fits_height'];raw.append((identity,line))
 assert len(counts)==6274*6 and set(counts.values())=={1};assert {key[:2] for key in counts}=={(n,s) for n in [10,13,20] for s in [1,2]};assert len(m['samples'])==d['sample_count']==37644;assert linescount==d['line_count'];assert not missing and not drawbad;assert len(raw)==len(d['raw_raster_diagnostics']);assert len(pages)==d['png_pages'];assert all((folder/p).is_file() for p in pages)
 recomputed.append({'viewport':vp,'samples':len(m['samples']),'lines':linescount,'variants':6274,'pages':len(pages),'missing_glyphs':0,'drawn_failures':0,'raw_scale2_width_only_diagnostics':len(raw)})
assert sum(x['samples'] for x in recomputed)==summary['matrix_samples']==75288;assert sum(x['lines'] for x in recomputed)==summary['matrix_lines']==83542;assert sum(x['pages'] for x in recomputed)==3212;assert sum(x['raw_scale2_width_only_diagnostics'] for x in recomputed)==592
ov=read(b/'native-overlay-equivalence.json');assert len(ov['overlays'])==38
for item in ov['overlays']:
 p=b/item['path'];assert sha(p)==item['sha256'];a=p.read_text().splitlines();assert dict(zip(a[::2],a[1::2]))==native
reports=summary['reports']+summary['extra_reports']+summary['registered_reports'];assert len(reports)==42;drawchecks=0
for item in reports:
 p=b/item['path'];assert sha(p)==item['sha256'];d=read(p)
 for cap in d.get('captures',[]):assert cap['checksum_before_draw']==cap['checksum_after_draw'];drawchecks+=1
for item in summary['registered_reports']:
 d=read(b/item['path']);probe=d['evidence_identity']['registered_resource_probe'];assert probe['complete'] and probe['native_name']=='मराठी' and not probe['overlay_keys_overridden'] and not d['native_keys_overridden'];assert probe['actual_native_catalog_sha256']==summary['catalog_sha256']['native'];assert probe['incomplete_metadata_sha256']==sha(root/'data/texts.incomplete.txt');assert d['evidence_identity']['producer']['revision']=='187ea34de87838911a42321fd52925d51da475b9'
head=summary['current_validation_head'];diff=lambda a:subprocess.check_output(['git','diff','--name-only',a,head],text=True).splitlines();assert diff('64aeda1300e3fcb81b074aef1a9a685498edcf63')==['browser/tests/marathi.spec.js','data/texts.incomplete.txt'];assert diff('187ea34de87838911a42321fd52925d51da475b9')==['browser/tests/marathi.spec.js']
inspectionfiles=[p for p in gate['evidence_sha256'] if 'visual-inspection' in p or 'root-visual-review' in p];images=[]
def inspect(v,parent):
 if isinstance(v,dict):
  if isinstance(v.get('path'),str) and v['path'].endswith('.png') and 'sha256' in v:
   s=v['path'];p=Path(s);p=p if p.is_absolute() else root/p if s.startswith('artifacts/') else parent/p;assert p.is_file() and sha(p)==v['sha256'];images.append(str(p.relative_to(root)))
  for child in v.values():inspect(child,parent)
 elif isinstance(v,list):
  for child in v:inspect(child,parent)
for name in inspectionfiles:inspect(read(b/name),(b/name).parent)
assert all(sha(Path(p))==d for p,d in frozen.items()),'Inputreport changed duringaudit; retryonlyafterstable'
import xml.etree.ElementTree as ET
initial=ET.parse(b/'native/test-results/junit.xml').getroot();retry=ET.parse(b/'native/test-results-display-retry/junit.xml').getroot();first=initial.findall('.//testcase');again=retry.findall('.//testcase');bad=lambda c:c.find('failure') is not None or c.find('error') is not None
assert len(first)==21 and sum(bad(c) for c in first)==3 and len(again)==3 and not any(bad(c) for c in again)
assert {c.attrib['name'] for c in first if bad(c)}=={c.attrib['name'] for c in again}
focused=read(b/'native/focused-results.json');assert focused['passed']==21 and focused['remaining_failures']==0 and focused['skipped']==0
inspectioncounts={}
for name,entry in summary['visual_inspection_records'].items():
 record=read(b/name);actual=len(record.get('inspections',record.get('actual_images_read',[])));assert actual==entry['record_count'];assert sha(b/name)==entry['sha256'];inspectioncounts[name]=actual
cleanup=read(b/'native/display-cleanup.json');assert cleanup['owned_remaining_before_cleanup']==[] and cleanup['owned_remaining_after_cleanup']==[] and cleanup['other_display_processes_left_untouched']==2;assert sha(b/'native/display-cleanup.json')==summary['display_cleanup']['sha256']
processes=[]
for proc in Path('/proc').iterdir():
 if not proc.name.isdigit():continue
 try:
  comm=(proc/'comm').read_text().strip()
  if comm not in ('Xvfb','openbox'):continue
  args=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace');ownedrefs=[]
  for fd in (proc/'fd').iterdir():
   try:
    target=str(fd.resolve());
    if str(b) in target:ownedrefs.append(target)
   except (OSError,PermissionError):pass
  assert not ownedrefs and str(b) not in args
  processes.append({'pid':int(proc.name),'comm':comm,'current_marathi_artifact_reference_found':False})
 except (FileNotFoundError,PermissionError):continue
result={'reviewer' :'/root/locale_integration','mode':'Independent read-only finalized-native input/report audit; no native/browser reruns, no source changes, no fullPNGmanualscan.','validation_head':head,'summary_sha256':sha(b/'native-final-summary.json'),'gate_inputs_sha256':sha(b/'native-gate-inputs.json'),'verdict':'PASS scopednative evidence recommendations; producer64fullrendering plus187normal-resource registration plus0e6browser-test-only carry remain explicit.','official_jobs_exit0':34,'focused_cases':{'unique_final_pass':21,'initial_pass':18,'initial_display_errors_retained':3,'targeted_retry_cases':3,'targeted_retry_pass':3,'retry_exactly_failed_initial_cases':True},'matrix_recomputed':recomputed,'variant_proof':{'native_keys':3411,'online_keys':2836,'online_variants':2863,'total_variants':6274,'canonical_hashes':summary['catalog_sha256'],'method':'Previewcatalog raw native values andparsedonline values exactlycanonical. Every sample identitycheckedatall6size/scalecombinations; every wrappedlinecharactersequence equals itscanonicalvariant afterwhitespaceremoval. No genericother-language corpus used.'},'overlay_raw_value_equivalence_count':38,'all_report_hashes_verified':42,'unchanged_game_draw_checksum_captures_verified':drawchecks,'registered_normal_resource_followups':4,'inspected_image_ref_records':inspectionfiles,'inspected_image_sha_refs_verified':len(images),'inspected_images':sorted(set(images)),'diagnostic_interpretation':'All592rawdiagnostics are scale2 width-only rasterextents. Fullbitmaps areblittedscaled intorecordedproductiondestinations; all83542drawnlinesfit. No warningdiscarded orreclassified asmissingglyph.','scope_limits':gate['explicit_omissions']+['Imagehash/referenceverificationdoesnotconstituteactualvisualinspectionbythisauditor; namedreviewersactualreads remainseparate.','Knownraw592warnings preserved; desktopdev-fastnativeharness notshippingperformanceevidence.','Registrationmetadatachangedwhilephonefontmatrixwasrunning; transitionrecord distinguishesresourceconsumption epochs ratherthanrelabeling64codeproducer.'],'all_gate_reference_hashes':gate['evidence_sha256'],'finding_count':0,'final_visual_inspection_counts':inspectioncounts,'display_cleanup_verified':cleanup,'read_only_display_process_snapshot':processes,'prior_audit_preserved':{'path':'native-independent-review-before-final-inspection-count.json','sha256':sha(b/'native-independent-review-before-final-inspection-count.json'),'scope':'Originalpriorhashbindingretained; finalnewsummary/gatecheckedseparately.'}}
(b/'native-independent-review-final.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps({'verdict':result['verdict'],'report_sha256':sha(b/'native-independent-review-final.json'),'matrix':recomputed,'images':len(images),'drawchecks':drawchecks},indent=2))
