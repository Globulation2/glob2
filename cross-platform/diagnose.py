from pathlib import Path
import struct,json
p=Path(__file__).resolve().parent
out=[]
for case,name in [('continuation','initial.game'),('continuation','final.game')]:
 a=(p/'devlaptop/runs'/case/'base'/name).read_bytes();b=(p/'devlaptop/runs'/case/'head'/name).read_bytes()
 diffs=[i for i,(x,y) in enumerate(zip(a,b)) if x!=y]
 pos=a.rfind(b'\x00\x00\x00\x05polls',0,min(i for i in diffs if i>55))
 count=struct.unpack_from('>I',a,pos-4)[0];names=[]
 def u32():
  global pos
  n=struct.unpack_from('>I',a,pos)[0];pos+=4;return n
 def string():
  global pos
  n=u32();s=a[pos:pos+n].decode();pos+=n;return s
 for _ in range(count):
  names.append(string());string();string();u32();u32()
 def sample(label):
  global pos
  tick=u32();available=u32()
  for field in names:
   start=pos;pos+=16
   changed=[i for i in diffs if start<=i<pos]
   if changed:out.append({'file':name,'sample':label,'tick':tick,'field':field,'offset':start,'changed_offsets':changed,'base_words':struct.unpack_from('>4I',a,start),'head_words':struct.unpack_from('>4I',b,start)})
 sample('current')
 for i in range(u32()):sample('history '+str(i))
 covered={i for r in out if r['file']==name for i in r['changed_offsets']}
 assert set(diffs)==covered|set(range(35,55)),(name,count,diffs,covered)
(p/'save-diagnosis.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
