from pathlib import Path
import hashlib,json,struct,sys
D=Path(sys.argv[1]);rows=[]
for case in ('g1','g52','v115','v117','crowded'):
 for suffix in ('','resumed'):
  p=D/case/suffix/'game.replay.checksums'
  with p.open('rb') as f:header=f.read(20);f.seek(0);digest=hashlib.file_digest(f,'sha256').hexdigest()
  rows.append({'case':case,'run':suffix or 'full','ticks':struct.unpack_from('<I',header,12)[0],'sha256':digest,'bytes':p.stat().st_size})
print(json.dumps(rows,indent=2))
