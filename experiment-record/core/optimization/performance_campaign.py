#!/usr/bin/env python3
"""Plan-only four-slot release paired campaign. --run starts fresh one-fixture workers.
Uses the versioned resource benchmark; no correctness tracing in measured games.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
ART = Path(__file__).resolve().parent
METRICS = ('simulation_cpu_s','simulation_wall_s','cpu_s','wall_s','peak_rss_bytes',
           'process_cpu_per_tick_s','simulation_cpu_per_tick_s','ticks_per_run_second')
MASKS = [[0,1,2,16,17,18], [3,4,5,19,20,21],
         [6,7,8,22,23,24], [9,10,11,25,26,27]]

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for part in iter(lambda: f.read(1024*1024), b''):
            h.update(part)
    return h.hexdigest()

def load(path):
    return json.loads(Path(path).read_text())

def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value, indent=2)+'\n')
    temp.replace(path)

def require(ok, message):
    if not ok:
        raise ValueError(message)

def worker(jobpath):
    # Import the audit/benchmark modules only in a fresh one-fixture process.
    job = load(jobpath)
    os.sched_setaffinity(0, set(job['mask']))
    for key in list(os.environ):
        if key.startswith('GLOB2_'):
            del os.environ[key]
    os.environ['SDL_VIDEODRIVER'] = 'dummy'
    os.environ['SDL_AUDIODRIVER'] = 'dummy'
    sys.path.insert(0, str(ROOT/'test'))
    import benchmark_resource_refactor as bench
    original = bench.execute
    def isolated(binary, args, output, *, cwd=ROOT):
        previous = {key: os.environ.get(key) for key in ('GLOB2_USER_DIR', 'GLOB2_USER_DATA_DIR')}
        updates = {key: str(output/'profile') for key in previous}
        os.environ.update(updates)
        try:
            row = original(binary, args, output, cwd=cwd)
            write(output/'isolation.json', dict(profile=str(output/'profile'),
                  environment_overrides=updates, wrapper_sha256=sha(Path(__file__)),
                  affinity=sorted(os.sched_getaffinity(0))))
            # Preserve raw outcome even when native window validation later rejects it.
            ticks = row['result'].get('benchmark_measured_ticks', 0)
            row['measured_ticks'] = ticks
            if ticks > 0:
                row['process_cpu_per_tick_s'] = row['cpu_s']/ticks
                row['simulation_cpu_per_tick_s'] = row['result']['benchmark_run_cpu_ns']/1e9/ticks
                row['ticks_per_run_second'] = ticks/(row['result']['run_ns']/1e9)
            write(output/'native-measurement.json', row)
            return row
        finally:
            for key, value in previous.items():
                if value is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = value
    bench.execute = isolated
    inputs = job['identity']
    require(sha(Path(__file__)) == inputs['wrapper_sha256'], 'wrapper changed')
    require(sha(job['before']) == inputs['before_sha256'], 'baseline binary changed')
    require(sha(job['after']) == inputs['after_sha256'], 'candidate binary changed')
    require(sha(job['manifest']) == inputs['manifest_sha256'], 'manifest changed')
    require(sha(ROOT/'test/benchmark_resource_refactor.py') == inputs['benchmark_sha256'] and
            sha(ROOT/'test/benchmark_parallel_compute.py') == inputs['execute_sha256'], 'benchmark source changed')
    if job.get('resume_check'):
        prior = load(Path(job['output'])/'input-verification.json')
        manifest = load(job['manifest'])
        fixtures = {Path(p) for scenario in manifest['scenarios'] for p in scenario['fixture_sha256']}
        current = bench.capture_inputs({'baseline':Path(job['before']), 'candidate':Path(job['after'])},
                   {'baseline':Path(job['before_root']), 'candidate':Path(job['after_root'])},
                   Path(job['manifest']), fixtures)
        require(not bench.changed_inputs(prior['after'], current), 'resume binary/library/data/fixture/environment inputs changed')
        write(Path(job['output'])/'resume-verification.json',dict(inputs_unchanged=True,identity=inputs))
        return
    sys.argv = [str(ROOT/'test/benchmark_resource_refactor.py'), job['before'], job['after'],
                job['manifest'], '--output', job['output'], '--before-root', job['before_root'],
                '--after-root', job['after_root'], '--repeats', '3', '--report-only']
    rc = bench.main()
    require(rc == 0, 'benchmark execution/integrity failed')
    verified = load(Path(job['output'])/'input-verification.json')
    require(verified['state'] == 'verified' and verified['inputs_unchanged'], 'input verification incomplete')
    # Add the wrapper and job to the versioned tool's runner/binary/data/fixture audit.
    require(sha(Path(__file__)) == inputs['wrapper_sha256'] and sha(job['manifest']) == inputs['manifest_sha256'],
            'wrapper/manifest changed during execution')
    write(Path(job['output'])/'campaign-passed.json', dict(identity=inputs, passed=True))

def aggregate(jobs, directory):
    groups = {}
    for job in jobs:
        path = Path(job['output'])/'measurements.jsonl'
        if not (Path(job['output'])/'campaign-passed.json').exists():
            continue
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        pairs = {}
        for row in rows:
            if row['repeat'] >= 0:
                pairs.setdefault(row['repeat'], {})[row['variant']] = row
        target = groups.setdefault(job['factor']+'/'+job['scenario'], {})
        target[str(job['seed'])] = dict(samples={}, windows=sorted({r['result']['ticks'] for r in rows}))
        for metric in METRICS:
            logs = [math.log(p['candidate'][metric]/p['baseline'][metric]) for p in pairs.values()]
            target[str(job['seed'])]['samples'][metric] = dict(paired_log_ratios=logs,
                geometric_ratio=math.exp(statistics.mean(logs)))
        target[str(job['seed'])]['scope'] = 'Three paired repeats; warmup pair excluded; engine window vs process boundaries separate.'
    result = {}
    for key, seeds in groups.items():
        result[key] = dict(seeds=seeds, seed_count=len(seeds),
            metrics={metric: dict(equal_seed_geometric_ratio=math.exp(statistics.mean(
                     statistics.mean(s['samples'][metric]['paired_log_ratios']) for s in seeds.values())))
                     for metric in METRICS})
    write(directory/'per-layout-summary.json', dict(groups=result,
          caveats=['Two independent seeds per layout; repeat noise is not unmeasured gameplay/platform coverage.',
                   'New frozen saved-state cohort with canonical replay enabled; not the historical legacy workload.',
                   'Peak RSS includes startup/loading/teardown; CPU per tick uses equal validated fixed windows.',
                   'Four concurrent slots; each pair/repeat uses one fixed three-physical-core/six-SMT mask.']))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run', action='store_true')
    p.add_argument('--resume', action='store_true')
    p.add_argument('--worker', type=Path, help=argparse.SUPPRESS)
    p.add_argument('--scheduler-metadata', type=Path)
    p.add_argument('--correctness-name', default='correctness')
    args = p.parse_args()
    if args.worker:
        worker(args.worker)
        return
    acceptance = load(ART/'acceptance-plan.json')
    campaign = load(ART/'campaign.json')
    variants = load(ART/'variant-builds.json')
    binary = {'baseline': Path(campaign['baseline_binary']), 'combined': ROOT/'build/linux/client/release/src/glob2'}
    roots = {'baseline': Path(campaign['baseline_source']), 'combined': ROOT}
    for name, entry in variants['variants'].items():
        roots[name] = Path(entry['root'])
        binary[name] = roots[name]/'build/linux/client/release/src/glob2'
    if args.scheduler_metadata:
        entry = load(args.scheduler_metadata)
        require(entry['state'] == 'complete', 'scheduler not built')
        binary['scheduler'] = Path(entry['binary'])
        roots['scheduler'] = Path(entry['root'])
    factors = [('worker',3),('worker','auto'),('snapshot',3),('telemetry',3),('combined',3),('combined','auto')]
    if 'scheduler' in binary:
        factors += [('scheduler',3)]
    design = dict(layouts=10,seeds=2,main_factors=factors,main_gpu='off',repeats=3,
                  discarded_warmup_pairs=1,slots=MASKS,additional_gpuon='combined Playground/Oazis at3, both seeds',
                  launch_requires='all builds complete, all correctness fixtures passed, root confirms idle compilers and historical slots held')
    write(ART/'performance-plan.json',design)
    if not args.run:
        print(json.dumps(design,indent=2))
        return
    require(variants['state']=='complete','all variant builds must finish')
    require(sha(binary['baseline'])==campaign['baseline_binary_sha256'],'baseline changed')
    for name, entry in variants['variants'].items():
        require(sha(binary[name])==entry['binary_sha256'],f'{name} changed')
    directory = ART/'performance'
    jobs = []
    for scenario in acceptance['scenario_design']:
        for seed in acceptance['new_game_seeds']:
            key = f"{scenario['id']}-{seed}"
            fixture_root = ART/args.correctness_name/key
            fixture = load(fixture_root/'fixture.json')
            require(load(fixture_root/'passed.json')['passed'],'fixture correctness incomplete')
            path = Path(fixture['path'])
            require(sha(path)==fixture['header']['gzip_sha256'],'fixture changed')
            cases = [(factor,count,'off') for factor,count in factors]
            if scenario['id'] in ('playground-ni','oazis-ni'):
                cases.append(('combined',3,'on'))
            for factor,count,gpu in cases:
                factorid = f'{factor}-t{count}-gpu{gpu}'
                identifier = key+'-'+factorid
                manifest_path = directory/'manifests'/f'{identifier}.json'
                manifest = dict(scenarios=[dict(id=key,start_tick=fixture['start_tick'],
                    fixture_sha256={str(path):sha(path)}, args=['--load-game',str(path),'--ticks',str(scenario['ticks']),
                    '--compute-threads',str(count),'--gpu',gpu,'--write-replay'])])
                write(manifest_path,manifest)
                identity = dict(wrapper_sha256=sha(Path(__file__)),before_sha256=sha(binary['baseline']),
                    after_sha256=sha(binary[factor]),manifest_sha256=sha(manifest_path),
                    benchmark_sha256=sha(ROOT/'test/benchmark_resource_refactor.py'),
                    execute_sha256=sha(ROOT/'test/benchmark_parallel_compute.py'),repeats=3)
                jobs.append(dict(id=identifier,scenario=scenario['id'],seed=seed,factor=factorid,
                    before=str(binary['baseline']),after=str(binary[factor]),before_root=str(roots['baseline']),
                    after_root=str(roots[factor]),manifest=str(manifest_path),output=str(directory/'runs'/identifier),
                    identity=identity))
    write(directory/'worklist.json',jobs)
    for index,job in enumerate(jobs):
        job['mask']=MASKS[index%4]
        job['identity']['affinity']=job['mask']
        passed = Path(job['output'])/'campaign-passed.json'
        if args.resume and passed.exists():
            prior = load(passed)
            require(prior['identity']==job['identity'],'resume arguments/source identity changed')
            audit=load(Path(job['output'])/'input-verification.json')
            require(audit['state']=='verified' and audit['inputs_unchanged'],'prior audit incomplete')
            job['resume_check']=True
            continue
        require(not Path(job['output']).exists(),f'partial/existing job requires diagnosis: {job["id"]}')
    # Fixed-mask workers draw only jobs assigned to their own mask; no mask moves
    # occur between warmup/repeats/variants. Child interpreter is fresh per job.
    failed=threading.Event()
    errors=[]
    def lane(mask):
        for job in jobs:
            if job['mask']!=mask or failed.is_set():
                continue
            passed=Path(job['output'])/'campaign-passed.json'
            jobpath=directory/'jobs'/f'{job["id"]}.json'
            write(jobpath,job)
            logpath=directory/'jobs'/f'{job["id"]}.log'
            env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
            with logpath.open('w') as log:
                rc=subprocess.run([sys.executable,str(Path(__file__)), '--worker',str(jobpath)],
                                  env=env,stdout=log,stderr=subprocess.STDOUT).returncode
            if rc:
                errors.append(dict(job=job['id'],returncode=rc,log=str(logpath),
                              scope='execution/window/input failure; no aggregate acceptance'))
                failed.set()
    threads=[threading.Thread(target=lane,args=(mask,)) for mask in MASKS]
    for t in threads:t.start()
    for t in threads:t.join()
    aggregate(jobs,directory)
    write(directory/'status.json',dict(complete=not errors,errors=errors))
    require(not errors,'performance campaign failed; see status/logs; early winners require explicit paired window decision')

if __name__=='__main__':
    main()
