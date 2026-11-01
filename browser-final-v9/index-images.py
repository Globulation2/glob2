from pathlib import Path
import json,hashlib,struct,argparse
p=argparse.ArgumentParser();p.add_argument('--name',required=True);a=p.parse_args();O=Path(__file__).resolve().parent;R=Path.cwd();h=json.loads((O/'handoff.json').read_text());rows=[]
for q in sorted([*O.glob('startup-*.png'),*O.joinpath('focused').glob('*.png'),*O.joinpath('phone-retry').glob('*.png'),*O.joinpath('editor-diagnostic').glob('*.png')]):
 w,height=struct.unpack('>II',q.read_bytes()[16:24]);rows.append({'path':str(q.relative_to(R)),'sha256':hashlib.sha256(q.read_bytes()).hexdigest(),'width':w,'height':height,'producer_revision':h['revision']})
p=O/a.name;assert not p.exists();p.write_text(json.dumps({'producer_revision':h['revision'],'scope':'Actual completed immutable PNGs only; capture production is separate from independent visual approval','images':rows},indent=2)+'\n');print(len(rows),'actual completed PNGs')
