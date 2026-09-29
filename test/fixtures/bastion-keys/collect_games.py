from pathlib import Path
import gzip,json
base=Path('/tmp/bastion-swim-games');out=base/'evidence';out.mkdir(exist_ok=True);summary=[]
for logfile in sorted(base.glob('*.log.gz')):
 name=logfile.name[:-7];result=json.loads((base/name/'result.json').read_text()); rows=[]
 with gzip.open(logfile,'rt') as f:
  for line in f:
   if line.startswith('GLOB2_MEASURE '):rows.append(dict((k,int(v)) for k,v in (x.split('=') for x in line.split()[1:])))
 finals=[r for r in rows if r.get('final')==1];assert len(finals)==4 and all(r['tick']==result['ticks'] for r in finals)
 with gzip.open(out/(name+'-measurements.json.gz'),'wt') as f:json.dump(rows,f)
 (out/(name+'-result.json')).write_text(json.dumps(result))
 seed,rot,ai=name.split('-');rot=int(rot[1:])
 for team,c in zip(result['teams'],sorted(finals,key=lambda r:r['team'])):
  assert team['team']==c['team'];timeline=[r for r in rows if r['team']==c['team']]
  fruit=lambda r:sum(r.get('harvested_'+str(i),0) for i in (5,6,7))
  summary.append(dict(game=name,team=c['team'],start=(c['team']-rot)%4,ticks=result['ticks'],units=team['units'],buildings=team['buildings'],alive=team['alive'],births=sum(c['births_'+str(i)] for i in range(3)),worker_starvation=c['deaths_0_1'],combat_deaths=sum(c['deaths_'+str(i)+'_0'] for i in range(3)),swimming_training=sum(c.get('abilityGains_'+str(i)+'_4',0) for i in range(3)),fruit=fruit(c),enemy_building_damage=c.get('damageDealt_0_1',0),first_fruit_tick=next((r['tick'] for r in timeline if fruit(r)),None),first_building_damage_tick=next((r['tick'] for r in timeline if r.get('damageDealt_0_1',0)),None),wood=c['harvested_0'],wheat=c['harvested_1'],conversions_in=sum(c['conversionsIn_'+str(i)] for i in range(3)),conversions_out=sum(c['conversionsOut_'+str(i)] for i in range(3))))
(out/'games-summary.json').write_text(json.dumps(summary,indent=2));print(len(summary)//4,'complete games',len(summary),'colony records')
