import json,re,collections,sys
AIS=['nicowar','cortex','cabino','maxima']
def agg(prefix):
  res=collections.defaultdict(list)
  for s in (1,2,3):
    for r in range(4):
      log=f'artifacts/lava/play/{prefix}-s{s}-r{r}.log'
      meas={}
      for line in open(log,errors='replace'):
        if line.startswith('GLOB2_MEASURE '):
          d=dict(re.findall(r'(\S+?)=(-?\d+)',line)); meas[int(d['team'])]=d
      rj=json.load(open(f'artifacts/lava/play/{prefix}-s{s}-r{r}/result.json'))
      teams=rj.get('teams') or rj.get('colonies')
      for k in range(4):
        ai=AIS[(k+r)%4]; m=meas.get(k,{}); t=teams[k]; h=t.get('unit_history') or t.get('units')
        units=h[-1] if isinstance(h,list) else h
        res[ai].append((units,int(m.get('harvested_1',0)),int(m.get('deaths_0_1',0))+int(m.get('deaths_2_1',0)),int(m.get('deaths_0_0',0))+int(m.get('deaths_2_0',0))))
  return res
for prefix in sys.argv[1:]:
  res=agg(prefix)
  for ai in AIS:
    L=res[ai]; n=len(L); u=[x[0] for x in L]
    print(f'{prefix:10} {ai:8} n={n} units avg {sum(u)/n:6.1f} min {min(u):4} wheat {sum(x[1] for x in L)//n:5} starved {sum(x[2] for x in L):3} killed {sum(x[3] for x in L):3}')
