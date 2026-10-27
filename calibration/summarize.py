import json,re,glob,os,statistics as st
rows=[]
for d in sorted(glob.glob('games/*/result.json')):
    name=d.split('/')[1]; m,s,ai=name.rsplit('-',2)[0].rsplit('-',1)[0] if False else None,None,None
    parts=name.split('-'); gen,seed,ai=parts[0],parts[1],parts[2]
    r=json.load(open(d)); meas={}
    for line in open(f'games/{name}.log',errors='replace'):
        if line.startswith('GLOB2_MEASURE'):
            v=dict(re.findall(r'(\S+?)=(-?\d+)',line)); meas[int(v['team'])]=v
    for t in r['teams']:
        h=[x[0] for x in t.get('history',[])]
        v=meas.get(t['team'],{})
        rows.append(dict(gen=gen,seed=seed,ai=ai,team=t['team'],final=h[-1] if h else 0,peak=max(h) if h else 0,
            bld=t.get('buildings'),elim=t.get('eliminated_tick'),births=int(v.get('births_0',0)),
            wheat=int(v.get('harvested_1',0)),wood=int(v.get('harvested_0',0)),
            starve=int(v.get('deaths_0_1',0))+int(v.get('deaths_2_1',0)),killed=int(v.get('deaths_0_0',0))+int(v.get('deaths_2_0',0))))
json.dump(rows,open('rows.json','w'))
for ai in ['nicowar','cortex','cabino','maxima']:
    print(f'== {ai}')
    for gen in ['r5','r6','qf','ff','door','forts']:
        for seed in ['101','202','303']:
            R=[r for r in rows if r['ai']==ai and r['gen']==gen and r['seed']==seed]
            f=lambda k: '/'.join(str(r[k]) for r in R)
            print(f'  {gen:5} {seed}: final {f("final"):16} peak {f("peak"):16} bld {f("bld"):12} wheat {f("wheat"):20} wood {f("wood"):18} births {f("births"):16} starve {f("starve"):10} killed {f("killed")}')
