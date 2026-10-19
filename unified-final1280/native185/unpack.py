"""Verify and decode the raw native185 proof: python3 unpack.py OUTPUT_DIR."""
from pathlib import Path
import hashlib,json,sys,tarfile
B=Path(__file__).resolve().parent;dest=Path(sys.argv[1]).resolve();receipt=json.loads((B/'CAS-VERIFIED.json').read_text());indexfile=B/'CAS.json';archive=B/'native185-cas.tar.gz'
def h(p):return hashlib.sha256(p.read_bytes()).hexdigest()
assert h(indexfile)==receipt['index_sha256'];assert h(archive)==receipt['archive_sha256'];index=json.loads(indexfile.read_text())
with tarfile.open(archive) as tar:
 for row in index['entries']:
  relative=Path(row['path']);assert not relative.is_absolute() and '..' not in relative.parts
  data=tar.extractfile('objects/'+row['sha256']).read();assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
  p=dest/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
print('Verified',len(index['entries']),'files into',dest)
