from pathlib import Path
import json,gzip,hashlib,shutil
p=Path(__file__).resolve().parent
for r in json.loads((p/'manifest.json').read_text()):
 root=p if r['host']=='macOS' else p/r['host']
 dest=root/'runs'/r['case']/r['variant']/r['file']
 b=gzip.decompress((p/r['object']).read_bytes());assert len(b)==r['bytes'] and hashlib.sha256(b).hexdigest()==r['sha256']
 dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b)
for name in ['results.json','run-plan.json']:shutil.copy2(p/'macOS'/name,p/name)
