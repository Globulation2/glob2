"""Untimed exact per-tick game/world comparison for frozen CPU/GPU variants.

This intentionally enables expensive verification exports. Its measurements are
not inputs to performance acceptance. Native array, save/replay and lifecycle
harnesses remain separate required correctness gates.
"""
import argparse
import fcntl
import json
from pathlib import Path
import struct
from benchmark_gpu_offload import execute, validate, sha


def trace(directory, endpoint):
    world = (directory / 'world.checksums').read_bytes()
    rows = [tuple(map(int, line.split())) for line in world.splitlines()]
    if not rows or [r[0] for r in rows] != list(range(rows[0][0], endpoint)):
        raise ValueError('per-tick world checksum trace incomplete or discontinuous')
    sidecar = directory / 'game.replay.checksums'
    header = sidecar.read_bytes()[:20]
    if len(header) != 20 or header[:4] != b'GCS1' or struct.unpack('<I', header[12:16])[0] != len(rows):
        raise ValueError('sidecar completeness not established')
    return dict(world_sha256=sha(directory / 'world.checksums'),
                sidecar_sha256=sha(sidecar), ticks=len(rows), first_tick=rows[0][0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lock', type=Path, required=True)
    parser.add_argument('--continuation-ticks', type=int, default=0, help='untimed final-save reload with exact paired continuation')
    args = parser.parse_args()
    config = json.loads(args.config.read_text()); validate(config)
    if args.continuation_ticks < 0: raise ValueError('continuation ticks must be nonnegative')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    with args.lock.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        (output / 'metadata.json').write_text(json.dumps(dict(configuration=config,
            config_sha256=sha(args.config), verifier_sha256=sha(__file__), continuation_ticks=args.continuation_ticks,
            runner_sha256=sha(Path(__file__).with_name('benchmark_gpu_offload.py')), note='Untimed exactness verification only'), indent=2)+'\n')
        with (output / 'verification.jsonl').open('w') as stream:
            for scenario in config['scenarios']:
                saved = {}
                for stage in ('initial', 'continuation') if args.continuation_ticks else ('initial',):
                    expected = None
                    for variant in config['variants']:
                        directory = output / scenario['id'] / variant['id']
                        selected = scenario
                        extra = ('--telemetry', 'checksums')
                        if stage == 'initial' and args.continuation_ticks:
                            extra += ('--save', 'final')
                        if stage == 'continuation':
                            previous = saved[variant['id']]
                            fixture = previous['directory'] / 'final.game.gz'
                            if not fixture.is_file(): raise ValueError('continuation final save missing')
                            directory /= 'continuation'
                            selected = dict(scenario, args=['--load-game', str(fixture), '--ticks',
                                str(previous['result']['ticks'] + args.continuation_ticks)],
                                fixture_sha256={str(fixture): sha(fixture)})
                        row = dict(scenario=scenario['id'], variant=variant['id'], stage=stage,
                                   **execute(variant, selected, directory, 0, extra_args=extra, reservation=config.get('reservation')))
                        row['timing_eligible'] = False
                        row['fixture_sha256'] = selected.get('fixture_sha256', {})
                        try:
                            if not row['valid']: raise ValueError('; '.join(row['errors']))
                            if stage == 'continuation' and row['result']['initialChecksum'] != previous['result']['finalChecksum']:
                                raise ValueError('loaded initial checksum differs from saved final state')
                            row['trace'] = trace(directory, row['result']['ticks'])
                            signature = (row['result']['initialChecksum'], row['result']['finalChecksum'], row['trace'])
                            if expected is None: expected = signature
                            elif expected != signature: raise ValueError('exact per-tick CPU/GPU state divergence')
                        except Exception as error:
                            row['valid'] = False; row['errors'].append(str(error))
                        stream.write(json.dumps(row)+'\n'); stream.flush()
                        print(f"{scenario['id']} {variant['id']} {stage} exact={row['valid']}", flush=True)
                        if not row['valid']: raise RuntimeError('correctness gate failed; all evidence retained')
                        if stage == 'initial': saved[variant['id']] = dict(directory=directory, result=row['result'])



if __name__ == '__main__': main()
