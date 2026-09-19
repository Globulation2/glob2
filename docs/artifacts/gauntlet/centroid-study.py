import concurrent.futures,subprocess,json,math,os,collections
ROOT='/tmp/gauntlet-evidence/centroid-review-v9'
BIN='/tmp/gauntlet-evidence/glob2-v9'
def inspect(path):
 r=json.load(open(path+'/result.json')); report=r.get('map_report')
 if not report:return {'error':r.get('diagnostic',r.get('status'))}
 with open(path+'/terrain.txt') as f:w,h=map(int,f.readline().split());a=[list(map(int,l.split())) for l in f]
 seen=set(); bs=[]
 for y in range(h):
  for x in range(w):
   if a[y][x]!=7 or (x,y) in seen:continue
   q=[(x,y)];seen.add((x,y))
   for xx,yy in q:
    for dx,dy in [(1,0),(-1,0),(0,1),(0,-1)]:
     p=((xx+dx)%w,(yy+dy)%h)
     if a[p[1]][p[0]]==7 and p not in seen:seen.add(p);q.append(p)
   bs.append(q)
 out=[];level=r['request']['options']['starting-towers']
 starts=report['map']['colonies']; owned={c['team']:[] for c in starts}
 for b in bs:
  bx=sum(x for x,y in b)/len(b);by=sum(y for x,y in b)/len(b)
  c=min(starts,key=lambda c:(c['start']['x']+2-bx)**2+(c['start']['y']+2-by)**2);owned[c['team']].append(b)
 for c in starts:
  local=owned[c['team']];v=set();poses=[]
  for b in local:
   x=min(p[0] for p in b);y=min(p[1] for p in b);X=max(p[0] for p in b);Y=max(p[1] for p in b);vr=4 if len(b)>4 else 5+level
   poses.append([x,y,len(b)])
   v|={((xx%w),(yy%h)) for xx in range(x-vr,X+vr+1) for yy in range(y-vr,Y+vr+1)}
  cx=sum(x for x,y in v)//len(v);cy=sum(y for x,y in v)//len(v)
  margin=next((rad for rad in range(0,10) if any(a[(cy+dy)%h][(cx+dx)%w] in [4,5,6,8,9] for dx in range(-rad,rad+1) for dy in range(-rad,rad+1))),10)
  out.append({'team':c['team'],'start':[c['start']['x'],c['start']['y']],'buildings':poses,'center':[cx,cy],'terrain':a[cy][cx],'resource_margin':margin})
 return out

def run(spec):
 n,level,seed,size=spec;path=f'{ROOT}/w{size}-n{n}-l{level}-s{seed}';os.makedirs(path,exist_ok=True)
 cmd=[BIN,'--generate-map','--generator','58','--map-seed',str(seed),'--param',f'teams={n}','--param',f'width={size}','--param',f'height={size}','--param',f'starting-towers={level}','--report','terrain','--output-dir',path]
 p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 result={'n':n,'level':level,'seed':seed,'size':2**size,'exit':p.returncode}
 if p.returncode:result['error']=p.stdout[-2000:]
 else:
  try:result['homes']=inspect(path)
  except Exception as e:result['error']=repr(e)
 return result
if __name__=='__main__':
 specs=[(n,l,s,size) for size,ns in [(8,[2,4,8]),(9,[2,4,12])] for n in ns for l in [0,1,2,3] for s in range(1,7)]
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool,open(ROOT+'/results.jsonl','w') as f:
  for r in pool.map(run,specs): f.write(json.dumps(r)+'\n');f.flush();print(r['n'],r['level'],r['seed'],r.get('error','ok'),flush=True)
