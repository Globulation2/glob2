"""Generate a retained fixture and check fixed-delay thread-count equivalence."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from benchmark_parallel_compute import execute, digest
from check_telemetry_simulation import detailed_ticks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    binary = a.binary.resolve()
    temporary = tempfile.TemporaryDirectory(prefix='glob2-pipeline-') if not a.output else None
    output = Path(temporary.name) if temporary else a.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = output/'fixture'
    execute(binary, ['--generator','26','--map-seed','4242','--game-seed','19',
                     '--param','width=7','--param','height=7','--param','teams=4','--param','pattern=1',
                     '--player','maxima','--player','cortex','--player','nicowar','--player','maxima',
                     '--ticks','1','--save','initial'], fixture)
    initial = fixture/'initial.game'
    manifest = output/'manifest.json'
    manifest.write_text(json.dumps({'scenarios':[{'id':'mixed-water','args':['--load-game',str(initial),'--ticks','1024'],
        'fixture_sha256':{str(initial):digest(initial)}}]}))
    subprocess.run([sys.executable,str(Path(__file__).with_name('benchmark_gradient_pipeline.py')),str(binary),str(manifest),
                    '--output',str(output/'verification'),'--workers','0','1','2','4','8','--delays','1','3','8','--verify'],check=True)
    default = execute(binary, ['--load-game',str(initial),'--ticks','32','--gradient-workers','1'], output/'default')
    assert default['result']['gradient_delay'] == 8
    assert default['result']['gradient_workers'] == 1
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
                             '--gradient-workers',str(workers),'--telemetry','checksums','--replay','true'], dest)
            ticks = detailed_ticks((dest/'game.replay.checksums').read_bytes())
            assert ticks and all(expected[t] == value for t,value in ticks.items()), (phase, workers)
    cases = [['--gradient-workers','17'],['--gradient-workers','0','--gradient-delay','0'],
             ['--load-game',str(output/'checkpoint-0/final.game'),'--gradient-delay','3']]
    for index, args in enumerate(cases):
        dest = output/f'rejection-{index}'
        run = subprocess.run([str(binary),'--run-game','--output-dir',str(dest),*args],capture_output=True,text=True)
        (output/f'rejection-{index}.log').write_text(run.stdout+run.stderr)
        assert run.returncode != 0, args
        assert not list(dest.glob('*.game*')) and not list(dest.glob('*.replay')), args
    print('PASS pipeline defaults, trace determinism, eight-phase save continuation and invalid option rejection')


if __name__ == '__main__':
    main()
