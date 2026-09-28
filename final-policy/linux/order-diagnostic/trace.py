from pathlib import Path
import json,struct,subprocess,time,sys,re,mmap
R=Path('/home/bradley/glob2-maxima-final');D=R/'artifacts/final-maxima/order-diagnostic';D.mkdir(parents=True,exist_ok=True)
# Do not compete with the quiet benchmark.
while not (R/'artifacts/final-maxima/benchmark/results.json').exists():time.sleep(15)
base=R/'artifacts/final-maxima/g1'
cmd=json.loads((base/'command.json').read_text());cmd[cmd.index('--ticks')+1]='6200';cmd[cmd.index('--output-dir')+1]=str(D/'full');cmd+=['--replay','true']
if "--parse-only" not in sys.argv:
 for name,command in [('full',cmd),('resumed',[cmd[0],'--run-game','--load-game',str(D/'full/checkpoint-4096.game'),'--ticks','6200','--telemetry','checksums','--replay','true','--output-dir',str(D/'resumed')])]:
  out=D/name;out.mkdir(exist_ok=True);(out/'command.json').write_text(json.dumps(command,indent=2))
  with (out/'run.log').open('w') as f:subprocess.run(command,cwd=R,stdout=f,stderr=f,check=True)
  print('RAN',name,flush=True)
version=int(re.search(r'#define VERSION_MINOR (\d+)',(R/'src/Version.h').read_text()).group(1))
def parse(path,start):
 data=path.read_bytes();marker=struct.pack('>HH',0,version);at=-1;matches=[]
 while True:
  at=data.find(marker,at+1)
  if at<0:break
  p=at+4;t=start;rows=[]
  try:
   while p<len(data):
    delta,n=struct.unpack_from('>II',data,p);p+=8
    if delta>6200 or not 1<=n<=32769 or p+n+5>len(data):raise ValueError()
    t+=delta;typ=data[p];payload=data[p+1:p+n];sender=data[p+n];checksum=struct.unpack_from('>I',data,p+n+1)[0];p+=n+5
    if typ==51:
     if n!=1 or p!=len(data) or t!=6200:raise ValueError()
     break
    if typ not in [20,22,23,24,30,31,32,35,37,38,39,40,41,42,43,44,59,67,71,73,74,100] or sender>1:raise ValueError()
    row=dict(tick=t,sender=sender,type=typ,payload=payload.hex())
    if typ==20 and len(payload)==28:row['create']=dict(zip(['team','x','y','building_type','workers','future_workers','flag_radius'],struct.unpack('>7i',payload)))
    rows.append(row)
   if p==len(data) and rows:matches.append((at,rows))
  except (ValueError,struct.error):pass
 assert len(matches)==1,[(x,len(r)) for x,r in matches]
 return matches[0]
aoff,a=parse(D/'full/game.replay',0);boff,b=parse(D/'resumed/game.replay',4096)
(D/'full/orders.json').write_text(json.dumps(a,indent=2));(D/'resumed/orders.json').write_text(json.dumps(b,indent=2));a=[x for x in a if x['tick']>4096]
first=next((i for i,(x,y) in enumerate(zip(a,b)) if x!=y),min(len(a),len(b)))
result={'full_header_offset':aoff,'resume_header_offset':boff,'full_orders_after_checkpoint':len(a),'resumed_orders':len(b),'first_difference_index':first,'full_context':a[max(0,first-3):first+4],'resumed_context':b[max(0,first-3):first+4]}
sys.path.insert(0,str(R/'test'));from compare_save_continuation import compare
for label,left,right in [('trace_matches_original',base/'game.replay.checksums',D/'full/game.replay.checksums'),('resume_continuity',D/'full/game.replay.checksums',D/'resumed/game.replay.checksums')]:
 try:result[label]=compare(left,right)
 except ValueError as e:result[label]=str(e)
(D/'comparison.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2),flush=True)
