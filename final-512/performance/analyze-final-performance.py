from pathlib import Path
import json,time,statistics,math,hashlib
c=Path(__file__).parent;out=c/'final-performance'
while json.loads((out/'status.json').read_text())['phase']!='complete':time.sleep(10)
rows=json.loads((out/'manifest.json').read_text());assert len(rows)==32
plan=json.loads((out/'plan.json').read_text())
assert len({(r['format'],r['policy'],r['repetition']) for r in rows})==32
for policy,meta in plan['binaries'].items():
 assert hashlib.sha256(Path(meta['path']).read_bytes()).hexdigest()==meta['sha256'],policy
for i,r in enumerate(rows):
 expected=plan['games'][i];result_path=Path(r['result']);result=json.loads(result_path.read_text())
 assert (r['index'],r['format'],r['policy'],r['repetition'],r['map_sha256'])==(i,expected['format'],expected['policy'],expected['repetition'],expected['map_sha256'])
 assert hashlib.sha256(Path(expected['map']).read_bytes()).hexdigest()==r['map_sha256']
 assert result['ticks']==r['ticks']==result['benchmark_measured_ticks']
 assert result['compute_threads']==1 and result['gpu_requested']==0
 assert result['compute_telemetry']['mode']=='off'
 assert not list(result_path.parent.glob('gradient-*.csv'))

formats=['1v1','2v2','3ffa','4ffa','4v4','8ffa','8v8','16ffa'];summaries=[]
for fmt in formats:
 p={k:sorted([r for r in rows if r['format']==fmt and r['policy']==k],key=lambda r:r['repetition']) for k in ['master','adaptive32']}
 for policy,rr in p.items():
  assert len(rr)==2 and {r['repetition'] for r in rr}=={0,1}
  results=[json.loads(Path(r['result']).read_text()) for r in rr]
  assert len({(r['ticks'],r['finalChecksum'],tuple(r['winning_teams']),r['termination']) for r in results})==1,(fmt,policy,'repeat execution differs')
 item={'format':fmt,'map':next(r['spec']['generator'] for r in json.loads((c/'final-performance-maps/manifest.json').read_text()) if r['format']==fmt)}
 for policy,rr in p.items():
  r=json.loads(Path(rr[0]['result']).read_text());item[policy]={**{key:statistics.mean(a[key] for a in rr) for key in ['cpu_ms_per_tick','total_cpu_ms_per_tick','total_cpu_seconds','simulation_cpu_seconds','wall_seconds','ticks_per_wall_second']},'ticks':rr[0]['ticks'],'termination':r['termination'],'winning_teams':r['winning_teams'],'final_units':sum(t['units'] for t in r['teams']),'gradient_jobs':r['gradient_jobs'],'building_gradient_jobs':r['building_gradient_jobs']}
 for key in ['cpu_ms_per_tick','total_cpu_seconds','wall_seconds','ticks','ticks_per_wall_second','gradient_jobs']:
  a,m=item['adaptive32'][key],item['master'][key];item[key+'_change_pct']=100*(a/m-1) if m else None
 item['cpu_ratio_repeats']=[p['adaptive32'][i]['cpu_ms_per_tick']/p['master'][i]['cpu_ms_per_tick'] for i in range(2)]
 item['total_cpu_ratio_repeats']=[p['adaptive32'][i]['total_cpu_seconds']/p['master'][i]['total_cpu_seconds'] for i in range(2)]
 summaries.append(item)
pooled={}
for policy in ['master','adaptive32']:
 r=[x for x in rows if x['policy']==policy];cpu=sum(x['simulation_cpu_seconds'] for x in r);ticks=sum(x['ticks'] for x in r);wall=sum(x['wall_seconds'] for x in r)
 pooled[policy]=dict(cpu_ms_per_tick=1000*cpu/ticks,total_cpu_seconds=sum(x['total_cpu_seconds'] for x in r),simulation_cpu_seconds=cpu,wall_seconds=wall,ticks=ticks,ticks_per_wall_second=ticks/wall)
ratios={k:100*(pooled['adaptive32'][k]/pooled['master'][k]-1) for k in pooled['master']}
report=dict(production='ca6365e83900e2668ff2348044a89ba3b2d70c43',test_only_followup='df25376f8b681dcd8d87dd6832b4e93a935cad96',base='3d896cbb95293da6d24caddd247a62c9ca99d77f',runs=32,independent_starting_states=8,repetitions=2,formats=summaries,pooled=pooled,pooled_change_pct=ratios,equal_format_geometric_mean_cpu_per_tick_change_pct=100*(math.prod(1+s['cpu_ms_per_tick_change_pct']/100 for s in summaries)**(1/8)-1),limitations=['Eight starting states; two timing repetitions per policy. This is a final smoke comparison, not robust across-map inference.','Whole-policy trajectories/populations/game lengths differ. CPU/tick is not an isolated refresh-kernel cost.','Serial games with explicit one-thread compute and GPU/probe off; default multi-core gameplay wall time was not benchmarked.','Tick-capped games have no measured finish time.','Range bars span the two paired timing ratios; bar centers and table entries use the ratio of policy means. They are not confidence intervals across maps.','Tick-capped totals are censored observed-prefix CPU/wall costs, not full-game finish costs; end status is shown for each policy.','One-host timing without a recorded continuous host-load/thermal guard; counterbalancing reduces drift but cannot rule out external confounding.'])
(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
lines=['# Final master versus adaptive32 release comparison','','Eight heldout starting states, two repetitions per policy, one game at a time, explicit one-thread compute, CPU backend, telemetry disabled.90000-tick cap or earlier engine end. Same master173-generated map file per paired state.','','|Format|CPU ms/tick master|CPU ms/tick A32|Change|Observed total CPU change|Observed wall change|Ticks master / A32|End master / A32|','|---|---:|---:|---:|---:|---:|---:|---|']
for s in summaries:
 m,a=s['master'],s['adaptive32'];lines.append(f"|{s['format']}|{m['cpu_ms_per_tick']:.4f}|{a['cpu_ms_per_tick']:.4f}|{s['cpu_ms_per_tick_change_pct']:+.1f}%|{s['total_cpu_seconds_change_pct']:+.1f}%|{s['wall_seconds_change_pct']:+.1f}%|{m['ticks']} / {a['ticks']}|{m['termination']} / {a['termination']}|")
lines+=['',f"Pooled measured CPU/tick change: {ratios['cpu_ms_per_tick']:+.2f}%. Total process CPU: {ratios['total_cpu_seconds']:+.2f}%. Wall: {ratios['wall_seconds']:+.2f}%. Total ticks: {ratios['ticks']:+.2f}%. Equal-format geometric mean CPU/tick change: {report['equal_format_geometric_mean_cpu_per_tick_change_pct']:+.2f}%.",'','![Per-format CPU comparison](comparison.png)','',*['- '+s for s in report['limitations']],'','Commands, maps/seeds, per-run CPU/wall/results and immutable binary hashes are retained with this report. Every repeated policy/state produced identical ticks, final checksum, winning teams and termination.']
(out/'README.md').write_text('\n'.join(lines)+'\n')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axes=plt.subplots(1,2,figsize=(12,5),layout='constrained')
for ax,key,title in zip(axes,['cpu_ratio_repeats','total_cpu_ratio_repeats'],['Simulation CPU per tick','Observed total process CPU (tick caps censored)']):
 means=[s['cpu_ms_per_tick_change_pct'] if key=='cpu_ratio_repeats' else s['total_cpu_seconds_change_pct'] for s in summaries];lower=[means[i]-100*(min(s[key])-1) for i,s in enumerate(summaries)];upper=[100*(max(s[key])-1)-means[i] for i,s in enumerate(summaries)]
 ax.barh(formats,means,xerr=[lower,upper],color=['#cb5a35' if x>0 else '#26856b' for x in means],capsize=3)
 ax.axvline(0,color='#555',linewidth=1);ax.set_xlabel('Adaptive32 change versus master (%)');ax.set_title(title);ax.invert_yaxis();ax.grid(axis='x',alpha=.2);ax.set_axisbelow(True)
fig.suptitle('Eight heldout games, two timing repeats per policy; one compute thread',fontsize=12)
fig.savefig(out/'comparison.png',dpi=150);fig.savefig(out/'comparison.svg');plt.close(fig)
(out/'analysis-complete.json').write_text(json.dumps({'status':'passed','repeat_checksums':'identical','runs':32}))
print(json.dumps(report,indent=2),flush=True)
