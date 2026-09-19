"""Render auditable summary tables; policy selection remains a human interpretation."""
import json,pathlib,sys
root=pathlib.Path(sys.argv[1]);report=json.loads((root/'summary.json').read_text());lines=['# Hospital ratio results','']
def name(v):return 'Former policy' if v=='control' else f'{int(v[4:])/100:.1f}'
def fmt(v,scale=1):return '—' if v is None else f'{v*scale:.2f}'
def ci(d,scale=1):
 return f"{fmt(d['mean'],scale)} [{', '.join(fmt(v,scale) for v in d['ci95'])}] (n={d['n']})" if d['ci95'] else '—'
for size,title in [('all','All cases'),('small','128×128'),('large','256×256 FFA')]:
 g=report['groups'][size];lines += [f'## {title}','', '| Target beds/warrior | Operating beds/warrior¹ | Wins / losses / unresolved | Exposed games | Attack samples short of beds¹ | Missing beds / 10 warriors¹ | Completed hospital resource units² |','|---|---:|---:|---:|---:|---:|---:|']
 for v,s in g['summary'].items():
  o=s['outcomes'];lines.append(f"| {name(v)} | {fmt(s['beds_per_warrior'])} | {o.get('won',0)} / {o.get('lost',0)} / {o.get('unresolved',0)} | {s['exposed_games']} | {fmt(s['shortage_fraction'],100)}% | {fmt(s['missing_per_warrior'],10)} | {fmt(s['hospital_completed_resource_units'])} |")
 lines += ['', '| Ratio vs former policy | Win difference, pp [95% CI] | Exact win p, Holm adjusted | Missing beds / 10 warriors difference [95% CI] | Resource-unit difference [95% CI] |','|---|---:|---:|---:|---:|']
 for v,c in g['paired_vs_control'].items():lines.append(f"| {name(v)} | {ci(c['win'],100)} | {c['win_discordance']['p_holm']:.4f} | {ci(c['missing_per_warrior'],10)} | {ci(c['hospital_completed_resource_units'])} |")
 lines += ['', '| Ratio | Mean peak missing beds/game³ | Warrior combat deaths/game | Buildings destroyed/game |','|---|---:|---:|---:|']
 for v,s in g['summary'].items():lines.append(f"| {name(v)} | {fmt(s['peak_missing'])} | {fmt(s['warrior_deaths'])} | {fmt(s['buildings_destroyed'])} |")
 lines += ['', '| Ratio | Worker combat deaths/game | Exploratory deaths/million worker-ticks | Exploratory worker-time needing healing without a hospital target |','|---|---:|---:|---:|']
 for v,s in g['summary'].items():lines.append(f"| {name(v)} | {fmt(s['worker_deaths'])} | {fmt(s['worker_deaths_per_million_ticks'])} | {fmt(s['worker_no_hospital_fraction'],100)}% |")
 lines += ['', 'Supplementary adjacent-ratio comparisons (higher minus lower; unadjusted):','', '| Comparison | Win difference, pp [95% CI] | Missing beds / 10 warriors difference [95% CI] | Resource-unit difference [95% CI] |','|---|---:|---:|---:|']
 for k,c in g['exploratory_adjacent_ratios'].items():
  high,low=k.split('-');lines.append(f"| {name(high)} − {name(low)} | {ci(c['win'],100)} | {ci(c['missing_per_warrior'],10)} | {ci(c['hospital_completed_resource_units'])} |")
 lines += ['']
lines += ['¹ Equal-weight per-game averages during sampled visible colony pressure, among games with warriors and pressure samples. Paired pressure intervals use cases exposed under both compared policies; their n can differ. These are capacity-deficit proxies, not queues or identical incoming attacks.','', '² Sum of resource requirements of completed hospital builds/upgrades. Excludes unfinished work, repairs, travel and opportunity cost.','', '³ Peak is the maximum sampled pressure deficit within each game, averaged across all games; games without qualifying pressure contribute zero.', '', 'Win means a real engine victory by tick 60,000. Unresolved games remain unresolved. Intervals are pointwise; only the four primary all-case win tests form the prespecified Holm-adjusted family. Stratum, secondary, worker-rate and adjacent-ratio results are descriptive.','']
(root/'RESULTS.md').write_text('\n'.join(lines))
