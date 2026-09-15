from pathlib import Path
import sys,json,csv,re
from statistics import mean
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root))
from tools.tournaments.results import Results
from tools.tournaments.game_telemetry import parse
base=Path(__file__).resolve().parent;name=sys.argv[1];out=base/name;rows=[];games=[]
def collect(directory,variant):
 s=Results(directory)
 for a in s:
  if a['category']!='success':continue
  j=a['job'];seed=j['labels']['map_seed'];rot=j['labels']['rotation'];n=a['result'];latest={}
  with s.open_artifact(a,'stdout.log') as f:
   for l in f:
    if not l.startswith('GLOB2_MEASURE '):continue
    team=int(re.search(r'\bteam=(\d+)',l)[1]);tick=int(re.search(r'\btick=(\d+)',l)[1])
    if team not in latest or tick>=latest[team][0]:latest[team]=(tick,l)
  alive=[t for t in n['teams'] if t['alive']];rank=sorted(alive,key=lambda t:(t['prestige'],t['units'],t['buildings']),reverse=True);leader=None
  if rank and (len(rank)==1 or tuple(rank[0][k] for k in ['prestige','units','buildings'])!=tuple(rank[1][k] for k in ['prestige','units','buildings'])):leader=(rank[0]['team']-rot)%4
  games.append(dict(variant=variant,map_seed=seed,rotation=rot,game_seed=j['seeds']['game'],leader=leader,winners=[(t-rot)%4 for t in n.get('winning_teams',[])],termination=n['termination'],host=a['host'],job=j['id'],ticks=n.get('ticks')))
  for t in n['teams']:
   team=t['team'];m=parse(latest[team][1])[1];rows.append(dict(variant=variant,map_seed=seed,rotation=rot,team=team,start=(team-rot)%4,units=t['units'],prestige=t['prestige'],buildings=t['buildings'],alive=t['alive'],starvation=sum(m[f'deaths_{i}_1'] for i in range(3)),combat=sum(m[f'deaths_{i}_0'] for i in range(3)),conversions_in=sum(m[f'conversionsIn_{i}'] for i in range(3)),conversions_out=sum(m[f'conversionsOut_{i}'] for i in range(3)),measurement_tick=latest[team][0]))
collect(root/'artifacts/forts-tournament/linux-run','baseline-r3');collect(out/'experiment',name)
with (out/'colonies.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
(out/'games.json').write_text(json.dumps(games,indent=2)+'\n');summary=[]
for variant in ['baseline-r3',name]:
 for seed in sorted({g['map_seed'] for g in games if g['variant']==variant}):
  g=[x for x in games if x['variant']==variant and x['map_seed']==seed];r=[x for x in rows if x['variant']==variant and x['map_seed']==seed]
  means=[mean(x['units'] for x in r if x['start']==i) for i in range(4)]
  e=dict(variant=variant,map_seed=seed,games=len(g),leaders=[sum(x['leader']==i and x['termination']=='tick_cap' for x in g) for i in range(4)],wins=[sum(i in x['winners'] for x in g) for i in range(4)],mean_population=means,mean_starvation=mean(x['starvation'] for x in r),eliminations=sum(not x['alive'] for x in r),low_population=sum(x['units']<10 for x in r),worst_best_population_ratio=min(means)/max(means) if max(means) else 0)
  summary.append(e);print(variant,seed,len(g),'leaders',e['leaders'],'pop',[round(x,1) for x in means],'starve',round(e['mean_starvation'],1),'elim',e['eliminations'])
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
