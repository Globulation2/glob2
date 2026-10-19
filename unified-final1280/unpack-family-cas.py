"""Verify and unpack published benchmark evidence; never executes game commands."""
import hashlib,json,sys,tarfile
from pathlib import Path
root=Path(__file__).resolve().parent
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'decoded-family-comparisons'
index=json.loads((root/'FAMILY-CAS.json').read_text())
with tarfile.open(root/'family-cas.tar.gz') as archive:
 for row in index['entries']:
  relative=Path(row['path']);assert not relative.is_absolute() and '..' not in relative.parts
  data=archive.extractfile('objects/'+row['sha256']).read()
  assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
  dest=out/relative;dest.parent.mkdir(parents=True,exist_ok=True)
  if dest.exists():assert dest.read_bytes()==data,'Refuse overwriting different evidence'
  else:dest.write_bytes(data)
print(f'Verified {len(index["entries"])} logical files in {out}')
