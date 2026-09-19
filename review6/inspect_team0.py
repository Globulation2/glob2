"""Read-only frozen-save building inspection; validates every slot/gid in team0 array.
Building::save record180bytes(type172,gid8,xy10/14), each preceded by isUsed4bytes.
"""
from pathlib import Path
import struct,json,gzip
root=Path(__file__).parent
b=gzip.open(root/'play-512-a2-s11/final.game.gz','rb').read()
pat=struct.pack('>IIIHii',1,1,0,0,57,232)
starts=[];j=0
while True:
 j=b.find(pat,j)
 if j<0:break
 starts.append(j);j+=1
for begin in starts:
 try:
  p=begin;out=[]
  for slot in range(1024):
   used=struct.unpack_from('>I',b,p)[0];p+=4
   assert used in [0,1]
   if not used:continue
   state,cs,gid,x,y=struct.unpack_from('>IIHii',b,p)
   assert gid==slot and 0<=x<512 and 0<=y<512
   hp=struct.unpack_from('>i',b,p+116)[0];typ,seen=struct.unpack_from('>II',b,p+172)
   assert typ<100
   out.append(dict(gid=gid,type=typ,x=x,y=y,hp=hp,state=state,construction_result=cs));p+=180
  print(json.dumps(out,indent=2));break
 except (AssertionError,struct.error):continue
else:raise RuntimeError('No fully validated team0 building array')
