"""Curate completed actual final-native evidence; never infer visual reviewer approval."""
from pathlib import Path
import json,hashlib,gzip,xml.etree.ElementTree as ET
R=Path(__file__).resolve().parents[3];B=R/'artifacts/bulgarian';O=B/'native-captures/v9-final';Q=B/'native-v9-qa';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen=json.loads((B/'v9-frozen-inputs.json').read_text());paths=set();images=[];jobs=[];draws=[];identities=[]
for p in sorted(O.glob('review-index-*.json')):
 d=json.loads(p.read_text());images+=d['images'];jobs+=d['jobs'];paths.add(p)
assert len(jobs)==35 and len({e['path']for e in images})==len(images)==108
for p in sorted(O.glob('surfaces-shard-*/execution.json'))+[O/'api/execution.json',O/'schools/execution.json']:
 paths.add(p);d=json.loads(p.read_text());assert all(e['returncode']==0 for e in d['jobs'])
 for e in d['jobs']:
  folder=p.parent/e['tag'];paths.add(folder/'capture.log')
  for f in folder.glob('*.json'):
   paths.add(f);v=json.loads(f.read_text());identity=v.get('evidence_identity')
   if identity:
    assert identity['producer']['revision']==frozen['compiled_revision'];assert identity['input_metadata']['final_source_revision']==frozen['final_source_revision'];identities.append({'path':str(f.relative_to(R)),'producer_revision':identity['producer']['revision'],'source_revision':identity['source_revision'],'sdl':identity['sdl_version'],'ttf':identity['sdl_ttf_version'],'harfbuzz':identity['harfbuzz_version'],'SDL_path':identity.get('sdl_library_path'),'TTF_path':identity.get('sdl_ttf_library_path')})
   for capture in v.get('captures',[]):
    if 'checksum_before_draw'in capture:
     assert capture['checksum_before_draw']==capture['checksum_after_draw'];draws.append({'path':str(f.relative_to(R)),'capture':capture['name'],'checksum':capture['checksum_before_draw']})
for e in images:
 p=R/e['repository_relative_path'];assert sha(p)==e['sha256'];paths.add(p)
font_pages=[];retained_font_pages=[]
for receipt in O.glob('fonts-full-corpus-shard-*/execution.json'):
 paths.add(receipt);d=json.loads(receipt.read_text());assert len(d['jobs'])==4 and all(e['returncode']==0 for e in d['jobs'])
 for e in d['jobs']:
  folder=receipt.parent/e['tag'];paths|={folder/'font-matrix.json.gz',folder/'capture.log',folder/'storage-observation.json'};pages=sorted(folder.glob('*.png'));assert pages;paths.add(pages[0]);retained_font_pages.append(str(pages[0].relative_to(R)))
  for p in pages:font_pages.append({'path':str(p.relative_to(R)),'sha256':sha(p),'bytes':p.stat().st_size})
paths.add(O/'probes/execution.json')
for folder in [O/'probes/desktop',O/'probes/phone']:paths.update(folder.glob('*.png'));paths|={folder/'font-matrix.json',folder/'capture.log'}
assert len(retained_font_pages)==12
inventory={'status':'Complete generated atlas page inventory; bounded public selection does not omit measurement diagnostics','generated_pages':len(font_pages),'generated_page_bytes':sum(e['bytes']for e in font_pages),'public_example_pages':retained_font_pages,'all_complete_measurements':'full-font-measurement-summary.json plus12 complete font-matrix.json.gz inputs','pages':font_pages};raw=(json.dumps(inventory,indent=2)+'\n').encode();packed=O/'font-generated-page-inventory.json.gz';packed.write_bytes(gzip.compress(raw,compresslevel=9,mtime=0));assert gzip.decompress(packed.read_bytes())==raw;paths.add(packed)
plan=O/'full-font-plan.json';packed=O/'full-font-plan.json.gz';packed.write_bytes(gzip.compress(plan.read_bytes(),compresslevel=9,mtime=0));paths.add(packed)
for p in O.glob('*.json'):
 if p.name not in ['full-font-plan.json','publish-selection.json']:paths.add(p)
paths.add(O/'probes/synthetic-equivalence.json')
for folder in O.glob('review-receipts-*'):paths.update(folder.glob('*.json'))
for p in Q.iterdir():
 if p.is_file()and p.suffix in ['.json','.py','.txt','.log','.patch']:paths.add(p)
for base in [B/'v9-full-render-input',B/'v9-probe-input']:paths.update(base.rglob('*.json'))
paths.add(B/'v9-frozen-inputs.json')
tests={}
for kind in ['engine','unit']:
 root=B/('native-tests-v9-'+kind);junit=ET.parse(root/'junit.xml').getroot();cases=list(junit.iter('testcase'));failed=[e.attrib for e in cases if e.find('failure')is not None or e.find('error')is not None];assert len(cases)==91 and not failed;tests[kind]={'cases':len(cases),'passed':len(cases),'failed':0,'junit':str((root/'junit.xml').relative_to(R))}
 for name in ['command.json','inventory.json','timings.json','junit.xml','run.log']:paths.add(root/name)
font=json.loads((O/'full-font-measurement-summary.json').read_text());assert font['samples']==77220 and not font['failures'];summary={'status':'Actual final V9 native execution passed; independent visual acceptance is separately recorded','compiled_revision':frozen['compiled_revision'],'final_source_revision':frozen['final_source_revision'],'source_closure':frozen['source_closure'],'source_sha256':frozen['source_sha256'],'catalog_sha256':frozen['catalog_sha256'],'preview_capture_sha256':'d11085a0fff664475f149c5071e822e441660269ec35e4da6436b4d3bd63448f','captures':{'surfaces':29,'API':4,'school_briefings':2,'jobs':35,'PNG_images':108,'all_returncodes_zero':True,'equal_draw_checksum_pairs':len(draws)},'font':{'full_values':6435,'combinations':12,'fresh_samples':77220,'lines':font['lines'],'missing_glyphs':0,'drawn_extent_failures':0,'peak_observed_combined_bytes':font['peak_observed_combined_output_bytes'],'budget_bytes':1073741824,'complete_measurements_truncated':False,'generated_PNG_pages':len(font_pages),'selected_example_pages':12},'probes':{'historical_1x_2x_PNGs':12,'all12_byte_identical_8db':True,'new_1_5x_diagnostic_PNGs':6,'total_synthetic_PNGs':18},'focused_tests':tests,'actual_runtime_identities':identities,'draw_checksum_receipts':draws,'visual_indices':['review-index-001.json','review-index-002.json'],'limits':['Linux SDL/Xvfb only; phone-size presentation does not certify physical mobile/Windows/macOS rendering','Full font samples use1x/2x raster scales; six additional1.5x probe samples are diagnostics, not a full fractional-scale corpus','Independent editor actual108-image review is separate; this worker personally opened only three bounded font example pages','Actual controlled localhost API fixtures do not certify production account/payment/provider flows','Native OS locale resources are independently reviewed and packaging checked separately; this Linux runtime does not execute physical mobile/macOS OS resources','Actual producer revision isEA0; final sourceF30 differs only in the exact bound E2E test-only patch. No old producer has been relabelled'],'history':{'earlierV8_bd2_producer_preserved':True,'initialV9_font_preflight_path_typo_logs_retained':True,'initial_font_preflight_executed_ELF':False}};(O/'native-runtime-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n');paths.add(O/'native-runtime-summary.json')
for p in paths:assert p.is_file()and not any(x in ['profile','profiles','binaries','__pycache__']for x in p.parts),p
selection={'status':'Explicit final V9 runtime evidence; independent visual approval remains separate','compiled_revision':frozen['compiled_revision'],'final_source_revision':frozen['final_source_revision'],'paths':sorted(str(p.relative_to(R))for p in paths),'file_count':len(paths),'bytes':sum(p.stat().st_size for p in paths),'coverage':{'actual_UI_PNGs':108,'bounded_full_font_example_pages':12,'complete_font_diagnostics_gz':12,'synthetic_probe_PNGs':18,'profiles_or_ELF_bytes_included':False},'note':'All77,220 measurements are included in complete compressed diagnostics. Only12 atlas example pages are publicly selected; complete generated-page hash inventory is retained. Unselected atlas pages remain ignored local artifacts, not silently dropped measurements.'};(O/'publish-selection.json').write_text(json.dumps(selection,indent=2)+'\n');print('Curated',len(paths),'files',round(selection['bytes']/1048576,2),'MiB; UI108,generatedfontpages',len(font_pages),',equalchecksums',len(draws))
