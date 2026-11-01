from pathlib import Path
import json,gzip,hashlib,collections
R=Path(__file__).resolve().parents[3];B=R/'artifacts/bulgarian';O=B/'native-captures/v9-final';frozen=json.loads((B/'v9-frozen-inputs.json').read_text());count=collections.Counter();failures=[];jobs=[];combinations=set();peaks=[]
for receipt in sorted(O.glob('fonts-full-corpus-shard-*/execution.json')):
 d=json.loads(receipt.read_text())
 for job in d['jobs']:
  assert job['returncode']==0;folder=receipt.parent/job['tag'];packed=folder/'font-matrix.json.gz';raw=gzip.decompress(packed.read_bytes());assert hashlib.sha256(raw).hexdigest()==job['diagnostics_sha256'];manifest=json.loads(raw);assert len(manifest['samples'])==frozen['final_render_values'];identity=manifest['evidence_identity'];assert identity['producer']['revision']==frozen['compiled_revision'];assert identity['input_metadata']['final_source_revision']==frozen['final_source_revision'];keys=set()
  for e in manifest['samples']:
   keys.add((e['surface'],e['key'],e['branch']));count['samples']+=1
   if not e['has_glyphs']:failures.append([job['tag'],e['key'],'missing-glyph'])
   for line in e['lines']:
    count['lines']+=1
    for flag in ['drawn_fits_width','drawn_fits_height']:
     if not line[flag]:failures.append([job['tag'],e['key'],flag,line['text']])
    if '\ufffd'in line['text']:failures.append([job['tag'],e['key'],'replacement-character'])
  assert len(keys)==frozen['final_render_values'];combinations.add((manifest['logical_width'],manifest['selected_size'],manifest['selected_scale']));storage=json.loads((folder/'storage-observation.json').read_text());peaks.append(storage['peak_combined_font_output_bytes']);jobs.append({'tag':job['tag'],'path':str(packed.relative_to(R)),'sha256':hashlib.sha256(packed.read_bytes()).hexdigest(),'raw_sha256':job['diagnostics_sha256'],'samples':len(manifest['samples']),'storage_returncode':storage['returncode']})
assert len(jobs)==12 and len(combinations)==12;assert count['samples']==frozen['expected_full_font_samples']==77220
summary={'status':'Actual FULL final font corpus; no old-measurement carry substituted','compiled_revision':frozen['compiled_revision'],'final_source_revision':frozen['final_source_revision'],'source_sha256':frozen['source_sha256'],'catalog_sha256':frozen['catalog_sha256'],'values_per_combination':frozen['final_render_values'],'combinations':sorted(combinations),'samples':count['samples'],'lines':count['lines'],'failures':failures,'all_glyphs_present':not failures,'all_drawn_extents_fit':not failures,'jobs':jobs,'font_storage_limit_bytes':1073741824,'peak_observed_combined_output_bytes':max(peaks),'measurement_truncation':False,'limits':['Raw raster overhang flags remain diagnostics; production drawn destination flags determine fitted displayed extents','Matrix shows literal placeholders/raw catalog escapes; representative formatted values and actual UI are checked separately','2x font raster scale is not certification of physical-device native-window high-DPI behavior']};(O/'full-font-measurement-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n');print('FULL FONT',count['samples'],'samples',count['lines'],'lines; failures',len(failures),'peak MiB',round(max(peaks)/1048576,1));print(failures[:3])
