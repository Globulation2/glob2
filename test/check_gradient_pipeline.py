"""Generate a retained fixture and check fixed-delay thread-count equivalence.

A second pass forks the fixture to building gradient delays 1, 4 and 8:
identical traces and building counters across worker counts, save/resume
continuation at every pending phase, identical traces for every worker depth
mode, and rejection of out-of-range delays."""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from benchmark_parallel_compute import execute, digest
from check_telemetry_simulation import detailed_ticks

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--skip-building', action='store_true', help='omit the scheduled building gradient pass')
    a = p.parse_args()
    binary = a.binary.resolve()
    temporary = tempfile.TemporaryDirectory(prefix='glob2-pipeline-') if not a.output else None
    output = Path(temporary.name) if temporary else a.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output/'fixture'
    execute(binary, ['--generator','26','--map-seed','4242','--game-seed','19',
                     '--set','width=7','--set','height=7','--set','teams=4','--set','pattern=1',
                     '--player','maxima','--player','cortex','--player','nicowar','--player','maxima',
                     '--ticks','1','--save','initial'], fixture)
    initial = fixture/'initial.game'
    manifest = output/'manifest.json'
    manifest.write_text(json.dumps({'scenarios':[{'id':'mixed-water','args':['--load-game',str(initial),'--ticks','1024'],
        'fixture_sha256':{str(initial):digest(initial)}}]}))
    subprocess.run([sys.executable,str(Path(__file__).with_name('benchmark_gradient_pipeline.py')),str(binary),str(manifest),
                    '--output',str(output/'verification'),'--workers','0','1','2','4','8','--delays','1','3','8','--verify'],check=True)
    default = execute(binary, ['--load-game',str(initial),'--ticks','32'], output/'default')
    assert default['result']['resolved']['rules']['aiOrderDelay'] == 8
    assert default['result']['gradient_delay'] == 8
    assert default['result']['gradient_workers'] == default['result']['compute_threads'] - 1
    explicit = execute(binary, ['--load-game',str(initial),'--ticks','32',
        '--compute-threads','2'], output/'explicit-shared-size')
    assert explicit['result']['compute_threads'] in (1, 2)  # Threadless fallback is supported.
    assert explicit['result']['gradient_workers'] == explicit['result']['compute_threads'] - 1
    # Save with work pending at every offset of the eight-tick pipeline. Compare
    # resumed tick/entity traces against the uninterrupted run, across worker counts.
    whole = output/'whole'
    execute(binary, ['--load-game',str(initial),'--ticks','80','--telemetry','checksums'], whole)
    expected = detailed_ticks((whole/'game.replay.checksums').read_bytes())
    for phase in range(8):
        checkpoint = output/f'checkpoint-{phase}'
        execute(binary, ['--load-game',str(initial),'--ticks',str(32+phase),'--save','final'], checkpoint)
        for workers in (0,1,2):
            dest = output/f'resumed-{phase}-{workers}'
            execute(binary, ['--load-game',str(checkpoint/'final.game'),'--ticks','80',
                             '--compute-threads',str(workers + 1),'--telemetry','checksums','--write-replay'], dest)
            ticks = detailed_ticks((dest/'game.replay.checksums').read_bytes())
            assert ticks and all(expected[t] == value for t,value in ticks.items()), (phase, workers)
    cases = [['--compute-threads','4294967296'],['--compute-threads','1','--gradient-delay','0'],
             ['--load-game',str(output/'checkpoint-0/final.game'),'--gradient-delay','3']]
    for index, args in enumerate(cases):
        dest = output/f'rejection-{index}'
        run = subprocess.run([str(binary),'game', 'run','--output-dir',str(dest),*args],capture_output=True,text=True)
        (output/f'rejection-{index}.log').write_text(run.stdout+run.stderr)
        assert run.returncode != 0, args
        assert not list(dest.glob('*.game*')) and not list(dest.glob('*.replay')), args
    print('PASS pipeline defaults, trace determinism, eight-phase save continuation and invalid option rejection')
    if not a.skip_building:
        building_pass(binary, initial, output/'building')


BUILDING_DELAYS = (1, 4, 8)
# Late enough that refreshes are frequent (about one per 17 ticks), so a save
# can catch fields in flight at every phase, even at delay 1.
SAVE_TICK, RESUME_TICKS = 2048, 2400


def fork(delay):
    return ['--fork-rule', f'buildingGradientDelay={delay}']


def building_pass(binary, initial, output):
    output.mkdir(parents=True, exist_ok=True)
    manifest = output/'manifest.json'
    manifest.write_text(json.dumps({'scenarios': [
        {'id': f'building-d{d}', 'args': ['--load-game', str(initial), *fork(d), '--ticks', '1024'],
         'fixture_sha256': {str(initial): digest(initial)}} for d in BUILDING_DELAYS]}))
    subprocess.run([sys.executable, str(Path(__file__).with_name('benchmark_gradient_pipeline.py')), str(binary), str(manifest),
                    '--output', str(output/'verification'), '--workers', '0', '1', '2', '4', '8', '--delays', '8', '--verify'], check=True)
    rows = [json.loads(line) for line in (output/'verification'/'measurements.jsonl').read_text().splitlines()]
    for row in rows:
        result = row['result']
        assert result['resolved']['fork'] == ['rule:buildingGradientDelay=' + row['scenario'].split('-d')[1]], row['variant']
        assert result['building_gradient_jobs'] > 0 and result['building_gradient_published'] > 0, (row['scenario'], row['variant'])
    # Save with scheduled fields pending at every phase of each delay; the resumed
    # trace must equal the uninterrupted fork's across worker counts.
    pending_saves = []
    for delay in BUILDING_DELAYS:
        whole = output/f'whole-d{delay}'
        execute(binary, ['--load-game', str(initial), *fork(delay), '--ticks', str(RESUME_TICKS), '--telemetry', 'checksums'], whole)
        expected = detailed_ticks((whole/'game.replay.checksums').read_bytes())
        for phase in range(delay):
            # Step the save by whole delays until a job is in flight; a young
            # colony requests few refreshes, so a fixed tick may catch none.
            for tick in range(SAVE_TICK+phase, RESUME_TICKS-delay, delay):
                checkpoint = output/f'checkpoint-d{delay}-{phase}-t{tick}'
                saved = execute(binary, ['--load-game', str(initial), *fork(delay), '--ticks', str(tick), '--save', 'final'], checkpoint)
                if saved['result']['building_gradient_pending'] > 0:
                    pending_saves.append((delay, checkpoint/'final.game'))
                    break
            for workers in (0, 1, 2):
                dest = output/f'resumed-d{delay}-{phase}-{workers}'
                resumed = execute(binary, ['--load-game', str(checkpoint/'final.game'), '--ticks', str(RESUME_TICKS),
                                           '--compute-threads', str(workers + 1), '--telemetry', 'checksums'], dest)
                assert resumed['result']['resolved']['rules']['buildingGradientDelay'] == delay
                assert resumed['result']['resolved']['fork'] == []
                ticks = detailed_ticks((dest/'game.replay.checksums').read_bytes())
                assert ticks and all(expected[t] == value for t, value in ticks.items()), (delay, phase, workers)
    assert len(pending_saves) == sum(BUILDING_DELAYS), 'saves must catch scheduled building fields in flight at every phase'
    # The worker depth moves CPU between worker and owner, never results, at
    # every operating point of the model.
    points = [p['name'] for p in json.loads((ROOT/'tools/gradient_depth_model.json').read_text())['points']]
    traces = {}
    for mode in ('table', 'full', 'lazy', *points):
        os.environ['GLOB2_BUILDING_DEPTH'] = mode
        try:
            dest = output/f'depth-{mode}'
            execute(binary, ['--load-game', str(initial), *fork(4), '--ticks', '1024', '--compute-threads', '3',
                             '--telemetry', 'checksums'], dest)
        finally:
            del os.environ['GLOB2_BUILDING_DEPTH']
        traces[mode] = (dest/'game.replay.checksums').read_bytes()
    assert len(set(traces.values())) == 1, 'building depth changed the simulation'
    generated = ['--generator', '26', '--map-seed', '4242', '--game-seed', '19', '--set', 'width=7', '--set', 'height=7',
                 '--set', 'teams=4', '--set', 'pattern=1', '--player', 'maxima', '--player', 'cortex', '--player', 'nicowar',
                 '--player', 'maxima', '--ticks', '2']
    cases = [[*generated, '--rule', f'buildingGradientDelay={d}'] for d in (0, 9)]
    cases += [['--load-game', str(initial), *fork(d), '--ticks', '2'] for d in (0, 9)]
    cases += [['--load-game', str(initial), '--rule', 'buildingGradientDelay=4', '--ticks', '2'],
              ['--load-game', str(initial), '--fork-rule', 'aiOrderDelay=1', '--ticks', '2'],
              [*generated, '--fork-rule', 'buildingGradientDelay=4']]
    # A save with fields in flight cannot be forked to another delay.
    delay, pending = pending_saves[0]
    cases.append(['--load-game', str(pending), '--fork-rule', f'buildingGradientDelay={9-delay}', '--ticks', str(RESUME_TICKS)])
    for index, args in enumerate(cases):
        dest = output/f'rejection-{index}'
        run = subprocess.run([str(binary), 'game', 'run', '--output-dir', str(dest), *args], capture_output=True, text=True)
        (output/f'rejection-{index}.log').write_text(run.stdout+run.stderr)
        assert run.returncode != 0, args
        assert not list(dest.glob('*.game*')) and not list(dest.glob('*.replay')), args
    print(f'PASS building gradient delays {BUILDING_DELAYS}: worker-count traces, {len(pending_saves)} in-flight saves resumed, delay 0/9 rejected')


if __name__ == '__main__':
    main()
