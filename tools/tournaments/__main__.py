"""python3 -m tools.tournaments --help"""
import argparse
import json
from pathlib import Path
import platform
import sys

from .bundles import inspect_bundle, register_bundle
from .common import atomic_json, read_json, store_artifact
from .coordinator import Coordinator
from .model import validate_experiment
from .results import Results
from .transport import Transport, worker_archive


def main():
    parser = argparse.ArgumentParser(description='Durable Glob2 experiments, localhost and SSH')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan', help='validate/expand an experiment without executing work')
    p.add_argument('manifest'); p.add_argument('--output')
    p = sub.add_parser('bundle', help='register an immutable supplied binary/data directory')
    p.add_argument('source'); p.add_argument('destination'); p.add_argument('--executable', default='glob2')
    p.add_argument('--revision', required=True); p.add_argument('--dirty-identity'); p.add_argument('--options')
    p.add_argument('--platform'); p.add_argument('--capabilities')
    p = sub.add_parser('input', help='import an input into the portable results directory')
    p.add_argument('directory'); p.add_argument('file')
    p = sub.add_parser('submit')
    p.add_argument('manifest'); p.add_argument('directory'); p.add_argument('--bundle', action='append', required=True)
    for name in ('run', 'collect', 'doctor'):
        p = sub.add_parser(name)
        p.add_argument('directory'); p.add_argument('--hosts', required=True); p.add_argument('--once', action='store_true')
    for name in ('status', 'pause', 'resume', 'cancel', 'retry', 'cleanup', 'diagnose'):
        p = sub.add_parser(name); p.add_argument('directory')
        if name == 'pause': p.add_argument('--drain', action='store_true')
        if name == 'retry': p.add_argument('--job')
        if name == 'diagnose':
            p.add_argument('job'); p.add_argument('--output', required=True)
        if name == 'cleanup':
            p.add_argument('--reports', action='store_true'); p.add_argument('--transfers', action='store_true')
            p.add_argument('--worker-hosts'); p.add_argument('--objects', action='store_true'); p.add_argument('--bundles', action='store_true')
    args = parser.parse_args()
    try:
        if args.command == 'bundle':
            value = register_bundle(args.source, args.destination, args.executable, args.revision,
                                    read_json(args.options) if args.options else None, args.dirty_identity,
                                    read_json(args.platform) if args.platform else None,
                                    read_json(args.capabilities) if args.capabilities else None)
        elif args.command == 'input':
            value = store_artifact(args.file, Path(args.directory) / 'artifacts')
        elif args.command == 'plan':
            value = validate_experiment(read_json(args.manifest))
            if args.output: atomic_json(args.output, value)
        elif args.command == 'submit':
            coordinator = Coordinator.submit(args.directory, read_json(args.manifest), args.bundle)
            try: value = coordinator.status()
            finally: coordinator.close()
        else:
            coordinator = Coordinator(args.directory)
            try:
                if args.command == 'status': value = coordinator.status()
                elif args.command in ('run', 'collect'):
                    value = coordinator.run(read_json(args.hosts), args.once or args.command == 'collect', args.command == 'collect')
                elif args.command == 'doctor':
                    value = []
                    for config in read_json(args.hosts):
                        try:
                            transport = Transport(config, coordinator.root / 'worker.pyz')
                            value.append({'host': config['name'], **transport.rpc('status')})
                        except Exception as error:
                            value.append({'host': config['name'], 'error': str(error)})
                elif args.command in ('pause', 'resume', 'cancel'):
                    mode = 'draining' if args.command == 'pause' and args.drain else {'pause': 'paused', 'resume': 'running', 'cancel': 'cancelled'}[args.command]
                    value = coordinator.control(mode)
                elif args.command == 'retry': value = coordinator.retry(args.job)
                elif args.command == 'diagnose':
                    job = next(j for j in coordinator.manifest['jobs'] if j['id'] == args.job)
                    job = json.loads(json.dumps(job))
                    job['outputs'].update({'replay': job['type'] == 'game', 'core': True, 'stack': True})
                    if job['type'] == 'game':
                        job['outputs'].update({'saves': ['initial', 'final', 'every:2048'], 'telemetry': ['checksums', 'team-timeline', 'maxima']})
                    value = {k: v for k, v in coordinator.manifest.items() if k != 'package_id'}
                    value.update(id=value['id'] + '-diagnostic', jobs=[job])
                    # Resolve dependencies to retained artifacts; no generator rerun.
                    job['inputs'] = coordinator.resolve_inputs(job)
                    job['depends_on'] = []
                    atomic_json(args.output, value)
                else:
                    import shutil
                    value = {'removed': []}
                    if args.reports:
                        shutil.rmtree(coordinator.root / 'reports'); (coordinator.root / 'reports').mkdir()
                        value['removed'].append('reports')
                    if args.transfers:
                        if coordinator.status()['jobs'].get('active'):
                            raise ValueError('transfer cleanup requires no active attempts')
                        shutil.rmtree(coordinator.root / 'transfers'); (coordinator.root / 'transfers').mkdir()
                        value['removed'].append('transfers')
                    if args.worker_hosts:
                        for config in read_json(args.worker_hosts):
                            transport = Transport(config, coordinator.root / 'worker.pyz')
                            transport.rpc('cleanup', objects=args.objects, bundles=args.bundles)
                            value['removed'].append('worker:' + config['name'])
            finally:
                coordinator.close()
        print(json.dumps(value, indent=2, allow_nan=False))
    except (ValueError, OSError, StopIteration) as error:
        parser.exit(2, f'error: {error}\n')


if __name__ == '__main__':
    main()
