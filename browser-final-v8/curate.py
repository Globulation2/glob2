from pathlib import Path
import json,hashlib,struct
R=Path.cwd();O=Path(__file__).resolve().parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();h=json.loads((O/'handoff.json').read_text())
images=sorted([*O.glob('startup-*.png'),*O.joinpath('scenes-retry').glob('*.png'),*O.joinpath('matrix').glob('*.png')]);rows=[]
for p in images:
 w,height=struct.unpack('>II',p.read_bytes()[16:24]);rows.append({'path':str(p.relative_to(R)),'sha256':sha(p),'width':w,'height':height,'producer_revision':h['revision']})
(O/'completed-image-index.json').write_text(json.dumps({'producer_revision':h['revision'],'source_closure_head':h['actual_head_at_freeze'],'scope':'Actual final48 completed immutable PNGs; no blanket visual approval implied','images':rows},indent=2)+'\n')
texts=[p for p in O.rglob('*')if p.is_file()and p.suffix in ['.json','.log','.md','.cjs','.py','.txt','.patch','.jsonl']and not any(x in p.relative_to(O).parts for x in ['static','test-results','html-report','__pycache__']) and p.name!='publish-selection.json']
(O/'publish-selection.json').write_text(json.dumps({'producer_revision':h['revision'],'source_closure_head':h['actual_head_at_freeze'],'scope':'Explicit actual evidence and preserved failures; no static binaries, dependency directories or traceZIPs','paths':[str(p.relative_to(R))for p in sorted(set(images+texts))]},indent=2)+'\n');print(len(images),'PNG;',len(texts),'text files')
