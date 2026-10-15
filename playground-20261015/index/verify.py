from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
m=json.loads((root/'index/content-manifest.json').read_text())
for row in m['files']:
 with(root/row['portable']).open('rb')as f:assert hashlib.file_digest(f,'sha256').hexdigest()==row['sha256'],row['portable']
print('Every selected byte verified; omitted checksum/replay/binary bodies are not verified by this bundle')
