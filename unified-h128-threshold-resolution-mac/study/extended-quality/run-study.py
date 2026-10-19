#!/usr/bin/env python3
"""One frozen, serial Mac threshold experiment; never training ingestion."""
import datetime, fcntl, hashlib, json, os, pathlib, signal, subprocess, time, traceback

D = pathlib.Path(__file__).resolve().parent
P = json.loads((D / 'PLAN.json').read_text())
R = pathlib.Path(P['runtime'])
lock = (D / 'study.lock').open('a')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

def stamp(): return datetime.datetime.now(datetime.timezone.utc).isoformat()
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def atomic(p, value):
    t = p.with_suffix(p.suffix + '.tmp')
    t.write_text(json.dumps(value, sort_keys=True) + '\n')
    t.replace(p)

S = dict(status='WAITING-QUALIFIED-RUNTIME', pid=os.getpid(), started_utc=stamp(),
         plan_sha256=sha(D / 'PLAN.json'), finished_runs=0, rows=[], no_promotion=True)
def save(**kw):
    S.update(kw, updated_utc=stamp())
    atomic(D / 'STATUS.json', S)

def context():
    procs = subprocess.check_output(['ps', '-axo', 'pid,ppid,pcpu,command'], text=True)
    engines = [l for l in procs.splitlines() if '/bin/glob2 game run ' in l]
    v = dict(utc=stamp(), loadavg=list(os.getloadavg()), engines=engines)
    for key, cmd in [('swap', ['sysctl', '-n', 'vm.swapusage']),
                     ('power_thermal', ['pmset', '-g', 'therm'])]:
        r = subprocess.run(cmd, capture_output=True, text=True)
        v[key] = r.stdout.strip()
    return v

def run(m, a, kind, repeat):
    before = context()
    # Collection is forbidden on Mac during this experiment. Also serialize
    # experiments; CPU measurements must not overlap another game or build.
    assert not before['engines'], 'Another Mac game is running; do not compete'
    name = f'{kind}-r{repeat}-{m["group"]}-{a["id"]}'
    out = D / 'runs' / name
    out.mkdir(parents=True, exist_ok=False)
    warm = pathlib.Path(m['warm_save_path'])
    assert sha(warm) == m['warm_save_sha256']
    extra = P['off_additional_ticks'] if kind == 'off' else P['on_additional_ticks']
    argv = [str(R / 'bin/glob2'), 'game', 'run', '--load-game', str(warm),
            '--ticks', str(m['warm_tick'] + extra), '--output-dir', str(out),
            '--profile', str(out / 'profile'), '--benchmark-warmup', '0',
            '--gpu', 'off', '--compute-threads', '1', '--compute-telemetry', 'off',
            '--data-dir', str(R / 'data')]
    if 'fixed_interval' in a:
        argv += ['--resource-gradient-exploration', str(a['fixed_interval'])]
    if kind == 'on':
        argv += ['--resource-gradient-probe', '--gradient-work-stats',
                 str(out / 'gradient-work.csv')]
    env = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_')}
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI=str(a['threshold_milli']))
    atomic(out / 'COMMAND.json', dict(argv=argv, cwd=str(R), environment={
        k: env[k] for k in ['SDL_VIDEODRIVER', 'SDL_AUDIODRIVER',
                            'GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI']},
        context_before=before, binary_sha256=S['binary_sha256'],
        plan_sha256=S['plan_sha256'], gate_sha256=S['gate_sha256'],
        warm_save_sha256=m['warm_save_sha256'], wall_cap_seconds=1800))
    start = time.monotonic()
    capped = False
    with (out / 'stdout.log').open('w') as so, (out / 'stderr.log').open('w') as se:
        child = subprocess.Popen(argv, cwd=R, env=env, stdout=so, stderr=se,
                                 start_new_session=True)
        save(status='RUNNING', active=dict(name=name, pid=child.pid,
                                          started_utc=stamp(), wall_cap_seconds=1800))
        while True:
            done, status, usage = os.wait4(child.pid, os.WNOHANG)
            if done: break
            if time.monotonic() - start >= 1800:
                capped = True
                os.killpg(child.pid, signal.SIGKILL)
                done, status, usage = os.wait4(child.pid, 0)
                break
            time.sleep(.1)
        child.returncode = os.waitstatus_to_exitcode(status)
    wall = time.monotonic() - start
    result = json.loads((out / 'result.json').read_text()) if (out / 'result.json').exists() else None
    row = dict(name=name, arm=a['id'], group=m['group'], format=m['format'],
               kind=kind, repeat=repeat, attempt=1, returncode=child.returncode,
               capped=capped, wall_seconds=wall, start_tick=m['warm_tick'],
               threshold_milli=a['threshold_milli'], fixed_interval=a.get('fixed_interval'),
               user_seconds=usage.ru_utime, system_seconds=usage.ru_stime,
               total_cpu_seconds=usage.ru_utime + usage.ru_stime,
               maximum_rss_bytes=usage.ru_maxrss, context_after=context(),
               requested_additional_ticks=extra, output=str(out))
    if result is not None:
        atomic(out / 'RESULT-RAW.json', result)
        delta = result['ticks'] - m['warm_tick']
        row.update(end_tick=result['ticks'], actual_ticks=delta,
                   final_checksum=result['finalChecksum'], initial_checksum=result['initialChecksum'],
                   termination=result['termination'], policy=result['adaptive_gradient_policy'],
                   full_horizon=delta == extra and result['termination'] == 'tick_cap',
                   benchmark_ticks=result['benchmark_measured_ticks'],
                   benchmark_cpu_ns=result['benchmark_run_cpu_ns'])
        assert result['benchmark_measured_ticks'] == delta
        assert row['policy']['threshold_milli_direction_cost'] == a['threshold_milli']
        side = out / 'research-gradient-threshold-input.json'
        assert row['policy']['research_input_sha256'] == sha(side)
        row['research_input_sha256'] = sha(side)
        if delta > 0:
            row.update(cpu_ms_per_tick=result['benchmark_run_cpu_ns'] / 1e6 / delta,
                       process_cpu_ms_per_tick=row['total_cpu_seconds'] * 1000 / delta,
                       wall_tps=delta / wall)
        for key in ['gradient_jobs', 'gradient_published', 'gradient_discarded',
                    'building_gradient_jobs', 'building_gradient_synchronous']:
            row[key] = result.get(key)
    row['artifact_hashes'] = {p.name: sha(p) for p in out.iterdir() if p.is_file()
                             and p.name not in ['progress.jsonl', 'stdout.log', 'stderr.log']}
    atomic(out / 'MEASUREMENT.json', row)
    S.pop('active', None)
    S['rows'].append(row)
    save(finished_runs=len(S['rows']))
    if child.returncode != 0 or capped or result is None:
        raise RuntimeError('Run failed/capped: preserved, no automatic retry: ' + name)

try:
    assert not (D / 'LAUNCH-INTENT.json').exists(), 'No implicit resume or duplicate launch'
    save()
    gatepath = pathlib.Path(P['required_gate'])
    deadline = time.monotonic() + 1800
    while not gatepath.exists():
        if time.monotonic() > deadline:
            raise RuntimeError('No qualified runtime gate within30min; inspect build')
        time.sleep(5)
    gate = json.loads(gatepath.read_text())
    assert gate['status'] == 'PASS' and gate['research_only']
    for k in ['source_base', 'model_sha256', 'header_sha256']:
        assert gate[k] == P[k], k
    assert gate['binary_sha256'] == sha(R / 'bin/glob2')
    assert gate['checks'] and all(gate['checks'].values())
    for m in P['maps']: assert sha(m['warm_save_path']) == m['warm_save_sha256']
    atomic(D / 'GATE-SNAPSHOT.json', gate)
    save(gate_sha256=sha(gatepath), binary_sha256=gate['binary_sha256'])
    atomic(D / 'LAUNCH-INTENT.json', dict(utc=stamp(), pid=os.getpid(),
           plan_sha256=S['plan_sha256'], gate_sha256=S['gate_sha256'], one_attempt=True))
    byid = {a['id']: a for a in P['arms']}
    for m in P['maps']:
        for rep, order in enumerate(m['off_repeat_orders']):
            for arm in order: run(m, byid[arm], 'off', rep)
    for m in P['maps']:
        for arm in m['diagnostic_order']: run(m, byid[arm], 'on', 0)
    parent = json.loads((D.parent / 'STATUS.json').read_text())
    comparisons = []
    for r in S['rows']:
        ref = next(x for x in parent['rows'] if x['kind']=='off' and x['group']==r['group'] and x['arm']==r['arm'])
        same = r['final_checksum']==ref['final_checksum'] and r['initial_checksum']==ref['initial_checksum']
        comparisons.append(dict(group=r['group'],arm=r['arm'],same=same,initial_checksum=r['initial_checksum'],final_checksum=r['final_checksum'],reference_off_run=ref['name']))
        assert same, 'Extended observer changed simulation'
    atomic(D / 'MATCHED-OFF-ON-FINAL-CHECKSUMS.json',dict(status='PASS',checks=comparisons))
    save(status='COMPLETE', finished_utc=stamp(), actual_runs=len(S['rows']),
         no_promotion=True, no_training_ingestion=True)
except Exception:
    save(status='FAILED-REQUIRES-REVIEW', error=traceback.format_exc())
    raise
