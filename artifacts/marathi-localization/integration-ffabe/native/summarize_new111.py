from pathlib import Path
import json,hashlib
from PIL import Image
B=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
selected=json.loads((B.parent/'final-targets111.json').read_text(encoding='utf-8'));reports=[];missing=[];drawn=[];raw=[];allpages=[];samples=lines=0
for view in ['1280x900','390x844']:
 p=B/'render-refresh'/view/'matrix/font-matrix.json';r=json.loads(p.read_text());actual={(s['key'],s['size'],s['scale']) for s in r['samples']};expected={(k,z,q) for k in selected for z in (10,13,20) for q in (1,2)};assert actual==expected and len(r['samples'])==666
 pages=set();rowlines=0
 for s in r['samples']:
  assert s['surface']=='online' and s['branch']==''
  if not s['has_glyphs']:missing.append({'viewport':view,**s})
  for l in s['lines']:
   pages.add(l['png']);rowlines+=1
   record={'viewport':view,'key':s['key'],'size':s['size'],'scale':s['scale'],**l}
   if not l['drawn_fits_width'] or not l['drawn_fits_height']:drawn.append(record)
   if not l['fits_width'] or not l['fits_height']:raw.append(record)
 # Every complete emitted page is measured, but only listed view_image entries are human inspected.
 for name in pages:
  im=Image.open(p.parent/name).convert('RGB');ext=im.getextrema();assert any(hi>lo for lo,hi in ext),name
 allpages +=[str(p.parent/x) for x in pages];samples+=len(r['samples']);lines+=rowlines
 reports.append({'path':str(p),'sha256':sha(p),'viewport':view,'samples':len(r['samples']),'lines':rowlines,'png_pages':len(pages),'evidence_identity':r['evidence_identity']})
assert samples==1332 and lines==1430 and not missing and not drawn and len(raw)==6
assert all(d['scale']==2 and d['fits_height'] and not d['fits_width'] and d['drawn_fits_width'] and d['drawn_fits_height'] for d in raw)
out={'actual_new_key_count':111,'actual_new_samples':samples,'actual_new_lines':lines,'missing_glyphs':len(missing),'drawn_bounds_failures':len(drawn),'raw_raster_diagnostic_count':len(raw),'raw_diagnostics':raw,'reports':reports,'png_page_count':len(allpages),'every_complete_page_nonuniform_pixel_probe':True,'all_111_six_combinations_each_viewport_coverage':True,'old_actual_matrix_carry':{'samples':75288,'lines':83542,'missing':0,'drawn':0,'raw':592,'proof_path':'native-font-carry-inputs.json','proof_sha256':sha(B/'native-font-carry-inputs.json')},'combined_behaviorally_carried_plus_new_measured':{'samples':76620,'lines':84972,'missing':0,'drawn':0,'raw':598},'limits':['Old matrix remains old actual producer64 evidence; it is carried by exact old-target/font/algorithm equality plus the provably inactive Malay-only font predicate. It is not relabelled newly measured.','New matrix uses actual official production font/wrap implementation with selected111 static text values; placeholders shown literally, no dynamic-format integration claim.','Nonuniform pixel check rejects blank/all-black pages but is not human review of all glyphs.','Raw scale2 width diagnostics describe full raster rectangles; the whole bitmap is resampled into the recorded fitting draw destination.']}
(B/'new111-font-diagnostics.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(samples,lines,len(allpages))
