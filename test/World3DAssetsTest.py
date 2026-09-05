"""Check the actual baked meshes, including topology-changing animation."""
import json,math,struct,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'data/models3d'
manifest=json.loads((root/'manifest.json').read_text())
assert len(manifest)==7
for name,info in manifest.items():
 data=(root/(name+'.g3d')).read_bytes();assert data[:4]==b'G3D1'
 count,=struct.unpack_from('<I',data,4);assert count==16
 offset=8;hashes=set()
 for i in range(count):
  n,=struct.unpack_from('<I',data,offset);offset+=4
  assert n==info['triangles'][i]*3 and n>0 and n%3==0
  chunk=data[offset:offset+n*40];assert len(chunk)==n*40
  hashes.add(hashlib.sha256(chunk).hexdigest())
  for v in struct.iter_unpack('<10f',chunk):
   assert all(math.isfinite(x) for x in v)
   assert -.001<=v[2]<100
   assert .9<sum(x*x for x in v[3:6])<1.1
  offset+=n*40
 assert offset==len(data)
 assert len(hashes)>1, name+' lost its animation'
 print(name,count,'frames',len(hashes),'distinct poses')
