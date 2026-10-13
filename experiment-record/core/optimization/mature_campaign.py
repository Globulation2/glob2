#!/usr/bin/env python3
"""Finite mature/full-winner extension. Default writes plans ONLY; --run is explicit.
No existing campaign, fixture, benchmark or source file is modified.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time

ART = Path(__file__).resolve().parent
ROOT = ART.parents[1]
OUT = ART/'mature'

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m
C = module('mature_correctness', ART/'correctness_campaign.py')
P = module('mature_performance', ART/'performance_campaign.py')
load, write, sha, require = P.load, P.write, P.sha, P.require


def design(args):
    acceptance = load(ART/'acceptance-plan.json')
    campaign = load(ART/'campaign.json')
    variants = load(ART/'variant-builds.json')
    scheduler = load(ART/'variants/scheduler/manifest.json')
    scenarios = {s['id']: s for s in acceptance['scenario_design']}
    scenarios['holdout-comb'] = dict(id='holdout-comb', generator='comb', width=256,
        height=512, ais=['nicowar','cortex','maxima','cabino'], map_seed=1738462951)
    binaries = dict(baseline=dict(path=campaign['baseline_binary'],
                        root=campaign['baseline_source'], sha256=campaign['baseline_binary_sha256']),
                    combined=dict(path=str(ROOT/'build/linux/client/release/src/glob2'),
                        root=str(ROOT), sha256=sha(ROOT/'build/linux/client/release/src/glob2')))
    for name, v in variants['variants'].items():
        binaries[name] = dict(path=str(Path(v['root'])/'build/linux/client/release/src/glob2'),
                             root=v['root'], sha256=v['binary_sha256'])
    binaries['scheduler'] = dict(path=scheduler['binary'], root=scheduler['root'],
                                 sha256=scheduler['binary_sha256'], accepted=False)
    checkpoints = [dict(id=f'{sid}-{seed}', scenario=scenarios[sid], seed=seed,
                        target_tick=target, canonical_fixture=str(ART/args.correctness_name/f'{sid}-{seed}'/'fixture.json'))
                   for sid, target in [('playground-ni',20000),('oazis-ni',100000)]
                   for seed in acceptance['new_game_seeds']]
    full = []
    for sid in ['playground-ni','oazis-ni','holdout-comb']:
        for seed in acceptance['new_game_seeds']+[acceptance['holdout_game_seed']]:
            key=f'{sid}-{seed}'
            initial = ART/args.correctness_name/key/'fixture.json' if seed in acceptance['new_game_seeds'] and sid != 'holdout-comb' else OUT/'full-fixtures'/key/'fixture.json'
            # Alternate AB / BA per seed, preserving one mask for the pair.
            names = ['baseline','combined'] if len(full)%4 == 0 else ['combined','baseline']
            for name in names:
                full.append(dict(id=f'{key}-{name}', scenario=scenarios[sid], seed=seed,
                    variant=name, fixture_record=str(initial), affinity=P.MASKS[(len(full)//2)%4],
                    compute_threads=3, gpu='on', timeout_s=3600, tick_sentinel=2147483647,
                    candidate_acceptance='combined3 provisional; does not imply accepted scheduler',
                    native_replay='canonical --write-replay retained; no checksum observer'))
    return dict(state='plan-only', design_version=1, previous_design='mature/design-v0', base_commit=campaign['base_commit'],
        correctness_name=args.correctness_name, binaries=binaries, checkpoints=checkpoints,
        window_lengths=[4096,8192], counts=[3], gpu_strata=['off','on'],
        masks=P.MASKS, full_cases=full,
        stages=['prepare-full-fixtures','checkpoints','correctness','performance','full-native','full-diagnostic'],
        worklist=dict(checkpoints=4, mature_direct_diagnostic=96,
            mature_save_continuation_diagnostic=48, paired_window_jobs=96,
            native_window_children=768, native_full_children=18, full_diagnostic_children=18,
            heldout_map_generations=1, additional_initial_fixtures=5),
        seals={str(p):sha(p) for p in [Path(__file__), ART/'acceptance-plan.json',
            ART/'campaign.json', ART/'variant-builds.json', ART/'variants/scheduler/manifest.json',
            ART/'correctness_campaign.py', ART/'performance_campaign.py',
            ROOT/'test/benchmark_resource_refactor.py', ROOT/'test/benchmark_parallel_compute.py',
            ART/'map-preparation.json']},
        caveats=[
            'Plan only; no launches without explicit stage --run. No automatic scheduler adoption.',
            'New canonical replay cohort; historical legacy seeds/settings are not pooled.',
            'Checkpoint preparation GPUoff3 is diagnostic; early winner is retained and blocks windows.',
            'Loaded windows measure cache rebuilding and subsequent work, not continuous-process peak retention.',
            'Full native cases retain canonical replay output; AIT1 validation occurs after timing, separate heavy GCS cases.',
            'Exact GCS/world/replay equality applies only matched loaded prefixes; save continuity compares detailed suffix records, not header-dependent aggregate CRC.',
            'Two primary seeds plus one holdout per layout; repeated windows do not increase independent seed coverage.',
            'One-hour timeout is censored/flagged, never completed-game timing or silently shortened.',
            'Stock full fixtures must already be frozen; heldout generation deferred and hashed before simulation.',
            'Full natural outcome agreement and separate diagnostic traces do not establish every native tick equivalence.',
            'One candidate combined3 represents first three optimizations, excluding separate unaccepted scheduler.'])


def assert_seals(plan):
    for path, digest in plan['seals'].items():
        require(sha(path)==digest, f'sealed source/plan changed: {path}')
    for item in plan['binaries'].values():
        require(sha(item['path'])==item['sha256'], 'binary changed')


def fixture(record, scenario, seed):
    f=load(record)
    require(f['scenario']==scenario and f['seed']==seed, 'fixture setup changed')
    path=Path(f['path'])
    header=C.save_header(path)
    require(header==f['header'], 'fixture bytes/header changed')
    return path


def prepare_full(plan):
    maps={m['id']:m for m in load(ART/'map-preparation.json')}
    scenario=next(c['scenario'] for c in plan['full_cases'] if c['scenario']['id']=='holdout-comb')
    mp=OUT/'maps/holdout-comb.map'
    report=OUT/'maps/holdout-comb.json'
    require(not report.exists(), 'holdout generation directory must be fresh')
    report.parent.mkdir(parents=True,exist_ok=True)
    binary=Path(plan['binaries']['baseline']['path'])
    command=['taskset','-c','12-14,28-30',str(binary),'map','generate','comb',
       '--seed',str(scenario['map_seed']),'--width','256','--height','512','--teams','4',
       '--output',str(mp),'--report-file',str(report)]
    write(OUT/'holdout-generation-invocation.json',dict(command=command,binary_sha256=sha(binary)))
    subprocess.run(command,cwd=plan['binaries']['baseline']['root'],env=C.clean_env(OUT/'map-profile'),check=True,timeout=3600)
    require(Path(str(mp)+'.gz').is_file(), 'expected compressed map missing')
    maps['holdout-comb']=dict(path=str(mp)+'.gz',sha256=sha(str(mp)+'.gz'))
    write(OUT/'holdout-map.json',maps['holdout-comb'])
    seen=set()
    for case in plan['full_cases']:
        record=Path(case['fixture_record'])
        if '/full-fixtures/' not in str(record) or str(record) in seen: continue
        seen.add(str(record)); s=case['scenario']; m=maps[s['id']]
        require(sha(m['path'])==m['sha256'],'map changed')
        directory=record.parent/'fixture'
        result=C.execute(binary,directory,['--map-file',m['path'],'--game-seed',str(case['seed']),
            '--ticks','1','--compute-threads','1','--gpu','off','--telemetry','checksums',
            '--save','initial']+[v for ai in s['ais'] for v in ('--player',ai)])
        C.validate_result(result,s,case['seed'])
        saved=directory/'initial.game.gz'
        header=C.save_header(saved)
        require((header['width'],header['height'],header['teams'])==(s['width'],s['height'],len(s['ais'])), 'new fixture geometry')
        write(record,dict(path=str(saved),header=header,start_tick=0,scenario=s,seed=case['seed'],map=m))


def checkpoint_cases(plan, only):
    selected=[c for c in plan['checkpoints'] if not only or c['id']==only]
    require(selected, f'no checkpoint matches --only {only!r}')
    return selected


def checkpoints(plan, only):
    for case in checkpoint_cases(plan,only):
        s=case['scenario']; initial=fixture(case['canonical_fixture'],s,case['seed'])
        folder=OUT/'checkpoints'/case['id']
        actual=C.run_case(Path(plan['binaries']['baseline']['path']),folder/'preparation',initial,
            s,case['seed'],3,'off',case['target_tick'],'final')
        saved=folder/'preparation/final.game.gz'
        write(folder/'checkpoint.json',dict(path=str(saved),header=actual['final_header'],
            start_tick=case['target_tick'],scenario=s,seed=case['seed'],
            initial_fixture_sha256=sha(initial), preparation=actual,
            lifetime_scope='fresh process loaded continuation; earlier allocator state not retained'))


def correctness(plan, only):
    for case in checkpoint_cases(plan,only):
        folder=OUT/'checkpoints'/case['id']; saved=fixture(folder/'checkpoint.json',case['scenario'],case['seed'])
        expected={}
        for gpu in plan['gpu_strata']:
            for name, entry in plan['binaries'].items():
                for window in plan['window_lengths']:
                    end=case['target_tick']+window
                    label=f'{name}-gpu{gpu}-w{window}'
                    actual=C.run_case(Path(entry['path']),OUT/'correctness'/case['id']/label,saved,
                        case['scenario'],case['seed'],3,gpu,end,'final')
                    require(actual['gcs']['first_tick']==case['target_tick'],'loaded checkpoint start changed')
                    if (gpu,window) not in expected: expected[gpu,window]=actual
                    else: C.same(actual,expected[gpu,window],label)
                    if gpu=='on' and name=='baseline': C.same(actual,expected['off',window],'GPU arithmetic')
                # Continue each 4096 save to8192, compare detailed suffix to baseline uninterrupted.
                final=OUT/'correctness'/case['id']/f'{name}-gpu{gpu}-w4096/final.game.gz'
                resumed=OUT/'correctness'/case['id']/f'{name}-gpu{gpu}-continued8192'
                C.run_case(Path(entry['path']),resumed,final,case['scenario'],case['seed'],3,gpu,
                    case['target_tick']+8192,'final')
                source=OUT/'correctness'/case['id']/f'baseline-gpu{gpu}-w8192/game.replay.checksums'
                exact=C.CONT.compare(source,resumed/'game.replay.checksums')
                write(resumed/'continuation.json',dict(passed=True,detailed_suffix=exact,
                    aggregate_excluded='save/header representation checksum',native_timing_excluded=True))
        write(OUT/'correctness'/case['id']/'passed.json',dict(passed=True,
            checkpoint_sha256=sha(saved),seals=plan['seals'],binaries=plan['binaries']))


def performance(plan, only, run):
    jobs=[]
    for case in checkpoint_cases(plan,only):
        f=load(OUT/'checkpoints'/case['id']/'checkpoint.json'); path=f['path']
        require(sha(path)==f['header']['gzip_sha256'],'checkpoint changed')
        require(C.save_header(path)==f['header'],'checkpoint header/geometry changed')
        for gpu in plan['gpu_strata']:
            for window in plan['window_lengths']:
                for factor in ['worker','snapshot','telemetry','scheduler','combined'] + (['workerauto','combinedauto'] if gpu=='off' else []):
                    count='auto' if factor.endswith('auto') else '3'
                    name=factor.removesuffix('auto')
                    label=f"{case['id']}-{factor}-gpu{gpu}-w{window}"
                    manifest=OUT/'performance-manifests'/f'{label}.json'
                    write(manifest,dict(scenarios=[dict(id=label,start_tick=case['target_tick'],
                        fixture_sha256={path:sha(path)},args=['--load-game',path,'--ticks',
                        str(case['target_tick']+window),'--compute-threads',count,'--gpu',gpu,'--write-replay'])]))
                    before=plan['binaries']['baseline']; after=plan['binaries'][name]
                    job=dict(id=label,scenario=case['scenario']['id'],seed=case['seed'],factor=f'{factor}-gpu{gpu}-w{window}',
                        before=before['path'],after=after['path'],before_root=before['root'],after_root=after['root'],
                        manifest=str(manifest),output=str(OUT/'performance'/label),mask=plan['masks'][len(jobs)%4],
                        mature=dict(checkpoint_tick=case['target_tick'],window=window,save_sha256=sha(path)),
                        identity=dict(wrapper_sha256=sha(ART/'performance_campaign.py'),before_sha256=before['sha256'],
                         after_sha256=after['sha256'],manifest_sha256=sha(manifest),
                         benchmark_sha256=sha(ROOT/'test/benchmark_resource_refactor.py'),
                         execute_sha256=sha(ROOT/'test/benchmark_parallel_compute.py')))
                    jobpath=OUT/'performance-jobs'/f'{label}.json';write(jobpath,job);jobs.append((jobpath,job))
    write(OUT/'performance-jobs.json',[job for _,job in jobs])
    if not run:return
    for case in checkpoint_cases(plan,only):
        gate=load(OUT/'correctness'/case['id']/'passed.json')
        checkpoint=load(OUT/'checkpoints'/case['id']/'checkpoint.json')
        require(gate['checkpoint_sha256']==sha(checkpoint['path'])==checkpoint['header']['gzip_sha256'],'checkpoint no longer matches correctness gate')
        require(C.save_header(checkpoint['path'])==checkpoint['header'],'checkpoint header changed after correctness')
        require(gate['passed'] and gate['seals']==plan['seals'] and gate['binaries']==plan['binaries'],'mature correctness gate changed')
    errors=[]
    def lane(index):
        for path,job in jobs[index::4]:
            if errors:return
            try:
                subprocess.run([sys.executable,str(ART/'performance_campaign.py'),'--worker',str(path)],check=True)
            except Exception as e:errors.append(str(e));return
    threads=[threading.Thread(target=lane,args=(i,)) for i in range(4)]
    for t in threads:t.start()
    for t in threads:t.join()
    require(not errors,errors)
    P.aggregate([job for _,job in jobs],OUT/'performance')


def full_worker(jobpath):
    # Fresh controller/case: wait4 RSS cannot inherit an analysis graph high-watermark.
    job=load(jobpath); os.sched_setaffinity(0,set(job['affinity']))
    require(sha(Path(__file__))==job['wrapper_sha256'],'full wrapper changed')
    require(sha(OUT/'plan.json')==job['plan_sha256'],'full plan changed')
    assert_seals(load(OUT/'plan.json'))
    sys.path.insert(0,str(ROOT/'test'))
    import benchmark_resource_refactor as bench
    binary=Path(job['binary']['path']); directory=Path(job['output']);require(not directory.exists(),'fresh output required')
    directory.mkdir(parents=True); initial=fixture(job['fixture_record'],job['scenario'],job['seed'])
    os.environ.clear();os.environ.update(job['environment']);
    inputs=lambda:bench.capture_inputs({'candidate':binary},{'candidate':Path(job['binary']['root'])},Path(jobpath),{initial})
    before=inputs();require(sha(binary)==job['binary']['sha256'],'full binary changed')
    command=[str(binary),'game','run','--load-game',str(initial),'--ticks',str(job['tick_sentinel']),
        '--compute-threads','3','--gpu','on','--write-replay','--output-dir',str(directory)]
    if job['diagnostic']: command+=['--telemetry','checksums']
    import resource
    parent_hwm=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024
    timed_out=threading.Event()
    started=time.monotonic()
    with (directory/'engine.log').open('wb') as log:
        process=subprocess.Popen(command,cwd=job['binary']['root'],stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        def kill():
            timed_out.set()
            try:os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError:pass
        timer=threading.Timer(job['timeout_s'],kill);timer.start()
        try:
            _,status,usage=os.wait4(process.pid,0)
            process.returncode=os.waitstatus_to_exitcode(status)
        except BaseException:
            # Never cancel the watchdog and abandon a live process group.
            try:os.killpg(process.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            try:os.wait4(process.pid,0)
            except ChildProcessError:pass
            raise
        finally:timer.cancel()
    wall=time.monotonic()-started;after=inputs()
    require(not bench.changed_inputs(before,after),'full input/library/environment changed')
    result=load(directory/'result.json') if (directory/'result.json').exists() else None
    record=dict(command=command,returncode=process.returncode,timed_out=timed_out.is_set(),
        wall_s=wall,cpu_s=usage.ru_utime+usage.ru_stime,user_s=usage.ru_utime,system_s=usage.ru_stime,
        peak_rss_bytes=usage.ru_maxrss*1024,runner_peak_rss_bytes=parent_hwm,
        rss_censored=usage.ru_maxrss*1024<=parent_hwm,result=result,job=job,
        diagnostic=job['diagnostic'],timing_excluded=job['diagnostic'],input_verification=dict(before=before,after=after,unchanged=True))
    write(directory/'measurement.json',record)
    if timed_out.is_set():return # Explicit censored outcome, retained; never a completion statistic.
    require(process.returncode==0 and result is not None,'full engine error')
    C.validate_result(result,job['scenario'],job['seed'])
    require(result['termination']=='engine_end' and result['winning_teams'],'full case did not reach natural winner')
    require(not record['rss_censored'],'full RSS inherited-runner floor')
    # Outside native measurement boundaries; each next case gets a fresh controller.
    replay=dict(ait1=C.ait1(directory/'game.replay'),replay_sha256=sha(directory/'game.replay'),
                replay_bytes=(directory/'game.replay').stat().st_size)
    write(directory/'replay-validation.json',replay)
    if job['diagnostic']:
        gcs=C.trace(directory/'game.replay.checksums');world=C.world_trace(directory/'world.checksums',gcs['first_tick'],result['ticks'])
        write(directory/'validated.json',dict(gcs=gcs,world=world,**replay))


def full(plan,diagnostic,only,run):
    jobs=[]
    for case in plan['full_cases']:
        if only and not case['id'].startswith(only):continue
        key='full-diagnostic' if diagnostic else 'full-native';directory=OUT/key/case['id']
        job=dict(case,binary=plan['binaries'][case['variant']],output=str(directory),diagnostic=diagnostic,
            environment=C.clean_env(directory),wrapper_sha256=sha(Path(__file__)),plan_sha256=sha(OUT/'plan.json'))
        # Preserve environment for execution but record no credentials in job artifacts.
        job['environment']={k:v for k,v in job['environment'].items() if k in ('PATH','LD_LIBRARY_PATH','LANG','LC_ALL','SDL_VIDEODRIVER','SDL_AUDIODRIVER') or k.startswith('GLOB2_')}
        path=OUT/'full-jobs'/f"{key}-{case['id']}.json";write(path,job);jobs.append((path,job))
    require(jobs,f'no full cases match --only {only!r}')
    if not run:return
    for case in plan['checkpoints']:
        gate=load(OUT/'correctness'/case['id']/'passed.json')
        checkpoint=load(OUT/'checkpoints'/case['id']/'checkpoint.json')
        require(gate['checkpoint_sha256']==sha(checkpoint['path'])==checkpoint['header']['gzip_sha256'],'full gate checkpoint changed')
        require(C.save_header(checkpoint['path'])==checkpoint['header'],'full gate checkpoint header changed')
        require(gate['passed'] and gate['seals']==plan['seals'] and gate['binaries']==plan['binaries'],'mature correctness gate required before full runs')
    if diagnostic:
        unavailable=[]
        for _,j in jobs:
            reference=OUT/'full-native'/j['id']/'measurement.json'
            if not reference.exists():
                unavailable.append(dict(case=j['id'],reason='missing native reference'))
            else:
                native=load(reference)
                if native.get('timed_out') or native.get('returncode')!=0 or native.get('result') is None:
                    unavailable.append(dict(case=j['id'],reason='native censored or failed',timed_out=native.get('timed_out')))
                elif native['job']['binary']!=j['binary'] or native['job']['fixture_record']!=j['fixture_record'] or native['job']['affinity']!=j['affinity']:
                    unavailable.append(dict(case=j['id'],reason='native identity mismatch'))
                elif not (reference.parent/'replay-validation.json').exists():
                    unavailable.append(dict(case=j['id'],reason='native replay validation missing'))
        if unavailable:
            write(OUT/'full-diagnostic-preflight.json',dict(passed=False,launches=0,unavailable=unavailable,action='parent must explicitly select complete paired native references; no silent skips'))
            raise ValueError('Full diagnostics blocked by unavailable native references; see full-diagnostic-preflight.json')
        write(OUT/'full-diagnostic-preflight.json',dict(passed=True,checked_cases=len(jobs),unavailable=[]))
    errors=[]
    def lane(index):
        for path,job in jobs:
            if job['affinity']!=plan['masks'][index] or errors:continue
            try:subprocess.run([sys.executable,str(Path(__file__)),'--full-worker',str(path)],check=True)
            except Exception as e:errors.append(str(e));return
    threads=[threading.Thread(target=lane,args=(i,)) for i in range(4)]
    for t in threads:t.start()
    for t in threads:t.join()
    require(not errors,errors)
    for s in {j['scenario']['id'] for _,j in jobs}:
        for seed in {j['seed'] for _,j in jobs if j['scenario']['id']==s}:
            pair=[j for _,j in jobs if j['scenario']['id']==s and j['seed']==seed]
            require(len(pair)==2,'full pair incomplete')
            rows=[load(Path(j['output'])/'measurement.json') for j in pair]
            if any(r['timed_out'] for r in rows):continue
            keys=['ticks','termination','winning_teams']
            require({k:rows[0]['result'][k] for k in keys}=={k:rows[1]['result'][k] for k in keys},'full natural outcome differs')
            native_replays=[load(Path(j['output'])/'replay-validation.json') for j in pair]
            require(native_replays[0]==native_replays[1],'full matched replay/AIT1 differs')
            if diagnostic:
                for j,r in zip(pair,rows):
                    native_path=OUT/'full-native'/j['id']/'measurement.json'
                    require(native_path.exists(),'native reference missing for full diagnostic')
                    native=load(native_path)
                    require(not native['timed_out'] and {k:r['result'][k] for k in keys}=={k:native['result'][k] for k in keys},'diagnostic natural outcome differs from native')
                a,b=[load(Path(j['output'])/'validated.json') for j in pair]
                require(a==b,'full diagnostic stream/replay differs')
    write(OUT/('full-diagnostic-complete.json' if diagnostic else 'full-native-complete.json'),dict(passed=not any(load(Path(j['output'])/'measurement.json')['timed_out'] for _,j in jobs),
        state='complete_with_censoring' if any(load(Path(j['output'])/'measurement.json')['timed_out'] for _,j in jobs) else 'complete',
        compared_pairs=sum(not any(load(Path(j['output'])/'measurement.json')['timed_out'] for _,j in jobs if j['scenario']['id']==sid and j['seed']==seed) for sid,seed in {(j['scenario']['id'],j['seed']) for _,j in jobs}),
        timeout_cases=[j['id'] for _,j in jobs if load(Path(j['output'])/'measurement.json')['timed_out']],
        scope='Matched natural outcomes; diagnostic stream comparisons are separate from native timings'))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true')
    p.add_argument('--stage',choices=['plan','prepare-full-fixtures','checkpoints','correctness','performance','full-native','full-diagnostic'],default='plan')
    p.add_argument('--correctness-name',default='correctness');p.add_argument('--only',help='checkpoint ID or full scenario-seed prefix')
    p.add_argument('--full-worker',type=Path);args=p.parse_args()
    if args.full_worker:full_worker(args.full_worker);return
    require(args.correctness_name not in ('','.','..') and '/' not in args.correctness_name,'invalid fixture campaign')
    sealed=OUT/'plan.json'
    if args.stage=='plan':
        require(not args.run,'plan stage never launches');plan=design(args);write(sealed,plan)
        full(plan,False,None,False);full(plan,True,None,False)
        print(json.dumps(dict(plan=str(sealed),worklist=plan['worklist'],state='plan-only'),indent=2));return
    plan=load(sealed);assert_seals(plan)
    if not args.run:
        print(json.dumps(dict(stage=args.stage,state='plan-only',launches=0)));return
    if args.stage=='prepare-full-fixtures':prepare_full(plan)
    elif args.stage=='checkpoints':checkpoints(plan,args.only)
    elif args.stage=='correctness':correctness(plan,args.only)
    elif args.stage=='performance':performance(plan,args.only,True)
    else:full(plan,args.stage=='full-diagnostic',args.only,True)

if __name__=='__main__':main()
