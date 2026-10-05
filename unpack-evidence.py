"""Restore named evidence files after extracting gradient-preparation-evidence.tar.gz."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('evidence',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
root=a.output.resolve();root.mkdir(parents=True,exist_ok=True)
for row in json.loads((a.evidence/'manifest.json').read_text()):
 data=(a.evidence/row['blob']).read_bytes()
 if hashlib.sha256(data).hexdigest()!=row['sha256']:raise ValueError('Hash mismatch: '+row['path'])
 dest=(root/row['path']).resolve()
 if not dest.is_relative_to(root):raise ValueError('Invalid path: '+row['path'])
 dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
print('Restored and verified all evidence files to',root)
