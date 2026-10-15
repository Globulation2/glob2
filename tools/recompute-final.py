#!/usr/bin/env python3
"""Recompute completed final-cohort evidence from relative bundle paths. No games."""
import argparse,hashlib,json,math,platform,random,re,statistics,sys
from pathlib import Path,PurePosixPath
if not __debug__:raise SystemExit('Use python3 without -O: validation assertions must be enabled')
MAPS=('Playground','Oazis');ROLES=('baseline','wake-only')
def read(path):
    with Path(path).open(encoding='utf-8') as f:return json.load(f)
def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
def child(root,relative):
    rel=PurePosixPath(relative)
    assert not rel.is_absolute() and '..' not in rel.parts,relative
    target=root.joinpath(*rel.parts)
    target.resolve().relative_to(root.resolve())
    return target

def locate(argument):
    starts=[Path(argument)] if argument else [Path.cwd(),Path(__file__).resolve().parent,*Path(__file__).resolve().parents]
    for start in starts:
        for root in [start,start/'evidence/confirmation/final-cohort',start/'confirmation/final-cohort']:
            if (root/'plan.json').is_file() and any((root/r).is_dir() for r in ('serial','waves')):return root.resolve()
    raise ValueError('No final-cohort directory: pass --root BUNDLE or its evidence/confirmation/final-cohort directory')
def parse_log(text,mapname,participants,eligible,returncode,capped):
 expected=(128,128,8,9) if mapname=='Playground' else (256,256,11,12)
 matches=re.findall(r'^HISTORY_RESULT (.*)$',text,re.M)
 if capped:
  v=dict(re.findall(r'(\w+)=([^\s]+)',matches[-1])) if matches else {}
  def number(key,kind=int):
   try:return kind(v[key])
   except (KeyError,ValueError):return None
  clocks=re.findall(r'^HISTORY_TIMING run_wall_seconds=([0-9.eE+-]+)$',text,re.M)
  try:inner=float(clocks[-1]) if clocks else None
  except ValueError:inner=None
  return {'status':'cap','ticks':number('ticks'),'winner':None,'reported':v,'roster_valid':None,'inner_seconds':inner,'observed_checksum':v.get('checksum') or v.get('finalChecksum'),'parse_warning':'Censored: partial/truncated summary permitted; dimensions/roster/terminal winner not asserted.'}
 assert len(matches)<=1
 v=dict(re.findall(r'(\w+)=([^\s]+)',matches[0])) if matches else {}
 clocks=re.findall(r'^HISTORY_TIMING run_wall_seconds=([0-9.eE+-]+)$',text,re.M);assert len(clocks)<=1
 roster=re.findall(r'Player: (Nicowar[^\n]*) for team (\d+)',text)
 roster_ok=len(roster)==expected[2] and sorted(int(t) for _,t in roster)==list(range(expected[2]))
 if v:
  assert tuple(int(v[k]) for k in ['width','height','teams','controllers'])==expected
  assert int(v['compute_participants'])==participants and int(v['eligible_cpus'])==eligible and v['gradient_backend']=='cpu'
  assert roster_ok,'Wrong AI roster'
 ticks=int(v['ticks']) if v else None;winner=int(v.get('winner',-1));ended=int(v.get('ended',0))
 winner_ok=not capped and returncode==0 and bool(v) and ended==1 and 0<=winner<expected[2] and roster_ok
 return {'status':'cap' if capped else 'winner' if winner_ok else 'failed' if returncode or not v else 'no_winner','ticks':ticks,'winner':winner if winner_ok else None,'reported':v,'roster_valid':roster_ok,'inner_seconds':float(clocks[0]) if clocks else None,'observed_checksum':v.get('checksum') or v.get('finalChecksum')}

def load_regime(root,regime):
    folder=root/regime;plan=read(root/'plan.json');complete=read(folder/'complete.json')
    assert len(plan['seeds'])==len(set(plan['seeds']))==32
    assert complete['state']=='complete' and complete['runs']==128 and complete['regime']==regime
    assert complete['plan_sha256']==digest(root/'plan.json')
    assert complete['launcher_sha256']==digest(root/'launcher.py')
    paths=sorted(folder.glob('*/*/receipt.json'))
    assert len(paths)==128 and set(complete['receipt_hashes'])=={p.relative_to(folder).as_posix() for p in paths}
    for p in paths:assert digest(p)==complete['receipt_hashes'][p.relative_to(folder).as_posix()]
    rows=[read(p) for p in paths]
    expected={(m,s,r) for m in MAPS for s in plan['seeds'] for r in ROLES}
    assert len({(r['map'],r['seed'],r['role']) for r in rows})==128
    assert {(r['map'],r['seed'],r['role']) for r in rows}==expected
    # Resolve only relative packaged metadata; never open absolute original paths.
    resolved_path=root.parent/'wake-only-allcore/latest-resolve-complete.json'
    assert resolved_path.is_file(),'Bundle must include evidence/confirmation/wake-only-allcore/latest-resolve-complete.json'
    resolved=read(resolved_path);assert resolved['base_revision']==plan['base_revision']
    variants={v['role']:v for v in resolved['variants']};assert set(variants)==set(ROLES)
    approval_path=root/'root-approved.json';approval=read(approval_path) if approval_path.is_file() else None
    if approval:
        assert digest(resolved_path)==approval['latest_resolve_sha256']
    omitted=[]
    for r in rows:
        path=folder/r['map']/(str(r['seed'])+'-'+r['role'])/'stdout.log'
        assert digest(path)==r['stdout_sha256']
        v=variants[r['role']]
        assert r['binary_sha256']==v['sha256'] and r['cwd']==v['cwd'] and r['cap_seconds']==plan['cap_seconds']
        if approval:assert r['binary_sha256']==approval['binary_hashes'][r['role']]
        declared=next(x for x in plan['declared_commands'][regime] if (x['map'],x['seed'],x['role'])==(r['map'],r['seed'],r['role']))
        assert r['command']==declared['command'] and r['allocation']==declared['allocation'] and r['compute_requested']==declared['compute']
        assert hashlib.sha256(json.dumps(r['command'],separators=(',',':')).encode()).hexdigest()==r['command_sha256']
        profile=declared['profile']
        assert r['env_overrides']==dict(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=profile,GLOB2_USER_DATA_DIR=profile,GLOB2_TEST_SEED=str(r['seed']),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd'])
        assert r['fixture_sha256']==plan['fixtures'] and r['base_revision']==plan['base_revision']
        assert r['status'] in ('winner','cap','no_winner')
        parsed=parse_log(path.read_text(encoding='utf-8',errors='replace'),r['map'],32 if regime=='serial' else 6,32 if regime=='serial' else 6,r['returncode'],r['status']=='cap')
        for key in ('status','ticks','winner','reported','inner_seconds','observed_checksum'):assert parsed[key]==r[key],(r['map'],r['seed'],r['role'],key)
        for item in r['checksum_files']:
            sidecar=child(path.parent,item['path'])
            if sidecar.is_file():assert digest(sidecar)==item['sha256']
            else:omitted.append({'receipt':path.parent.relative_to(root).as_posix()+'/receipt.json','path':item['path'],'declared_sha256':item['sha256']})
    lookup={(r['map'],r['seed'],r['role']):r for r in rows}
    assert len(complete['pairs'])==64 and {(p['map'],p['seed']) for p in complete['pairs']}=={(m,s) for m in MAPS for s in plan['seeds']}
    for pair in complete['pairs']:
        b=lookup[pair['map'],pair['seed'],'baseline'];w=lookup[pair['map'],pair['seed'],'wake-only']
        both=b['status']==w['status']=='winner';same=both and (b['ticks'],b['winner'])==(w['ticks'],w['winner'])
        if both and b['checksum_files'] and w['checksum_files']:
            be=[(x.get('rows'),x.get('first'),x.get('last')) for x in b['checksum_files'] if x['complete']]
            we=[(x.get('rows'),x.get('first'),x.get('last')) for x in w['checksum_files'] if x['complete']]
            same=same and bool(be) and be==we
        if both and b['observed_checksum'] is not None and w['observed_checksum'] is not None:same=same and b['observed_checksum']==w['observed_checksum']
        assert pair['both_winners']==both and pair['endpoints_match']==(same if both else None) and pair['eligible_winner_timing_pair']==same
        assert not both or same,('Both-winner trajectory mismatch',pair)
    wavefiles=list(folder.glob('wave-*.json'))
    assert set(complete['wave_hashes'])=={p.relative_to(folder).as_posix() for p in wavefiles}
    for p in wavefiles:assert digest(p)==complete['wave_hashes'][p.relative_to(folder).as_posix()]
    width=1 if regime=='serial' else 4
    order=[(m,n,r) for m in MAPS for n in range(32//width) for r in (ROLES if n%2==0 else tuple(reversed(ROLES)))]
    assert [(w['map'],w['wave'],w['role']) for w in complete['waves']]==order
    for wave in complete['waves']:
        path=folder/('wave-'+str(wave['wave'])+'-'+wave['map']+'-'+wave['role']+'.json')
        assert read(path)==wave
        span=wave['makespan_seconds'];assert span>0 and abs(wave['finished_monotonic']-wave['started_monotonic']-span)<1e-6
        indices=list(range(wave['wave']*width,(wave['wave']+1)*width));assert wave['seed_indices']==indices
        members=[lookup[wave['map'],plan['seeds'][i],wave['role']] for i in indices]
        assert all(wave['started_monotonic']<=r['started_monotonic']<=r['finished_monotonic']<=wave['finished_monotonic'] for r in members)
        assert wave['winners']==sum(r['status']=='winner' for r in members) and wave['caps']==sum(r['status']=='cap' for r in members)
        assert math.isclose(wave['sum_game_seconds'],sum(r['e2e_seconds'] for r in members),rel_tol=1e-12,abs_tol=1e-6)
        assert math.isclose(wave['games_per_hour'],wave['winners']*3600/span,rel_tol=1e-12,abs_tol=1e-6)
        assert wave['complete_tick_coverage']==all(r['ticks'] is not None for r in members)
    for b,w in zip(complete['waves'],complete['waves'][1:]):assert b['finished_monotonic']<=w['started_monotonic'],'Role waves overlap'
    assert complete['winners']==sum(r['status']=='winner' for r in rows) and complete['caps']==sum(r['status']=='cap' for r in rows)
    assert complete['campaign_makespan_seconds']>=complete['waves'][-1]['finished_monotonic']-complete['waves'][0]['started_monotonic']
    assert math.isclose(complete['campaign_completed_games_per_hour'],complete['winners']*3600/complete['campaign_makespan_seconds'],rel_tol=1e-12,abs_tol=1e-6)
    return plan,complete,rows,{'omitted_optional_sidecars':omitted,'optional_root_approval_present':approval is not None,'receipt_hashes_verified':128,'wave_hashes_verified':len(wavefiles),'resolve_sha256':digest(resolved_path)}

def recompute(plan,complete,rows,regime):
    rng=random.Random(605441);report={'scope':'32 declared seeds, one game per role/seed; paired bootstrap10000 fixedseed605441, conditional on both matching successful winners. Caps separately censored; no exact Cartesian resampling and no warmups.','regime':regime,'campaign_makespan_seconds':complete['campaign_makespan_seconds'],'campaign_completed_games_per_hour':complete['campaign_completed_games_per_hour'],'maps':[],'waves':complete['waves']}
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
      if regime=='waves':
       keys=list(clusters)
       for _ in range(10000):
        indices=[i for key in rng.choices(keys,k=len(keys)) for i in clusters[key]];before_cluster=sum(b[i] for i in indices);
        if before_cluster:clustered.append(sum(w[i] for i in indices)/before_cluster-1)
      metrics[field]={'eligible':len(selected),'undefined_zero_reference_seed_draws':10000-len(ratios),'wave_cluster_count':len(clusters) if regime=='waves' else None,'wave_clustered_bootstrap95':interval(clustered) if regime=='waves' and len(clusters)>1 and clustered else None,'baseline_mean':before,'wake_mean':after,'ratio_of_means_change':after/before-1 if before else None,'paired_bootstrap95':interval(ratios) if len(selected)>1 and ratios else None,'conditional_on':'Both matching winners, metric present. Seed bootstrap treats seeds independently; waves also supplies paired four-seed wave-cluster interval to account shared host windows.'}
     summaries={}
     for role in ['baseline','wake-only']:
      rr=[r for r in rows if r['map']==mapname and r['role']==role];wins=[r for r in rr if r['status']=='winner'];ww=[w for w in complete['waves'] if w['map']==mapname and w['role']==role]
      summaries[role]={'runs':len(rr),'winners':len(wins),'caps':sum(r['status']=='cap' for r in rr),'no_winner':sum(r['status']=='no_winner' for r in rr),'all_process_cpu_seconds':sum(r['total_cpu_seconds'] for r in rr),'all_process_system_cpu_seconds':sum(r['system_cpu_seconds'] for r in rr),'max_process_peak_rss_kib':max(r['peak_rss_kib'] for r in rr),'all_voluntary_context_switches':sum(r['voluntary_context_switches'] for r in rr),'all_involuntary_context_switches':sum(r['involuntary_context_switches'] for r in rr),'winner_mean_inner_tps':statistics.mean(r['inner_tps'] for r in wins if r['inner_tps'] is not None) if any(r['inner_tps'] is not None for r in wins) else None,'winner_ticks_over_sum_game_e2e_seconds':sum(r['ticks'] for r in wins)/sum(r['e2e_seconds'] for r in wins) if wins else None,'actual_role_wave_makespan_sum_seconds':sum(w['makespan_seconds'] for w in ww),'completed_games_per_role_active_wave_hour':sum(w['winners'] for w in ww)*3600/sum(w['makespan_seconds'] for w in ww),'note':'Role wave active windows exclude interleaved other role; not fictitious contiguous per-role campaign span. Wave tick throughput with missing cap tick counts is incomplete; report raw waves.'}
     wave_throughput=None
     if regime=='waves':
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
    report['state']='validated_recomputation';report['artifact_validation']='passed_for_packaged_inputs'
    return report

def compare(got,want,path=''):
    if isinstance(got,dict):
        assert set(got)==set(want),(path,'keys')
        for k in got:compare(got[k],want[k],path+'/'+k)
    elif isinstance(got,list):
        assert len(got)==len(want),(path,'length')
        for i,(a,b) in enumerate(zip(got,want)):compare(a,b,path+'/'+str(i))
    elif isinstance(got,(float,int)) and not isinstance(got,bool):
        assert math.isclose(got,want,rel_tol=1e-12,abs_tol=1e-12),(path,got,want)
    else:assert got==want,(path,got,want)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--root',help='Bundle, coordination root or direct final-cohort input directory; auto-detect if omitted');ap.add_argument('--regime',choices=('serial','waves','both'),default='both');ap.add_argument('--output',type=Path,help='New output JSON, otherwise stdout');ap.add_argument('--compare-retained',action='store_true',help='Compare numerical maps/waves with packaged analysis.json; never overwrite it');args=ap.parse_args()
    root=locate(args.root);result={'state':'validated_recomputation','runtime':{'python':sys.version,'implementation':platform.python_implementation()},'algorithm':{'draws':10000,'seed_metrics_random_seed':605441,'wave_rate_random_seed':605442,'random':'stdlib random.Random/choices; canonical analyzer order preserved','quantile':'Sorted floor index int(p*(N-1)), p=.025/.975, no interpolation','metric_units':'Ratio changes and CI endpoints are fractions, not percent; multiply100 for percentages','independence':'One paired seed/game, no repeats; four-game regime additionally resamples paired four-seed clusters and8 paired actual wave spans. Caps consume wave windows and count0 wins.','limits':'No universal optimum, population/causal worker-count or production acceptance. Different regimes also change affinity/concurrency.'},'regimes':{}}
    requested=('serial','waves') if args.regime=='both' else (args.regime,)
    if args.output:assert not args.output.exists(),'Never overwrite an earlier recomputation'
    # Validate every requested regime before the first bootstrap, so incomplete bundles fail early.
    loaded={regime:load_regime(root,regime) for regime in requested}
    for regime in requested:
        plan,complete,rows,integrity=loaded[regime];report=recompute(plan,complete,rows,regime);report['integrity']=integrity
        if args.compare_retained:
            retained=read(root/regime/'analysis.json')
            assert retained['state']=='validated' and retained['artifact_validation']=='passed'
            for key in ('maps','waves','campaign_makespan_seconds','campaign_completed_games_per_hour'):compare(report[key],retained[key],regime+'/'+key)
            report['retained_analysis_comparison']='matched within 1e-12 floating-point tolerance'
        result['regimes'][regime]=report
    result['evidence_limits']=['Original absolute command/cwd/environment strings are compared as provenance only; none is opened or executed. Input resolution uses relative bundle layout only.','Binaries, engine runtime dependencies and original fixture trees are not required or executed. Their reported hashes/declared identities are checked, not their absent bytes.','Optional checksum sidecar bodies may be omitted; missing bodies are explicit. Endpoint metadata is not full per-tick simulation proof.','This recomputes canonical Python RNG/floor-quantile estimators, not independent V8/root PRNG/linear-quantile sensitivity runs. CPython/version recorded; compare retained numbers within rounding tolerance.']
    text=json.dumps(result,indent=2)+'\n'
    if args.output:
        with args.output.open('x',encoding='utf-8') as f:f.write(text)
    else:sys.stdout.write(text)
if __name__=='__main__':main()
