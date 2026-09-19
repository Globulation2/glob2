"""Retain every simulation tick, aggregate checksum and SHA-256 of detailed records."""
import gzip,hashlib,json,lzma,pathlib,struct,sys
source=pathlib.Path(sys.argv[1]);dest=pathlib.Path(sys.argv[2]);opener=gzip.open if source.suffix=='.gz' else lzma.open if source.suffix=='.xz' else open
whole=hashlib.sha256()
with opener(source,'rb') as src,gzip.open(dest,'wt') as out:
 def read(n):
  data=src.read(n)
  if len(data)!=n:raise ValueError('Truncated checksum stream')
  whole.update(data);return data
 header=read(20)
 if header[:4]!=b'GCS1':raise ValueError('Invalid signature')
 teams,players,count,flags=struct.unpack_from('<4I',header,4)
 out.write('tick\taggregate\tdetailed_sha256\n')
 for _ in range(count):
  tick,aggregate=struct.unpack('<II',read(8));digest=hashlib.sha256()
  for _ in range(teams):
   digest.update(read(4))
   for _ in range(2):
    b=read(4);digest.update(b);entities=struct.unpack('<I',b)[0]
    for _ in range(entities):
     b=read(10);digest.update(b);fields=struct.unpack_from('<I',b,6)[0];digest.update(read(4*fields))
  out.write(f'{tick}\t{aggregate:08x}\t{digest.hexdigest()}\n')
 if src.read(1):raise ValueError('Trailing checksum data')
meta=dict(source_sha256=whole.hexdigest(),teams=teams,players=players,ticks=count,flags=flags)
dest.with_name(dest.name+'.json').write_text(json.dumps(meta,indent=2));print(meta)
