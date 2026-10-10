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
    args = parser.parse_args()
    config = json.loads(args.config.read_text()); validate(config)
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    args.lock.parent.mkdir(parents=True, exist_ok=True)
    with args.lock.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        (output / 'metadata.json').write_text(json.dumps(dict(configuration=config,
            config_sha256=sha(args.config), note='Untimed exactness verification only'), indent=2)+'\n')
        with (output / 'verification.jsonl').open('w') as stream:
            for scenario in config['scenarios']:
                expected = None
                for variant in config['variants']:
                    directory = output / scenario['id'] / variant['id']
                    row = dict(scenario=scenario['id'], variant=variant['id'],
                               **execute(variant, scenario, directory, 0, extra_args=('--telemetry', 'checksums')))
                    row['timing_eligible'] = False
                    try:
                        if not row['valid']: raise ValueError('; '.join(row['errors']))
                        row['trace'] = trace(directory, row['result']['ticks'])
                        signature = (row['result']['initialChecksum'], row['result']['finalChecksum'], row['trace'])
                        if expected is None: expected = signature
                        elif expected != signature: raise ValueError('exact per-tick CPU/GPU state divergence')
                    except Exception as error:
                        row['valid'] = False; row['errors'].append(str(error))
                    stream.write(json.dumps(row)+'\n'); stream.flush()
                    print(f"{scenario['id']} {variant['id']} exact={row['valid']}", flush=True)
                    if not row['valid']: raise RuntimeError('correctness gate failed; all evidence retained')


if __name__ == '__main__': main()
