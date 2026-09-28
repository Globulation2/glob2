from pathlib import Path
import json,gzip,hashlib
p=Path(__file__).resolve().parent
for r in json.loads((p/'manifest.json').read_text()):
 root=p if r['host']=='macOS' else p/r['host']
 dest=(root/'baseline-repeat'/r['file']) if r['variant']=='baseline-repeat' else root/'runs'/r['case']/r['variant']/r['file']
 b=gzip.decompress((p/r['object']).read_bytes());assert len(b)==r['bytes'] and hashlib.sha256(b).hexdigest()==r['sha256']
 dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b)
