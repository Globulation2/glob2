from pathlib import Path
from collections import Counter
import json,hashlib,sys
base=Path('artifacts/marathi-localization/integration-bc8');reports=[]
for viewport in ['1280x900','390x844']:
 folder=base/'native-render-clean64'/viewport/'matrix';p=folder/'font-matrix.json'
 if not p.exists():continue
 d=json.loads(p.read_text(encoding='utf-8'));samples=d['samples'];lines=[(s,l) for s in samples for l in s['lines']];raw=[];drawn=[]
 for s,l in lines:
  common={k:s[k] for k in ['surface','key','branch','size','scale']}
  if not l['fits_width'] or not l['fits_height']:raw.append({**common,'line':l})
  if not l['drawn_fits_width'] or not l['drawn_fits_height']:drawn.append({**common,'line':l})
 missing=[{k:s[k] for k in ['surface','key','branch','size','scale']} for s in samples if not s['has_glyphs']]
 coverage=Counter((s['surface'],s['key'],s['branch']) for s in samples);expected=set()
 for surface in ['native','online']:
  catalog=json.loads((base/'previews/mr'/(surface+'.json')).read_text(encoding='utf-8'))
  for key,value in catalog.items():
   for branch in (value if isinstance(value,dict) else ['']):expected.add((surface,key,branch))
 assert set(coverage)==expected and all(c==6 for c in coverage.values())
 record={'report':str(p),'report_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'producer':d['evidence_identity'],'sample_count':len(samples),'line_count':len(lines),'unique_variants':len(coverage),'every_variant_at_each_10_13_20px_scale1_2':True,'sample_counts':{str(k):v for k,v in Counter((s['surface'],s['size'],s['scale']) for s in samples).items()},'missing_glyphs':missing,'raw_raster_diagnostics':raw,'drawn_bounds_failures':drawn,'png_pages':len(list(folder.glob('*.png'))),'interpretation':'Raw raster rectangles may exceed logical destinations at scale2. They are never cropped: the production helper draws the whole bitmap scaled into its recorded destination. Drawn bounds and actual inspected images are evaluated separately. Structured checks do not assert every PNG was human-read.'}
 (folder/'matrix-diagnostics.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');reports.append({k:record[k] for k in ['sample_count','line_count','unique_variants','png_pages'] }|{'viewport':viewport,'missing':len(missing),'raw':len(raw),'drawn_failures':len(drawn)})
print(json.dumps(reports,indent=2))
