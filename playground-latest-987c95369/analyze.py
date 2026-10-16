from pathlib import Path
import json,csv,hashlib,math,re,html,datetime,sys,runpy,platform,subprocess
O=Path(__file__).resolve().parent;H=O.parent/'october-regression/coordination-redesign/full-design-comparison/campaign/serial/Playground'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def save(p,v):Path(p).write_text(json.dumps(v,indent=2)+'\n')
def main():
 end=read(O/'complete.json');assert end['state']=='complete' and end['runs']==32
 helper=runpy.run_path(str(O/'run.py'),run_name='audit_helpers');assert not helper['census']()
 request=read(O/'request.json');build=read(O/'build-complete.json');assert sha(build['binary'])==end['binary_sha256']==build['binary_sha256']
 assert sha(O/'request.json')==end['request_sha256'] and sha(O/'run.py')==read(O/'launch.json')['script_sha256']
 for name,digest in build['shared_library_hashes'].items():assert sha(name)==digest,('Runtime dependency changed',name)
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=build['cwd'],text=True).strip()==build['revision']
 assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=build['cwd'],text=True).strip()
 out=O/'results';out.mkdir(exist_ok=True)
 # Heavy analysis/plotting starts only after all timed processes end.
 import numpy as np
 import matplotlib;matplotlib.use('Agg');import matplotlib.pyplot as plt
 metrics={'e2e_seconds':('End-to-end game time','s',False),'total_cpu_seconds':('Total CPU per game','CPU-s',False),'e2e_tps':('End-to-end tick rate','ticks/s',True),'cpu_ms_tick':('CPU per tick','ms',False),'peak_mib':('Peak memory per game','MiB',False),'ticks':('Game length','ticks',False),'user_cpu_seconds':('User CPU per game','CPU-s',False),'system_cpu_seconds':('System CPU per game','CPU-s',False),'voluntary_context_switches':('Voluntary context switches','count',False),'involuntary_context_switches':('Involuntary context switches','count',False)}
 historical_proof=read(H.parents[2]/'playground-stop/complete.json')
 allrows=[];groups={};audit=[]
 for role in ['baseline','wake-only','latest-master']:
  rows=[]
  for i,seed in enumerate(request['seeds']):
   p=O/'runs'/str(seed)/'receipt.json' if role=='latest-master' else H/(str(seed)+'-'+role)/'receipt.json';r=read(p)
   if role!='latest-master':assert sha(p)==historical_proof['receipt_hashes'][str(p.relative_to(H.parent))]
   assert r['seed']==seed and r['seed_index']==i and r.get('allocation')==list(range(32))
   log=p.with_name('stdout.log');assert sha(log)==r['stdout_sha256']
   if role=='latest-master':
    assert sha(p)==end['receipts'][str(p.relative_to(O))]
    raw=r['raw_wait4_usage'];assert r['user_cpu_seconds']==raw['ru_utime'] and r['system_cpu_seconds']==raw['ru_stime'] and r['peak_rss_kib']==raw['ru_maxrss'];assert math.isclose(r['e2e_seconds'],r['finished_monotonic']-r['started_monotonic'],abs_tol=1e-9)
    assert r['revision']==build['revision'] and r['binary_sha256']==build['binary_sha256'] and r['map_sha256']==request['map_sha256'] and not r['checksum_files'] and r['telemetry']=='off'
    assert r['compute_requested']=='auto' and r['compute_participants_preflight']==32
    e=r['env'];assert e['GLOB2_TEST_SEED']==str(seed) and e['GLOB2_TEST_MAX_TICKS']=='2147483647'
    expected=['taskset','-c',','.join(map(str,range(32))),build['binary'],'dev','random-games','--runs','1','--ticks','2147483647','--map','Playground','--matchup',','.join(['nicowar']*8),'--compute-threads','auto','--gradient-backend','cpu'];assert r['command']==expected
   # Reparse the native summary independently, including old adapter runs.
   text=log.read_text();lines=re.findall(r'^GLOB2_GAME_END ticks=(\d+) winner_team=(-?\d+) seed=(\d+) map="([^"]+)" orders=(\d+) players=(.+)$',text,re.M)
   if r['status']=='winner':
    assert len(lines)==1;t,w,s,mapname,orders,players=lines[0];assert (int(t),int(w),int(s),mapname)==(r['ticks'],r['winner'],seed,'Playground');teams=[int(x[4:].split(':')[0])for x in players.split(',')if x.endswith(':Nicowar')];assert sorted(teams)==list(range(8)) and len(players.split(','))==9
    assert r['returncode']==0 and r['e2e_seconds']<3600
   else:assert r['status']=='cap' and r['e2e_seconds']>=3600
   assert math.isclose(r['total_cpu_seconds'],r['user_cpu_seconds']+r['system_cpu_seconds'],abs_tol=1e-9)
   row={'role':role,'seed':seed,'seed_index':i,'status':r['status'],'winner':r['winner'],'ticks':r['ticks'],'orders':int(orders) if r['status']=='winner' else None,'e2e_seconds':r['e2e_seconds'],'total_cpu_seconds':r['total_cpu_seconds'],'user_cpu_seconds':r['user_cpu_seconds'],'system_cpu_seconds':r['system_cpu_seconds'],'peak_mib':r['peak_rss_kib']/1024,'voluntary_context_switches':r['voluntary_context_switches'],'involuntary_context_switches':r['involuntary_context_switches'],'receipt':str(p),'receipt_sha256':sha(p),'stdout_sha256':sha(log)}
   row.update(e2e_tps=r['ticks']/r['e2e_seconds'] if r['ticks'] else None,cpu_ms_tick=1000*r['total_cpu_seconds']/r['ticks'] if r['ticks'] else None)
   if r['ticks']:assert math.isclose(row['e2e_tps'],r['e2e_tps'],rel_tol=1e-12) and math.isclose(row['cpu_ms_tick'],1000*r['cpu_per_tick'],rel_tol=1e-12)
   rows.append(row);allrows.append(row);audit.append({'role':role,'seed':seed,'receipt':str(p),'receipt_sha256':sha(p),'stdout_sha256':sha(log)})
  assert len(rows)==32 and len({r['seed'] for r in rows})==32;groups[role]=rows
 rng=np.random.default_rng(20261015);indices=rng.integers(0,32,size=(10000,32));summary={};comparisons=[]
 for role,rows in groups.items():
  eligible=[r for r in rows if r['status']=='winner'];n=len(eligible);idx=indices if n==32 else rng.integers(0,n,size=(10000,n));stats={}
  for key in metrics:
   a=np.asarray([r[key] for r in eligible],dtype=float);boots=a[idx].mean(axis=1);stats[key]={'mean':float(a.mean()),'median':float(np.median(a)),'p95':float(np.quantile(a,.95)),'min':float(a.min()),'max':float(a.max()),'mean_ci95':np.quantile(boots,[.025,.975]).tolist()}
  summary[role]={'winners':n,'caps':32-n,'metrics':stats,'aggregate_e2e_tps':sum(r['ticks']for r in eligible)/sum(r['e2e_seconds']for r in eligible),'pooled_cpu_ms_per_tick':1000*sum(r['total_cpu_seconds']for r in eligible)/sum(r['ticks']for r in eligible),'sum_winner_cpu_seconds':sum(r['total_cpu_seconds']for r in eligible),'sum_winner_e2e_seconds':sum(r['e2e_seconds']for r in eligible),'mean_orders':sum(r['orders']for r in eligible)/n,'observed_max_peak_mib':max(r['peak_mib']for r in rows)}
 for ref in ['baseline','wake-only']:
  pairs=[(a,b)for a,b in zip(groups[ref],groups['latest-master'])if a['status']==b['status']=='winner'];n=len(pairs);idx=indices if n==32 else rng.integers(0,n,size=(10000,n))
  for key,(label,unit,larger) in metrics.items():
   a=np.asarray([x[key]for x,y in pairs]);b=np.asarray([y[key]for x,y in pairs]);boots=100*(b[idx].mean(axis=1)/a[idx].mean(axis=1)-1)
   comparisons.append({'reference':ref,'candidate':'latest-master','metric':key,'n':n,'reference_mean':float(a.mean()),'latest_mean':float(b.mean()),'change_pct':float(100*(b.mean()/a.mean()-1)),'change_ci95_low':float(np.quantile(boots,.025)),'change_ci95_high':float(np.quantile(boots,.975)),'adverse_seeds':int(np.count_nonzero(b<a if larger else b>a)),'outcomes_matching':sum((x['ticks'],x['winner'])==(y['ticks'],y['winner'])for x,y in pairs)})
 decomposition={}
 for ref in ['baseline','wake-only']:
  a=summary[ref];b=summary['latest-master'];old_ticks=sum(r['ticks']for r in groups[ref]if r['status']=='winner');new_ticks=sum(r['ticks']for r in groups['latest-master']if r['status']=='winner');decomposition[ref]={'total_ticks_change_pct':100*(new_ticks/old_ticks-1),'aggregate_tps_change_pct':100*(b['aggregate_e2e_tps']/a['aggregate_e2e_tps']-1),'total_e2e_change_pct':100*(b['sum_winner_e2e_seconds']/a['sum_winner_e2e_seconds']-1),'pooled_cpu_per_tick_change_pct':100*(b['pooled_cpu_ms_per_tick']/a['pooled_cpu_ms_per_tick']-1),'total_cpu_change_pct':100*(b['sum_winner_cpu_seconds']/a['sum_winner_cpu_seconds']-1)}
 save(out/'summary.json',{'revision':build['revision'],'groups':summary,'comparisons':comparisons,'decomposition':decomposition,'methods':'32 fixed paired seeds. 10000 paired bootstrap draws; ratio of arithmetic means; numpy linear 2.5/97.5 quantiles. Historical controls were not rerun/interleaved. Intervals describe seed variability, not temporal/environment confounding. Winners only for completion metrics; caps separate. E2E TPS for every role; no comparison of old inner TPS with new E2E TPS.','limits':['CPU backend, one host, unrestricted auto32, one game at a time; no concurrent-throughput claim.','Historical references use archived research adapters on054804590; latest is unmodifiedmaster 987c95369.','Hiring timing changed on master: seed pairing does not establish identical gameplay or equal useful work.','No timed checksum telemetry; outcome agreement would not establish checksum equivalence.','Golden and64-tickauto smoke checks are separate from whole-game timing.']})
 for name,rows in [('every-game.csv',allrows),('comparisons.csv',comparisons)]:
  with(out/name).open('w',newline='')as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 save(out/'raw-audit.json',{'state':'verified','latest_runs':32,'historical_runs':64,'build_sha256':sha(O/'build-complete.json'),'request_sha256':sha(O/'request.json'),'complete_sha256':sha(O/'complete.json'),'records':audit,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
 labels=['Earlier baseline\n054804590','Bounded wakeups\n054804590','Latest master\n987c95369'];colors=['#88939f','#447eac','#14896a'];order=list(groups)
 plt.rcParams.update({'font.size':11,'axes.spines.top':False,'axes.spines.right':False})
 fig,axes=plt.subplots(2,3,figsize=(15,9))
 for ax,key in zip(axes.flat,['e2e_seconds','total_cpu_seconds','e2e_tps','cpu_ms_tick','peak_mib','ticks']):
  means=np.array([summary[r]['metrics'][key]['mean'] for r in order]);ci=np.array([summary[r]['metrics'][key]['mean_ci95'] for r in order]);ax.bar(range(3),means,color=colors,yerr=np.vstack((means-ci[:,0],ci[:,1]-means)),capsize=4,width=.65)
  ax.set_xticks(range(3),labels,fontsize=9);ax.set_ylabel(metrics[key][1]);ax.set_title(metrics[key][0]);ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
  for x,y in enumerate(means):ax.text(x,ci[x,1]*1.03,f'{y:,.2f}'if y<100 else f'{y:,.0f}',ha='center',fontsize=10)
  ax.set_ylim(0,max(ci[:,1])*1.25)
 fig.suptitle('Playground 128×128 · eight Nicowars · 32 fixed seeds per version\nOne unrestricted auto32 CPU game at a time; historical references, not interleaved controls',fontsize=14);fig.tight_layout(rect=(0,0,.999,.93))
 for ext in ['png','svg']:fig.savefig(out/('performance.'+ext),dpi=160)
 plt.close(fig)
 body=['<!doctype html><meta charset="utf-8"><title>Latest master Playground performance</title><style>body{font:16px system-ui;margin:40px auto;max-width:1150px;line-height:1.5;color:#172635}table{border-collapse:collapse;width:100%;margin:22px 0}th,td{padding:9px 12px;text-align:right;border-bottom:1px solid #ddd}th:first-child,td:first-child{text-align:left}img{width:100%}code{background:#eef2f4}h1{line-height:1.2}.note{background:#f2f6fa;padding:16px}</style><h1>Latest master: 32 Playground games</h1><p>Release revision <code>'+build['revision']+'</code>, one unrestricted auto32 CPU game at a time, eight Nicowars on the fixed128×128 map, same32 game seeds. '+str(summary['latest-master']['winners'])+' winners and '+str(summary['latest-master']['caps'])+' caps.</p><img src="performance.svg"><table><tr><th>Metric (mean per game)</th>']
 body.extend('<th>'+html.escape(x.replace('\n',' '))+'</th>'for x in labels);body.append('</tr>')
 for key in ['e2e_seconds','total_cpu_seconds','e2e_tps','cpu_ms_tick','peak_mib','ticks']:
  body.append('<tr><td>'+metrics[key][0]+' ('+metrics[key][1]+')</td>'+''.join('<td>'+f"{summary[r]['metrics'][key]['mean']:,.3f}"+'</td>'for r in order)+'</tr>')
 body.append('</table><h2>Changes against the earlier bounded-wakeup version</h2><table><tr><th>Metric</th><th>Change</th><th>Paired95% interval</th><th>Adverse seeds</th></tr>')
 for r in comparisons:
  if r['reference']=='wake-only':body.append('<tr><td>'+metrics[r['metric']][0]+'</td><td>'+f"{r['change_pct']:+.2f}%"+'</td><td>'+f"[{r['change_ci95_low']:+.2f}%, {r['change_ci95_high']:+.2f}%]"+'</td><td>'+str(r['adverse_seeds'])+'/32</td></tr>')
 matches=next(r['outcomes_matching']for r in comparisons if r['reference']=='wake-only');body.append('</table><p>Winner and ending tick agree with the earlier bounded-wakeup run on <b>'+str(matches)+'/32 seeds</b>. Master changed hiring timing; differences in completion time include changed game trajectories as well as execution costs.</p><div class="note"><b>Interpretation limits.</b> Historical controls were not rerun or interleaved. Paired intervals describe seed variability; they do not control for time-of-day, machine state or engine-version changes. All tick rates above use full process end-to-end wall time; the earlier inner-loop rate is deliberately not substituted. These are CPU/single-game measurements on one host. Timing has no optional checksum/profiling telemetry; outcome checks are not full-game checksum proof.</div><p>Mean per-game peak RSS and the observed maximum are distinct: latest maximum '+f"{summary['latest-master']['observed_max_peak_mib']:.2f}"+'MiB. Latest aggregate tick rate: '+f"{summary['latest-master']['aggregate_e2e_tps']:.2f}"+' ticks/s; pooled CPU/tick: '+f"{summary['latest-master']['pooled_cpu_ms_per_tick']:.3f}"+'ms. Total CPU across latest completed games: '+f"{summary['latest-master']['sum_winner_cpu_seconds']:.2f}"+'CPU-s.</p><p><a href="every-game.csv">Every game</a> · <a href="comparisons.csv">Comparisons and intervals</a> · <a href="summary.json">Summary/methods</a> · <a href="raw-audit.json">Raw audit</a></p>')
 d=decomposition['wake-only'];body.append('<h2>Game length versus execution rate</h2><p>Across the completed games, total simulated ticks changed '+f"{d['total_ticks_change_pct']:+.2f}%"+' and aggregate end-to-end tick rate changed '+f"{d['aggregate_tps_change_pct']:+.2f}%"+'. Full completion time changed '+f"{d['total_e2e_change_pct']:+.2f}%"+'. This is an accounting decomposition (total time = total ticks / aggregate tick rate), not proof that a tick does the same work on both engine versions. Mean recorded orders per game: '+f"{summary['wake-only']['mean_orders']:,.0f}"+' before versus '+f"{summary['latest-master']['mean_orders']:,.0f}"+' on latest.</p>')
 (out/'index.html').write_text(''.join(body))
 save(out/'receipt.json',{'state':'report_ready','source_sha256':sha(__file__),'packages':{'numpy':np.__version__,'matplotlib':matplotlib.__version__},'files':{p.name:sha(p)for p in out.iterdir()if p.is_file()and p.name!='receipt.json'},'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
 print(json.dumps({'latest':summary['latest-master'],'comparisons':comparisons},indent=2))
if __name__=='__main__':main()
