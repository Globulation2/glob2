# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded same-checkpoint branches and prospective release-binary comparisons.

These are validation experiments, never additional independent training maps.
CPU comparisons disable all probes; diagnostic repeats verify simulation parity.
"""
from __future__ import annotations
import csv
import gzip
import json
import os
from pathlib import Path
import statistics
import subprocess


def intervals(value):
    try:
        result = [int(v) for v in value.split(',')]
    except (ValueError, AttributeError):
        raise ValueError('Exploration intervals must be comma-separated integers') from None
    if not result or len(set(result)) != len(result) or any(v != 0 and not 8 <= v <= 65536 for v in result):
        raise ValueError('Intervals must be unique: 0 (adaptive) or 8..65536 ticks')
    return result


def _helpers():
    # Lazy import avoids a cycle with the main CLI module.
    from tools import resource_gradient_model as model
    return model


def _env():
    return {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_RESOURCE_')}


def _frozen(out, config):
    m = _helpers()
    path = out/'experiment.json'
    if path.exists() and json.loads(path.read_text()) != config:
        raise ValueError('Experiment inputs changed; choose a new output directory')
    if not path.exists() and any(out.iterdir()):
        raise ValueError('Existing experiment output has no frozen provenance')
    m.save(path, config)


def _execute(command, directory, timeout):
    """Resume only fully finished attempts with matching exact commands."""
    m = _helpers()
    directory.mkdir(parents=True, exist_ok=True)
    cmd_path = directory/'command.json'
    result = directory/'result.json'
    if result.exists() and not cmd_path.exists():
        raise ValueError(f'Completed result has no command provenance: {directory}')
    if cmd_path.exists() and json.loads(cmd_path.read_text()) != command:
        raise ValueError(f'Command changed: {directory}')
    m.save(cmd_path, command)
    result = directory/'result.json'
    if not result.exists():
        with (directory/'run.log').open('w') as log:
            subprocess.run(command, cwd=m.ROOT, env=_env(), stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=timeout)
    data = json.loads(result.read_text())
    if data.get('status') != 'completed':
        raise ValueError(f'Incomplete result: {result}')
    return data


def _quality(directory):
    report = {}
    for name, keys in [('gradient-quality.csv', ['flagged','extra_cost','false_unreachable','false_reachable']),
                       ('gradient-planning-quality.csv', ['false_unreachable','false_reachable'])]:
        path = directory/name
        if not path.exists() and path.with_suffix('.csv.gz').exists():
            path = path.with_suffix('.csv.gz')
        if not path.exists():
            raise ValueError(f'Missing diagnostic output: {path}')
        with gzip.open(path,'rt') if path.suffix == '.gz' else path.open() as stream:
            reader = csv.DictReader(stream)
            missing = set(keys)-set(reader.fieldnames or [])
            if missing:
                raise ValueError(f'Quality schema missing {sorted(missing)}: {path}')
            total, sums = 0, dict.fromkeys(keys, 0)
            for row in reader:
                total += 1
                for key in keys: sums[key] += int(row[key])
        report[name] = dict(samples=total, totals=sums,
                            mean={k: v/total if total else None for k,v in sums.items()})
    return report


def branch(args):
    """Compare a single demanded-field admission now versus after a delay.

    Every arm starts at the same saved world. Subsequent AI actions may diverge;
    the resulting horizon is a closed-loop consequence, not an isolated label.
    """
    m = _helpers()
    binary, checkpoint = Path(args.binary).resolve(), Path(args.checkpoint).resolve()
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=True)
    waits = sorted(set([0, *args.wait]))
    if args.field < 0 or not waits or min(waits) < 0 or max(waits) > 65536:
        raise ValueError('Field must be nonnegative and waits must be 0..65536')
    if args.ticks <= 0 or args.threads <= 0 or args.timeout_seconds <= 0:
        raise ValueError('Ticks, threads and timeout must be positive')
    _frozen(out, dict(schema=1,kind='checkpoint-branch',binary_sha256=m.sha(binary),
                      checkpoint_sha256=m.sha(checkpoint),field=args.field,waits=waits,
                      absolute_tick_limit=args.ticks,threads=args.threads))
    records = []
    for wait in waits:
        directory = out/f'wait-{wait}'
        command = [str(binary),'game','run','--load-game',str(checkpoint),'--ticks',str(args.ticks),
                   '--compute-threads',str(args.threads),'--gpu','off','--compute-telemetry','off',
                   '--output-dir',str(directory),'--resource-gradient-probe',
                   '--resource-gradient-fork-field',str(args.field),'--resource-gradient-fork-wait',str(wait)]
        result = _execute(command, directory, args.timeout_seconds)
        decision_path=directory/'gradient-decisions.csv'
        with decision_path.open() as stream:
            executed=[r for r in csv.DictReader(stream) if r['reason']=='checkpoint_fork'
                      and int(r['key'])==args.field and int(r['admitted'])==1]
        if len(executed)!=1:
            raise ValueError(f'Expected exactly one executed field intervention: {directory}; got {len(executed)}')
        records.append(dict(wait=wait,executed_intervention=executed[0],result=result,quality=_quality(directory),
                            result_sha256=m.sha(directory/'result.json')))
        m.save(out/'report.json',dict(schema=1,arms=records,
               note='Probe CPU is excluded from performance claims. Branches share initial state; later AI decisions can diverge.'))
    print(json.dumps(dict(output=str(out),arms=len(records)),indent=2))


def compare(args):
    """Matched saved-state benchmarks with repeated alternating execution order."""
    m = _helpers()
    out = Path(args.output).resolve(); out.mkdir(parents=True, exist_ok=True)
    binaries = {k:Path(getattr(args,k)).resolve() for k in ['incumbent','candidate']}
    checkpoints = [Path(p).resolve() for p in args.checkpoint]
    if len(set(checkpoints)) != len(checkpoints): raise ValueError('Duplicate checkpoints')
    if min(args.ticks,args.threads,args.repeats,args.timeout_seconds) <= 0 or args.warmup < 0:
        raise ValueError('Positive ticks, threads, repeats and timeout required; warmup must be nonnegative')
    config = dict(schema=1,kind='prospective-comparison',
                  binaries={k:dict(path=str(p),sha256=m.sha(p)) for k,p in binaries.items()},
                  checkpoints=[dict(path=str(p),sha256=m.sha(p)) for p in checkpoints],
                  absolute_tick_limit=args.ticks,warmup=args.warmup,repeats=args.repeats,
                  threads=args.threads,quality=args.quality)
    _frozen(out,config)
    records=[]; pairs=[]
    for index, checkpoint in enumerate(checkpoints):
        for repeat in range(args.repeats):
            order = ['incumbent','candidate'] if (index+repeat)%2 == 0 else ['candidate','incumbent']
            pair={}
            for arm in order:
                directory=out/f'map-{index:03d}'/f'repeat-{repeat:02d}'/arm
                command=[str(binaries[arm]),'game','run','--load-game',str(checkpoint),
                         '--ticks',str(args.ticks),'--compute-threads',str(args.threads),
                         '--gpu','off','--compute-telemetry','off','--benchmark-warmup',str(args.warmup),
                         '--output-dir',str(directory)]
                result=_execute(command,directory,args.timeout_seconds)
                measured=result['benchmark_measured_ticks']
                if measured <= 0: raise ValueError('No measured advances')
                cpu=result['benchmark_run_cpu_ns']; wall=result['run_ns']
                row=dict(checkpoint=index,repeat=repeat,arm=arm,order=order.index(arm),
                         game_ticks=result['ticks'],measured_ticks=measured,cpu_ns=cpu,
                         cpu_ns_per_tick=cpu/measured,run_wall_ns=wall,
                         # run_ns includes warmup: name the denominator explicitly.
                         measured_ticks_per_run_wall_second=measured/(wall/1e9),
                         final_checksum=result['finalChecksum'],result_sha256=m.sha(directory/'result.json'))
                previous=[r for r in records if r['checkpoint']==index and r['arm']==arm]
                if any(r['final_checksum']!=row['final_checksum'] or r['game_ticks']!=row['game_ticks'] for r in previous):
                    raise ValueError(f'Repeated simulation differs: map {index}, {arm}')
                pair[arm]=row;records.append(row)
            pairs.append(dict(checkpoint=index,repeat=repeat,
                              cpu_per_tick_ratio=pair['candidate']['cpu_ns_per_tick']/pair['incumbent']['cpu_ns_per_tick']))
            m.save(out/'timings.json',dict(config=config,records=records,pairs=pairs))
    quality=[]
    if args.quality:
        # A separate repeat keeps oracle rebuilds out of CPU measurements.
        for index,checkpoint in enumerate(checkpoints):
            for arm in binaries:
                directory=out/f'map-{index:03d}'/'quality'/arm
                command=[str(binaries[arm]),'game','run','--load-game',str(checkpoint),
                         '--ticks',str(args.ticks),'--compute-threads',str(args.threads),
                         '--gpu','off','--compute-telemetry','off','--resource-gradient-probe',
                         '--output-dir',str(directory)]
                result=_execute(command,directory,args.timeout_seconds)
                reference=[r for r in records if r['checkpoint']==index and r['arm']==arm]
                if any(r['final_checksum']!=result['finalChecksum'] or r['game_ticks']!=result['ticks'] for r in reference):
                    raise ValueError(f'Observer/repeat simulation equivalence failed: map {index}, {arm}')
                quality.append(dict(checkpoint=index,arm=arm,quality=_quality(directory)))
    report=dict(schema=1,config=config,records=records,pairs=pairs,quality=quality,
                median_paired_cpu_per_tick_ratio=statistics.median(p['cpu_per_tick_ratio'] for p in pairs),
                worst_paired_cpu_per_tick_ratio=max(p['cpu_per_tick_ratio'] for p in pairs),
                note='Release builds required. Different policies may end at different ticks. No automatic promotion; inspect per-map tails and planning/movement quality.')
    m.save(out/'report.json',report)
    print(json.dumps({k:v for k,v in report.items() if k not in ['config','records','pairs','quality']},indent=2))


def add_commands(sub):
    p=sub.add_parser('branch',help='Same-checkpoint single-field refresh-now versus wait experiment')
    p.add_argument('--binary',required=True);p.add_argument('--checkpoint',required=True)
    p.add_argument('--field',type=int,required=True);p.add_argument('--wait',type=int,nargs='+',default=[32,128,512])
    p.add_argument('--ticks',type=int,required=True,help='Absolute final tick, greater than checkpoint tick')
    p.add_argument('--output',required=True);p.add_argument('--threads',type=int,default=4)
    p.add_argument('--timeout-seconds',type=int,default=7200);p.set_defaults(action=branch)
    p=sub.add_parser('compare',help='Prospective matched release-binary comparison; no model fitting')
    p.add_argument('--incumbent',required=True);p.add_argument('--candidate',required=True)
    p.add_argument('--checkpoint',action='append',required=True);p.add_argument('--ticks',type=int,required=True)
    p.add_argument('--output',required=True);p.add_argument('--warmup',type=int,default=256)
    p.add_argument('--threads',type=int,default=4);p.add_argument('--repeats',type=int,default=2)
    p.add_argument('--timeout-seconds',type=int,default=7200);p.add_argument('--quality',action='store_true')
    p.set_defaults(action=compare)
