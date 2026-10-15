"""Explicit post-completion compact extras: plotting recovery and independent PG448 audit."""
import hashlib,json
from pathlib import Path
O=Path(__file__).resolve().parent;F=O.parent;A=F.parents[1]/'fresh-audit-causal/playground448-final'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=O/'resolved-selection-attempt1.json';dest=O/'resolved-selection-final.json';assert not dest.exists()
j=json.loads(source.read_text());assert j['state']=='resolved';review=json.loads((A/'review.json').read_text());assert review['state']=='independently_verified_448_playground_only'and review['runs']==448
extra=[(p,'evidence/full-design-comparison/playground-stop/plot-environment-recovery/'+p.name)for p in(O/'plot-environment-recovery').iterdir()if p.is_file()and p.suffix in('.json','.log')]
extra += [(p,'evidence/independent/playground448-final/'+p.name)for p in A.iterdir()if p.is_file()and p.suffix in('.json','.py')]
extra += [(O/name,'evidence/full-design-comparison/playground-stop/'+name)for name in('materialize-evidence.py','augment-selection.py')]
known={x['portable']for x in j['files']};added=[]
for p,alias in extra:
 if alias in known:continue
 row={'source':str(p),'portable':alias,'bytes':p.stat().st_size,'declared_sha256':sha(p),'category':'Explicit post-completion plotting-recovery/independent-audit extra','source_aliases':[str(p)]};j['files'].append(row);known.add(alias);added.append(alias)
j['files'].sort(key=lambda x:x['portable']);j['file_count']=len(j['files']);j['bytes']=sum(x['bytes']for x in j['files']);j['augmentation']={'source_selection_sha256':sha(source),'source_script_sha256':sha(Path(__file__)),'added':added,'notes':'No binary/source-policy changes. Plot failure/recovery metadata and actual independent448audit are retained; no publication.'}
with dest.open('x')as f:f.write(json.dumps(j,indent=2)+'\n')
print('finalselection',len(j['files']),j['bytes'],'bytes',len(added),'extras')
