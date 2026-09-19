import pathlib,json,re,gzip
root=pathlib.Path(__file__).resolve().parent
rows=[]
keys=['tick','births_0','harvested_1','harvested_0','harvested_4','delivered_4','deaths_0_1','deaths_2_1','deaths_0_0','deaths_2_0','abilityGains_0_4','abilityGains_2_4']
for p in sorted(list(root.glob('release-*.log'))+list(root.glob('release-*.log.gz'))):
 name=p.name.removesuffix('.gz').removesuffix('.log')
 result=root/name/'result.json'
 if not result.exists():continue
 last={};ais={}
 for line in (gzip.open(p,'rt') if p.suffix=='.gz' else p.open()):
  if line.startswith('GLOB2_MEASURE'):
   d={k:int(v) for k,v in re.findall(r'(\S+?)=(-?\d+)',line)};last[d['team']]={k:d.get(k) for k in keys}
  if line.startswith('GLOB2_AI_FINAL'):
   d=dict(re.findall(r'(\S+?)=(\S+)',line));ais[int(d['team'])]=d.get('ai')
 data=json.load(open(result));teams=data.get('teams',[])
 row=dict(game=name,teams=[dict(team=t['team'],ai=ais.get(t['team']),units=t.get('units'),buildings=t.get('buildings'),eliminated_tick=t.get('eliminated_tick'),**last.get(t['team'],{})) for t in teams])
 rows.append(row)
 print(row['game'],'births',[t.get('births_0') for t in row['teams']],'starved',[t.get('deaths_0_1') for t in row['teams']],'killed',[t.get('deaths_0_0') for t in row['teams']])
(root/'game-summary.json').write_text(json.dumps(rows,indent=2)+'\n')
