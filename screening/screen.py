import pathlib,subprocess,json,argparse,statistics,time
p=argparse.ArgumentParser();p.add_argument('--variant',action='append',required=True);p.add_argument('--cases',nargs='+',required=True);p.add_argument('--output',required=True);p.add_argument('--ticks',type=int,default=1000);p.add_argument('--repeats',type=int,default=2);p.add_argument('--cpus',default='8,10');a=p.parse_args();root=pathlib.Path.cwd();e=root/'artifacts/broad-pass';out=e/a.output;out.mkdir()
cases=json.loads((root/'artifacts/pr-final/evidence/cases.json').read_text());cases.append({'id':'cortex12','save':str(e/'cortex12.game.gz'),'tick':20000})
variants={'baseline':root/'artifacts/pr-final/candidate',**{v:e/v for v in a.variant}};rows=[]
for name in a.cases:
 c=next(c for c in cases if c['id']==name);reference=None
 for repeat in range(-1,a.repeats):
  order=list(variants) if repeat%2 else list(reversed(variants))
  for label in order:
   d=out/f'{name}-{repeat}-{label}';d.mkdir();cmd=['taskset','-c',a.cpus,str(variants[label]),'--run-game','--load-game',str(root/'artifacts/pr-final/evidence'/c['save']),'--ticks',str(c['tick']+a.ticks),'--gradient-workers','1','--gradient-delay','8','--output-dir',str(d)];(d/'command.json').write_text(json.dumps(cmd));start=time.monotonic()
   with (d/'stdout.log').open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
   r=json.loads((d/'result.json').read_text());core={k:r[k] for k in ['ticks','game_seed','termination','resolved','players','teams','winning_teams','winning_alliances','unresolved']}
   if reference is None:reference=core
   assert core==reference,(name,label)
   row={'case':name,'variant':label,'repeat':repeat,'run_s':r['run_ns']/1e9,'wall_s':time.monotonic()-start};rows.append(row);(out/'results.json').write_text(json.dumps(rows,indent=2));print(row,flush=True)
 for label in a.variant:
  b=statistics.median(r['run_s'] for r in rows if r['case']==name and r['variant']=='baseline' and r['repeat']>=0);v=statistics.median(r['run_s'] for r in rows if r['case']==name and r['variant']==label and r['repeat']>=0);print(name,label,'reduction',100*(1-v/b),flush=True)
