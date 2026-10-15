"""Conditional paired winner analysis. Pure artifact reads; never executes games."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,random,statistics
O=Path(__file__).resolve().parent
ap=argparse.ArgumentParser();ap.add_argument('regime',choices=['serial','waves']);a=ap.parse_args();folder=O/a.regime
complete=json.loads((folder/'complete.json').read_text());assert complete['state']=='complete' and complete['runs']==128
plan=json.loads((O/'plan.json').read_text());sha=lambda q:hashlib.sha256(q.read_bytes()).hexdigest()
assert complete['plan_sha256']==sha(O/'plan.json') and complete['launcher_sha256']==sha(O/'launcher.py')
paths=list(folder.glob('*/*/receipt.json'));assert len(paths)==128 and set(complete['receipt_hashes'])=={str(q.relative_to(folder)) for q in paths}
for q in paths:assert sha(q)==complete['receipt_hashes'][str(q.relative_to(folder))]
spec=importlib.util.spec_from_file_location('retained_winner_parser',O/'launcher.py');parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
rows=[json.loads(q.read_text()) for q in paths];assert len(rows)==128
approval=json.loads((O/'root-approved.json').read_text());resolved=json.loads(Path(plan['latest_resolve']).read_text());assert sha(Path(plan['latest_resolve']))==approval['latest_resolve_sha256']
variants={v['role']:v for v in resolved['variants']}
expected={(mapname,seed,role) for mapname in ['Playground','Oazis'] for seed in plan['seeds'] for role in ['baseline','wake-only']}
assert len({(r['map'],r['seed'],r['role']) for r in rows})==128 and {(r['map'],r['seed'],r['role']) for r in rows}==expected
for r in rows:
 path=folder/r['map']/(str(r['seed'])+'-'+r['role'])/'stdout.log';assert sha(path)==r['stdout_sha256']
 v=variants[r['role']];assert r['binary_sha256']==v['sha256']==approval['binary_hashes'][r['role']] and r['cwd']==v['cwd'] and r['cap_seconds']==plan['cap_seconds']
 profile=str(path.parent/'profile');assert r['env_overrides']==dict(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=profile,GLOB2_USER_DATA_DIR=profile,GLOB2_TEST_SEED=str(r['seed']),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd'])
 assert hashlib.sha256(json.dumps(r['command'],separators=(',',':')).encode()).hexdigest()==r['command_sha256']
 declared=next(x for x in plan['declared_commands'][a.regime] if (x['map'],x['seed'],x['role'])==(r['map'],r['seed'],r['role']));assert r['command']==declared['command'] and r['allocation']==declared['allocation'] and r['compute_requested']==declared['compute']
 for x in r['checksum_files']:assert sha(path.parent/x['path'])==x['sha256']
 reparsed=parser.parse_log(path.read_text(errors='replace'),r['map'],32 if a.regime=='serial' else 6,32 if a.regime=='serial' else 6,r['returncode'],r['status']=='cap')
 for key in ['status','ticks','winner','reported','inner_seconds','observed_checksum']:assert reparsed[key]==r[key],('Rawlog/receipt disagreement',r['map'],r['seed'],r['role'],key)
 assert r['fixture_sha256']==plan['fixtures'] and r['base_revision']==plan['base_revision']
 assert r['status'] in ['winner','cap','no_winner']
# Validate complete pair/wave inventory and actual source-declared order before statistics.
lookup_all={(r['map'],r['seed'],r['role']):r for r in rows}
assert len(complete['pairs'])==64 and len({(x['map'],x['seed']) for x in complete['pairs']})==64
for pair in complete['pairs']:
 b=lookup_all[pair['map'],pair['seed'],'baseline'];w=lookup_all[pair['map'],pair['seed'],'wake-only'];both=b['status']==w['status']=='winner';same=both and (b['ticks'],b['winner'])==(w['ticks'],w['winner'])
 if both and b['checksum_files'] and w['checksum_files']:
  be=[(x.get('rows'),x.get('first'),x.get('last')) for x in b['checksum_files'] if x['complete']];we=[(x.get('rows'),x.get('first'),x.get('last')) for x in w['checksum_files'] if x['complete']];same=same and bool(be) and be==we
 if both and b['observed_checksum'] is not None and w['observed_checksum'] is not None:same=same and b['observed_checksum']==w['observed_checksum']
 assert pair['both_winners']==both and pair['endpoints_match']==(same if both else None) and pair['eligible_winner_timing_pair']==same
 assert not both or same,('Both-winner endpoint mismatch requires root hold',pair)
wavefiles=list(folder.glob('wave-*.json'));assert set(complete['wave_hashes'])=={str(q.relative_to(folder)) for q in wavefiles}
for q in wavefiles:assert sha(q)==complete['wave_hashes'][str(q.relative_to(folder))]
width=1 if a.regime=='serial' else 4;expected_waves=[]
for mapname in ['Playground','Oazis']:
 for wave in range(32//width):
  for role in (['baseline','wake-only'] if wave%2==0 else ['wake-only','baseline']):expected_waves.append((mapname,wave,role))
assert [(x['map'],x['wave'],x['role']) for x in complete['waves']]==expected_waves
for wave in complete['waves']:
 q=folder/('wave-'+str(wave['wave'])+'-'+wave['map']+'-'+wave['role']+'.json');assert json.loads(q.read_text())==wave
 assert wave['makespan_seconds']>0 and abs(wave['finished_monotonic']-wave['started_monotonic']-wave['makespan_seconds'])<1e-6
 members=[lookup_all[wave['map'],plan['seeds'][i],wave['role']] for i in wave['seed_indices']];assert wave['seed_indices']==list(range(wave['wave']*width,(wave['wave']+1)*width)) and len(members)==width
 assert wave['winners']==sum(r['status']=='winner' for r in members) and wave['caps']==sum(r['status']=='cap' for r in members)
 assert all(wave['started_monotonic']<=r['started_monotonic']<=r['finished_monotonic']<=wave['finished_monotonic'] for r in members)
for before,after in zip(complete['waves'],complete['waves'][1:]):assert before['finished_monotonic']<=after['started_monotonic'],'Homogeneous role waves overlapped'
rng=random.Random(605441);report={'scope':'32 declared seeds, one game per role/seed; paired bootstrap10000 fixedseed605441, conditional on both matching successful winners. Caps separately censored; no exact Cartesian resampling and no warmups.','regime':a.regime,'campaign_makespan_seconds':complete['campaign_makespan_seconds'],'campaign_completed_games_per_hour':complete['campaign_completed_games_per_hour'],'maps':[],'waves':complete['waves']}
def interval(values):
 values.sort();return [values[int(.025*(len(values)-1))],values[int(.975*(len(values)-1))]]
for mapname in ['Playground','Oazis']:
 pairs=[p for p in complete['pairs'] if p['map']==mapname];eligible=[p['seed'] for p in pairs if p['eligible_winner_timing_pair']];excluded=[p for p in pairs if not p['eligible_winner_timing_pair']]
 lookup={(r['seed'],r['role']):r for r in rows if r['map']==mapname};metrics={}
 for field in ['e2e_seconds','inner_seconds','user_cpu_seconds','system_cpu_seconds','total_cpu_seconds','peak_rss_kib','voluntary_context_switches','involuntary_context_switches','cpu_per_tick','inner_tps','e2e_tps']:
  selected=[seed for seed in eligible if all(lookup[(seed,role)].get(field) is not None for role in ['baseline','wake-only'])]
  if not selected:metrics[field]={'eligible':0};continue
  b=[lookup[(seed,'baseline')][field] for seed in selected];w=[lookup[(seed,'wake-only')][field] for seed in selected];ratios=[]
  for _ in range(10000):
   indices=rng.choices(range(len(selected)),k=len(selected));before=sum(b[i] for i in indices);after=sum(w[i] for i in indices);
   if before:ratios.append(after/before-1)
  before=statistics.mean(b);after=statistics.mean(w);clusters={}
  for i,seed in enumerate(selected):clusters.setdefault(plan['seeds'].index(seed)//4,[]).append(i)
  clustered=[]
  if a.regime=='waves':
   keys=list(clusters)
   for _ in range(10000):
    indices=[i for key in rng.choices(keys,k=len(keys)) for i in clusters[key]];before_cluster=sum(b[i] for i in indices);
    if before_cluster:clustered.append(sum(w[i] for i in indices)/before_cluster-1)
  metrics[field]={'eligible':len(selected),'undefined_zero_reference_seed_draws':10000-len(ratios),'wave_cluster_count':len(clusters) if a.regime=='waves' else None,'wave_clustered_bootstrap95':interval(clustered) if a.regime=='waves' and len(clusters)>1 and clustered else None,'baseline_mean':before,'wake_mean':after,'ratio_of_means_change':after/before-1 if before else None,'paired_bootstrap95':interval(ratios) if len(selected)>1 and ratios else None,'conditional_on':'Both matching winners, metric present. Seed bootstrap treats seeds independently; waves also supplies paired four-seed wave-cluster interval to account shared host windows.'}
 summaries={}
 for role in ['baseline','wake-only']:
  rr=[r for r in rows if r['map']==mapname and r['role']==role];wins=[r for r in rr if r['status']=='winner'];ww=[w for w in complete['waves'] if w['map']==mapname and w['role']==role]
  summaries[role]={'runs':len(rr),'winners':len(wins),'caps':sum(r['status']=='cap' for r in rr),'no_winner':sum(r['status']=='no_winner' for r in rr),'all_process_cpu_seconds':sum(r['total_cpu_seconds'] for r in rr),'all_process_system_cpu_seconds':sum(r['system_cpu_seconds'] for r in rr),'max_process_peak_rss_kib':max(r['peak_rss_kib'] for r in rr),'all_voluntary_context_switches':sum(r['voluntary_context_switches'] for r in rr),'all_involuntary_context_switches':sum(r['involuntary_context_switches'] for r in rr),'winner_mean_inner_tps':statistics.mean(r['inner_tps'] for r in wins if r['inner_tps'] is not None) if any(r['inner_tps'] is not None for r in wins) else None,'winner_ticks_over_sum_game_e2e_seconds':sum(r['ticks'] for r in wins)/sum(r['e2e_seconds'] for r in wins) if wins else None,'actual_role_wave_makespan_sum_seconds':sum(w['makespan_seconds'] for w in ww),'completed_games_per_role_active_wave_hour':sum(w['winners'] for w in ww)*3600/sum(w['makespan_seconds'] for w in ww),'note':'Role wave active windows exclude interleaved other role; not fictitious contiguous per-role campaign span. Wave tick throughput with missing cap tick counts is incomplete; report raw waves.'}
 wave_throughput=None
 if a.regime=='waves':
  wave_pairs=[]
  for number in range(8):
   pair={role:next(x for x in complete['waves'] if x['map']==mapname and x['wave']==number and x['role']==role) for role in ['baseline','wake-only']}
   wave_pairs.append(pair)
  random_wave=random.Random(605442);wall_draws=[];throughput_draws=[]
  for _ in range(10000):
   sampled=random_wave.choices(wave_pairs,k=8);bs=sum(x['baseline']['makespan_seconds'] for x in sampled);ws=sum(x['wake-only']['makespan_seconds'] for x in sampled);bw=sum(x['baseline']['winners'] for x in sampled);ww=sum(x['wake-only']['winners'] for x in sampled)
   wall_draws.append(ws/bs-1)
   if bw:throughput_draws.append((ww/ws)/(bw/bs)-1)
  bs=sum(x['baseline']['makespan_seconds'] for x in wave_pairs);ws=sum(x['wake-only']['makespan_seconds'] for x in wave_pairs);bw=sum(x['baseline']['winners'] for x in wave_pairs);ww=sum(x['wake-only']['winners'] for x in wave_pairs)
  wave_throughput={'paired_wave_units':8,'games_per_wave':4,'bootstrap_draws':10000,'random_seed':605442,'makespan_ratio_change':ws/bs-1,'makespan_paired_wave_bootstrap95':interval(wall_draws),'baseline_completed_games_per_hour':bw*3600/bs,'wake_completed_games_per_hour':ww*3600/ws,'completed_games_per_hour_ratio_change':(ww/ws)/(bw/bs)-1 if bw else None,'throughput_paired_wave_bootstrap95':interval(throughput_draws) if throughput_draws else None,'undefined_zero_baseline_winner_draws':10000-len(throughput_draws),'caps':{role:sum(x[role]['caps'] for x in wave_pairs) for role in ['baseline','wake-only']},'raw_wave_pairs':wave_pairs,'scope':'Actual matched homogeneous4gamewave makespans, 8pairedwaveunits permap. Allwavesincludingcaps retained; cap consumeswindow andcontributes0 completedwins. NotpergameTPSmean orsumgamewall; conditionalon1hcap andfixed32seedgrouping. ThiswaveCI leads hostthroughput conclusions.'}
 report['maps'].append({'map':mapname,'eligible_seed_pairs':eligible,'excluded_pairs':excluded,'metrics':metrics,'role_summaries':summaries,'paired_wave_throughput':wave_throughput})
mismatches=[p for p in complete['pairs'] if p['both_winners'] and not p['endpoints_match']]
assert not mismatches,'Both-winner endpoint mismatch cannot validate';report['trajectory_mismatches']=[];report['artifact_validation']='passed';report['state']='validated'
(folder/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
