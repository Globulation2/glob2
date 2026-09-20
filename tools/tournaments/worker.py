"""Persistent worker, short-lived RPC endpoint, and isolated attempt supervisors."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import traceback
import uuid
import zipfile

from .bundles import inspect_bundle, package_identity, platform_identity
from .common import (atomic_json, canonical, copy_decoded, database, digest, file_hash,
                     hash_id, identifier, inside, lock, locked, read_json, store_artifact, transaction)
from .jobs import get_job_type
from .diagnostics import postmortem
from .model import PROTOCOL_VERSION, validate_job
from .transfer import get_chunk, offset, put_chunk

HOST_DEFAULTS = {'slots': max(1, (os.cpu_count() or 1) - 1), 'collect_slots': 1,
                 'disk_reserve_bytes': 1024**3, 'spool_budget_bytes': 10 * 1024**3,
                 'cache_budget_bytes': 20 * 1024**3, 'builds': [], 'memory_mb': None}


def launcher():
    if sys.argv[0].endswith('.pyz'):
        return [sys.executable, str(Path(sys.argv[0]).resolve())]
    return [sys.executable, '-m', 'tools.tournaments.worker']


def usage(directory):
    total = 0
    for p in Path(directory).rglob('*'):
        try:
            if p.is_file() and not p.is_symlink(): total += p.stat().st_size
        except FileNotFoundError:
            pass  # Concurrent acknowledgement/atomic publication.
    return total


class Worker:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True)
        for folder in ('attempts', 'objects', 'bundles', 'spool'):
            (self.root / folder).mkdir(exist_ok=True)
        self.db = database(self.root / 'queue.sqlite')
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS queue (
                id TEXT PRIMARY KEY, token TEXT NOT NULL, experiment TEXT NOT NULL,
                job TEXT NOT NULL, state TEXT NOT NULL, pid INTEGER,
                started REAL, updated REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS controls (experiment TEXT PRIMARY KEY, mode TEXT NOT NULL);
        ''')
        path = self.root / 'host.json'
        self._host_config_path = path
        self._host_config_mtime = path.stat().st_mtime if path.exists() else None
        self.config = HOST_DEFAULTS | (read_json(path) if path.exists() else {})

    def close(self):
        self.db.close()

    def reload_config(self):
        """Pick up host.json edits (e.g. from `configure`) without a daemon restart.

        A persistent daemon's Worker instance is constructed once at startup and
        otherwise never re-reads host.json; a `configure` RPC updates the file via
        a separate, short-lived Worker instance for that single call. Without this,
        slot/build/budget changes silently have no effect until the daemon happens
        to be restarted for an unrelated reason.
        """
        path = self._host_config_path
        mtime = path.stat().st_mtime if path.exists() else None
        if mtime != self._host_config_mtime:
            self._host_config_mtime = mtime
            self.config = HOST_DEFAULTS | (read_json(path) if path.exists() else {})

    def configure(self, values):
        unknown = set(values) - set(HOST_DEFAULTS)
        if unknown:
            raise ValueError('unknown host fields: ' + ','.join(sorted(unknown)))
        config = HOST_DEFAULTS | values
        for key in ('slots', 'collect_slots', 'spool_budget_bytes', 'cache_budget_bytes'):
            if type(config[key]) is not int or config[key] < 1:
                raise ValueError(key + ' must be a positive integer')
        if config['disk_reserve_bytes'] < 0:
            raise ValueError('disk reserve must be nonnegative')
        self.config = config
        atomic_json(self.root / 'host.json', config)
        return self.status()

    def status(self):
        counts = dict(self.db.execute('SELECT state,count(*) FROM queue GROUP BY state').fetchall())
        spool = usage(self.root / 'attempts') + usage(self.root / 'spool')
        free = shutil.disk_usage(self.root).free
        pending = [dict(row) for row in self.db.execute(
            "SELECT id,token,experiment,state,started FROM queue WHERE state NOT IN ('acknowledged','cancelled') ORDER BY updated")]
        return {'protocol_version': PROTOCOL_VERSION, 'package_id': package_identity(),
                'platform': platform_identity(), 'slots': self.config['slots'], 'builds': self.config['builds'],
                'counts': counts, 'attempts': pending, 'free_bytes': free, 'spool_bytes': spool, 'cache_bytes': usage(self.root / 'objects') + usage(self.root / 'bundles'),
                'accepting': free > self.config['disk_reserve_bytes'] and spool < self.config['spool_budget_bytes'],
                'daemon_running': locked(self.root / 'daemon.lock'),
                'daemon_pid': read_json(self.root / 'daemon.json')['pid'] if (self.root / 'daemon.json').exists() else None}

    def enqueue(self, attempt):
        identifier(attempt['id'])
        identifier(attempt['token'])
        identifier(attempt['experiment'])
        validate_job(attempt['job'])
        if attempt['package_id'] != package_identity():
            raise ValueError('worker package mismatch')
        bundle = inspect_bundle(self.root / 'bundles' / attempt['job']['build'])
        if bundle['platform'] != platform_identity() or (self.config['builds'] and bundle['id'] not in self.config['builds']):
            raise ValueError('ineligible build')
        with transaction(self.db):
            old = self.db.execute('SELECT token,job FROM queue WHERE id=?', (attempt['id'],)).fetchone()
            if old:
                if old['token'] != attempt['token'] or json.loads(old['job']) != attempt:
                    raise ValueError('attempt identity collision')
                return {'enqueued': True}
            if not self.status()['accepting']:
                return {'enqueued': False, 'reason': 'disk_pressure'}
            for source in attempt['resolved_inputs'].values():
                path = self.root / 'objects' / hash_id(source['sha256'])
                if not path.exists() or file_hash(path) != source['sha256']:
                    raise ValueError('input artifact missing or corrupted')
            directory = self.root / 'attempts' / attempt['id']
            directory.mkdir(parents=True, exist_ok=True)
            atomic_json(directory / 'attempt.json', attempt)
            self.db.execute('INSERT INTO queue VALUES (?,?,?,?,?,NULL,NULL,?)',
                            (attempt['id'], attempt['token'], attempt['experiment'], canonical(attempt).decode(), 'queued', time.time()))
        return {'enqueued': True}

    def control(self, experiment, mode):
        identifier(experiment)
        if mode not in ('running', 'paused', 'draining', 'cancelled'):
            raise ValueError('unknown worker control')
        with transaction(self.db):
            previous = self.db.execute('SELECT mode FROM controls WHERE experiment=?', (experiment,)).fetchone()
            if previous and previous['mode'] == 'cancelled' and mode != 'cancelled':
                return {'mode': 'cancelled'}
            self.db.execute('INSERT OR REPLACE INTO controls VALUES (?,?)', (experiment, mode))
            if mode == 'cancelled':
                # Running work finishes safely and is collected as a cancelled late attempt.
                self.db.execute("UPDATE queue SET state='cancelled',updated=? WHERE experiment=? AND state='queued'", (time.time(), experiment))
        return {'mode': mode}

    def install(self, identity, bundle_id):
        archive = self.root / 'objects' / hash_id(identity)
        hash_id(bundle_id)
        final = self.root / 'bundles' / bundle_id
        if final.exists():
            inspect_bundle(final)
            return {'installed': True}
        if file_hash(archive) != identity:
            raise ValueError('corrupted bundle archive')
        if usage(self.root / 'bundles') + archive.stat().st_size > self.config['cache_budget_bytes']:
            raise ValueError('bundle cache budget reached; explicit cleanup required')
        with lock(self.root / ('bundle-' + bundle_id + '.lock'), blocking=True):
            temporary = self.root / 'bundles' / ('.' + bundle_id)
            if temporary.exists():
                shutil.rmtree(temporary)
            temporary.mkdir()
            with zipfile.ZipFile(archive) as zipped:
                total = sum(entry.file_size for entry in zipped.infolist())
                if total > self.config['cache_budget_bytes'] or shutil.disk_usage(self.root).free - total < self.config['disk_reserve_bytes']:
                    raise ValueError('insufficient disk for bundle extraction')
                for entry in zipped.infolist():
                    target = inside(temporary, entry.filename)
                    if entry.is_dir():
                        target.mkdir(parents=True, exist_ok=True)
                    else:
                        target.parent.mkdir(parents=True, exist_ok=True)
                        with zipped.open(entry) as source, target.open('wb') as out:
                            shutil.copyfileobj(source, out, 1024 * 1024)
            manifest = read_json(temporary / 'bundle.json')
            if manifest['id'] != bundle_id:
                raise ValueError('bundle identity mismatch')
            for entry in manifest['files']:
                os.chmod(inside(temporary, entry['path']), entry['mode'] & 0o777)
            inspect_bundle(temporary)
            os.rename(temporary, final)
        return {'installed': True}

    def acknowledge(self, identity, token):
        identifier(identity)
        with transaction(self.db):
            row = self.db.execute('SELECT * FROM queue WHERE id=?', (identity,)).fetchone()
            if not row or row['token'] != token:
                raise ValueError('unknown acknowledgement')
            if row['state'] not in ('done', 'acknowledged'):
                raise ValueError('cannot acknowledge an unfinished attempt')
            self.db.execute("UPDATE queue SET state='acknowledged',updated=? WHERE id=?", (time.time(), identity))
        # Destructive cleanup is after a durable acknowledgement, never before it.
        directory = self.root / 'attempts' / identity
        with lock(self.root / 'spool.lock', blocking=True):
            record = read_json(directory / 'record.json') if (directory / 'record.json').exists() else {'artifacts': []}
            needed = set()
            for other in self.db.execute("SELECT id FROM queue WHERE state!='acknowledged'"):
                path = self.root / 'attempts' / other['id'] / 'record.json'
                if path.exists(): needed.update(a['sha256'] for a in read_json(path)['artifacts'])
            for artifact in record['artifacts']:
                if artifact['sha256'] not in needed:
                    with contextlib.suppress(FileNotFoundError): (self.root / 'spool' / artifact['sha256']).unlink()
            shutil.rmtree(directory, ignore_errors=True)
        return {'acknowledged': True}

    def start(self):
        if not locked(self.root / 'daemon.lock'):
            with contextlib.suppress(FileNotFoundError): (self.root / 'stop.json').unlink()
            with (self.root / 'daemon.log').open('ab') as log:
                subprocess.Popen(launcher() + ['daemon', str(self.root)], stdin=subprocess.DEVNULL,
                                 stdout=log, stderr=log, start_new_session=True)
        return {'starting': True}

    def tick(self):
        self.reload_config()
        now = time.time()
        rows = self.db.execute("SELECT * FROM queue WHERE state IN ('running','packing','executed')").fetchall()
        for row in rows:
            directory = self.root / 'attempts' / row['id']
            if (directory / 'record.json').exists():
                self.db.execute("UPDATE queue SET state='done',updated=? WHERE id=?", (now, row['id']))
            elif (directory / 'execution.json').exists() and row['state'] == 'running':
                self.db.execute("UPDATE queue SET state='executed',updated=? WHERE id=?", (now, row['id']))
            elif row['state'] == 'running' and now - row['started'] > json.loads(row['job'])['job']['limits'].get('timeout_seconds', 3600) + 15 and locked(directory / 'execute.lock'):
                # The inherited lock remains held by an orphan engine if its
                # supervisor was killed. Its process group cannot be reused while
                # that original child still holds the execution lock.
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(row['pid'], signal.SIGKILL)
            elif row['state'] in ('running', 'packing') and now - row['updated'] > 5:
                stage = 'execute' if row['state'] == 'running' else 'pack'
                if not locked(directory / (stage + '.lock')):
                    if stage == 'pack':
                        self.db.execute("UPDATE queue SET state='executed',updated=? WHERE id=?", (now, row['id']))
                    else:
                        atomic_json(directory / 'execution.json', {'category': 'interrupted', 'diagnostic': 'attempt supervisor interrupted',
                                                                   'started': row['started'], 'finished': now, 'exit_code': None, 'result': None})
        running = self.db.execute("SELECT count(*) FROM queue WHERE state='running'").fetchone()[0]
        collecting = self.db.execute("SELECT count(*) FROM queue WHERE state='packing'").fetchone()[0]
        for row in self.db.execute("SELECT * FROM queue WHERE state='executed' ORDER BY updated").fetchall():
            if collecting >= self.config['collect_slots']:
                break
            self.launch(row, 'pack', 'packing')
            collecting += 1
        if not self.status()['accepting']:
            return
        for row in self.db.execute("SELECT q.* FROM queue q LEFT JOIN controls c ON q.experiment=c.experiment WHERE q.state='queued' AND COALESCE(c.mode,'running') NOT IN ('draining','cancelled') ORDER BY q.updated").fetchall():
            if running >= self.config['slots']:
                break
            self.launch(row, 'execute', 'running')
            running += 1

    def launch(self, row, command, state):
        now = time.time()
        self.db.execute('UPDATE queue SET state=?,started=COALESCE(started,?),updated=? WHERE id=?', (state, now, now, row['id']))
        directory = self.root / 'attempts' / row['id']
        with (directory / 'supervisor.log').open('ab') as log:
            process = subprocess.Popen(launcher() + [command, str(self.root), row['id']], stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=log, start_new_session=True)
        self.db.execute('UPDATE queue SET pid=? WHERE id=?', (process.pid, row['id']))


def execute(root, identity):
    worker = Worker(root)
    directory = worker.root / 'attempts' / identifier(identity)
    with lock(directory / 'execute.lock') as guard:
        if (directory / 'execution.json').exists():
            return
        attempt = read_json(directory / 'attempt.json')
        job = attempt['job']
        started = time.time()
        record = {'started': started, 'finished': None, 'exit_code': None, 'category': 'interrupted', 'result': None,
                  'diagnostics': {'core_requested': bool(job['outputs'].get('core')), 'stack_available': False}}
        process = None
        try:
            bundle = inspect_bundle(worker.root / 'bundles' / job['build'])
            inputs = {}
            for name, artifact in attempt['resolved_inputs'].items():
                target = directory / 'inputs' / identifier(name)
                copy_decoded(worker.root / 'objects' / artifact['sha256'], target, artifact)
                inputs[name] = str(target)
            out = directory / 'output'
            out.mkdir(exist_ok=True)
            args = get_job_type(job['type']).command(job, bundle, directory, inputs)
            record['command'] = args
            # HOME is child-only. Each attempt also gets an explicit engine profile.
            env = {k: v for k, v in os.environ.items() if not k.startswith('GLOB2_')}
            env['HOME'] = str(directory / 'home')
            env['XDG_CONFIG_HOME'] = str(directory / 'home/.config')
            Path(env['HOME']).mkdir(exist_ok=True)
            memory = job['limits'].get('memory_mb', worker.config['memory_mb'])
            def resource_limits():
                import resource
                if memory:
                    resource.setrlimit(resource.RLIMIT_AS, (int(memory * 1024**2), int(memory * 1024**2)))
                if job['outputs'].get('core'):
                    soft, hard = resource.getrlimit(resource.RLIMIT_CORE)
                    resource.setrlimit(resource.RLIMIT_CORE, (hard, hard))
            with (directory / 'stdout.log').open('wb') as stdout, (directory / 'stderr.log').open('wb') as stderr:
                process = subprocess.Popen(args, cwd=bundle['directory'], env=env, stdout=stdout, stderr=stderr,
                                           preexec_fn=resource_limits, pass_fds=(guard.fileno(),))
                timeout = job['limits'].get('timeout_seconds', 3600)
                deadline = time.monotonic() + timeout
                while process.poll() is None:
                    if time.monotonic() >= deadline:
                        record['category'] = 'timeout'
                        process.kill()
                        break
                    if shutil.disk_usage(directory).free < worker.config['disk_reserve_bytes'] // 2:
                        record['category'] = 'disk_pressure'
                        process.kill()
                        break
                    time.sleep(0.2)
                record['exit_code'] = process.wait()
                import resource
                measured = resource.getrusage(resource.RUSAGE_CHILDREN)
                record['resource_usage'] = {'user_seconds': measured.ru_utime, 'system_seconds': measured.ru_stime,
                                             'peak_rss_bytes': measured.ru_maxrss * (1 if sys.platform == 'darwin' else 1024)}
            if record['category'] not in ('timeout', 'disk_pressure'):
                code = record['exit_code']
                record['category'] = 'crash' if code < 0 else 'invalid_request' if code == 2 else 'process_failure' if code else 'success'
                try:
                    record['result'] = get_job_type(job['type']).collect(job, directory)
                    status = record['result']['status']
                    if code >= 0 and status in ('invalid_request', 'generation_failed', 'artifact_failure'):
                        record['category'] = status
                    elif code == 0 and status != 'completed':
                        record['category'] = 'invalid_result'
                except (OSError, ValueError) as error:
                    record['diagnostic'] = str(error)
                    if code == 0:
                        record['category'] = 'invalid_result'
            progress = out / 'progress.jsonl'
            if progress.exists():
                with progress.open('rb') as stream:
                    stream.seek(max(0, progress.stat().st_size - 8192))
                    lines = stream.read().splitlines()
                if lines:
                    record['last_progress'] = json.loads(lines[-1])
            elif (out / 'progress.json').exists():
                record['last_progress'] = read_json(out / 'progress.json')
        except BaseException as error:
            if process and process.poll() is None:
                process.kill()
                process.wait()
            record['category'] = 'invalid_request' if isinstance(error, ValueError) else 'interrupted'
            record['diagnostic'] = str(error)
            record['supervisor_traceback'] = traceback.format_exc()
        if process and record['category'] == 'crash':
            record['diagnostics'] = postmortem(record['command'][0], process.pid, directory,
                                               job['outputs'].get('stack', False) or job['outputs'].get('core', False), started)
        record['finished'] = time.time()
        record['seconds'] = record['finished'] - started
        atomic_json(directory / 'execution.json', record)
    worker.close()


def pack(root, identity):
    worker = Worker(root)
    directory = worker.root / 'attempts' / identifier(identity)
    with lock(directory / 'pack.lock'), lock(worker.root / 'spool.lock', blocking=True):
        if (directory / 'record.json').exists():
            return
        attempt = read_json(directory / 'attempt.json')
        execution = read_json(directory / 'execution.json')
        # A completed result is carried by execution.json, so an Elo job with no
        # requested outputs need not copy incidental maps, logs and reports back
        # to the coordinator. They can dominate a short duel's wall time and
        # leave otherwise idle worker slots waiting for collection.
        retain_incidental = bool(attempt['job']['outputs'])
        artifacts = []
        if retain_incidental:
            for path in sorted(directory.rglob('*')):
                relative = path.relative_to(directory).as_posix()
                if not path.is_file() or path.is_symlink() or relative.startswith(('home/', 'inputs/', 'output/profile/')) or '/profile/' in relative or relative in ('attempt.json', 'execution.json', 'record.json') or path.suffix == '.lock':
                    continue
                # A packing restart replaces objects atomically with identical bytes.
                meta = store_artifact(path, worker.root / 'spool', compress=path.stat().st_size > 4096 and path.suffix not in ('.json',))
                meta['path'] = relative[7:] if relative.startswith('output/') else relative
                artifacts.append(meta)
        present = {a['path'] for a in artifacts}
        requested = list(attempt['job']['outputs'].get('required', []))
        requested += [f'{s}.game' for s in attempt['job']['outputs'].get('saves', []) if s in ('initial', 'final')]
        if attempt['job']['outputs'].get('replay'):
            requested.append('game.replay')
        if 'checksums' in attempt['job']['outputs'].get('telemetry', []):
            requested.append('game.replay.checksums')
        if 'terrain' in attempt['job']['outputs'].get('reports', []):
            requested.append('terrain.txt')
        if attempt['job']['outputs'].get('map'):
            requested += [f'map-r{i}.map' for i in range(attempt['job']['config'].get('rotations', 1))]
        missing = sorted(set(requested) - present)
        if missing and execution['category'] == 'success':
            execution['category'] = 'artifact_failure'
        record = {'schema_version': 1, 'id': identity, 'token': attempt['token'], 'experiment': attempt['experiment'],
                  'job': attempt['job'], 'host': attempt['host'], 'package_id': attempt['package_id'],
                  **execution, 'artifacts': artifacts, 'missing_artifacts': missing}
        atomic_json(directory / 'record.json', record)
    worker.close()


def rpc(root, request):
    if request.get('protocol_version') != PROTOCOL_VERSION or request.get('package_id') != package_identity():
        raise ValueError('worker protocol/package mismatch')
    worker = Worker(root)
    try:
        args = request.get('args', {})
        op = request['op']
        if op == 'status': return worker.status()
        if op == 'configure': return worker.configure(**args)
        if op == 'start': return worker.start()
        if op == 'enqueue': return worker.enqueue(**args)
        if op == 'control': return worker.control(**args)
        if op == 'install': return worker.install(**args)
        if op == 'ack': return worker.acknowledge(**args)
        if op == 'offset': return {'offset': offset(worker.root / 'objects', args['identity'], args['size'])}
        if op == 'put':
            remaining = len(args['data']) * 3 // 4
            if shutil.disk_usage(worker.root).free - remaining < worker.config['disk_reserve_bytes']:
                raise ValueError('disk reserve prevents receiving another chunk')
            if usage(worker.root / 'objects') + usage(worker.root / 'bundles') + remaining > worker.config['cache_budget_bytes']:
                raise ValueError('cache budget reached; explicit cleanup required')
            return put_chunk(worker.root / 'objects', args['identity'], args['size'], args['start'], args['data'], args['checksum'])
        if op == 'stop':
            atomic_json(worker.root / 'stop.json', {'requested': time.time()})
            return {'stopping': True}
        if op == 'get':
            location = 'spool' if (worker.root / 'spool' / hash_id(args['identity'])).exists() else 'objects'
            return get_chunk(worker.root / location, args['identity'], args['start'])
        if op == 'record': return read_json(worker.root / 'attempts' / identifier(args['identity']) / 'record.json')
        if op == 'has_bundle':
            path = worker.root / 'bundles' / hash_id(args['identity'])
            return {'present': path.exists() and bool(inspect_bundle(path))}
        if op == 'cleanup':
            # Objects may be referenced by queued jobs or unacknowledged results.
            active = worker.db.execute("SELECT count(*) FROM queue WHERE state NOT IN ('acknowledged','cancelled')").fetchone()[0]
            if active: raise ValueError('worker cleanup requires all work acknowledged or cancelled')
            if args.get('objects'):
                shutil.rmtree(worker.root / 'objects'); (worker.root / 'objects').mkdir()
            if args.get('bundles'):
                shutil.rmtree(worker.root / 'bundles'); (worker.root / 'bundles').mkdir()
            return {'cleaned': True}
        raise ValueError('unknown RPC operation: ' + op)
    finally:
        worker.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=('rpc', 'daemon', 'execute', 'pack'))
    parser.add_argument('root')
    parser.add_argument('attempt', nargs='?')
    args = parser.parse_args()
    if args.command == 'rpc':
        try:
            request = json.loads(sys.stdin.buffer.read(2 * 1024 * 1024 + 1))
            result = {'ok': True, 'value': rpc(args.root, request)}
        except Exception as error:
            result = {'ok': False, 'error': str(error)}
        sys.stdout.buffer.write(canonical(result) + b'\n')
    elif args.command == 'daemon':
        try:
            with lock(Path(args.root) / 'daemon.lock'):
                worker = Worker(args.root)
                atomic_json(worker.root / 'daemon.json', {'pid': os.getpid(), 'package_id': package_identity()})
                try:
                    while not (worker.root / 'stop.json').exists():
                        worker.tick()
                        # Reap completed supervisor children without blocking dispatch.
                        while True:
                            try:
                                if os.waitpid(-1, os.WNOHANG)[0] == 0: break
                            except ChildProcessError:
                                break
                        time.sleep(0.5)
                finally:
                    worker.close()
        except BlockingIOError:
            pass
    elif args.command == 'execute': execute(args.root, args.attempt)
    else: pack(args.root, args.attempt)


if __name__ == '__main__':
    main()
