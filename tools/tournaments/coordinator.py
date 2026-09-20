"""Lease authority and durable single-result acceptance for at-least-once work."""
import concurrent.futures
import contextlib
import json
from pathlib import Path
import shutil
import sqlite3
import time
import uuid
import zipfile

from .bundles import eligible, inspect_bundle, package_identity
from .common import (atomic_json, canonical, database, digest, file_hash, hash_id, identifier,
                     lock, read_json, transaction)
from .model import DEFAULTS, validate_experiment
from .transfer import receive_file, send_file
from .transport import Transport, worker_archive

INFRASTRUCTURE = {'transport_failure', 'interrupted', 'disk_pressure', 'artifact_failure', 'invalid_result'}
PROCESS_FAILURES = {'crash', 'timeout', 'process_failure'}
TERMINAL_RESULTS = {'success', 'invalid_request', 'generation_failed'}


class Coordinator:
    def __init__(self, directory):
        self.root = Path(directory).resolve()
        self.manifest = read_json(self.root / 'experiment.json')
        if self.manifest['package_id'] != package_identity():
            raise ValueError('experiment pins another worker package; use its preserved worker.pyz or create a new experiment revision')
        self.settings = DEFAULTS | self.manifest.get('settings', {})
        # Bundle manifests are immutable once installed, and dispatch consults one
        # per pending job per host. Reading them from disk each time costs more
        # than everything else the coordinator does on a large experiment.
        self.bundles = {}
        self.db = database(self.root / 'state.sqlite')
        self.db.executescript('''
            CREATE TABLE IF NOT EXISTS jobs (
                id TEXT PRIMARY KEY, ordinal INTEGER NOT NULL, spec TEXT NOT NULL,
                state TEXT NOT NULL, active TEXT, accepted TEXT);
            CREATE TABLE IF NOT EXISTS attempts (
                id TEXT PRIMARY KEY, job_id TEXT NOT NULL REFERENCES jobs(id), host TEXT NOT NULL,
                token TEXT NOT NULL UNIQUE, lease_until REAL NOT NULL, state TEXT NOT NULL,
                record TEXT, created REAL NOT NULL, category TEXT);
            CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS hosts (name TEXT PRIMARY KEY, updated REAL NOT NULL, status TEXT, error TEXT);
        ''')
        # Submission and exports are replayable if interrupted between files and SQLite.
        with transaction(self.db):
            for ordinal, job in enumerate(self.manifest['jobs']):
                self.db.execute('INSERT OR IGNORE INTO jobs VALUES (?,?,?,\'pending\',NULL,NULL)',
                                (job['id'], ordinal, canonical(job).decode()))
            self.db.execute("INSERT OR IGNORE INTO settings VALUES ('mode','running')")
        self.repair_exports()

    def close(self):
        self.db.close()

    @classmethod
    def submit(cls, directory, manifest, bundle_directories):
        validate_experiment(manifest)
        directory = Path(directory).resolve()
        resolved = dict(manifest, package_id=package_identity())
        if (directory / 'experiment.json').exists():
            if read_json(directory / 'experiment.json') != resolved:
                raise ValueError('existing experiment is immutable; create a new revision/cohort')
            return cls(directory)
        builds = {b['id']: b for b in (inspect_bundle(p) for p in bundle_directories)}
        required = {job['build'] for job in manifest['jobs']}
        if required - builds.keys():
            raise ValueError('missing supplied build bundles: ' + ', '.join(required - builds.keys()))
        for job in manifest['jobs']:
            if job['type'] not in builds[job['build']]['capabilities']['commands']:
                raise ValueError('build does not support job type')
        for subdir in ('builds', 'jobs', 'results', 'failures', 'attempts', 'artifacts', 'reports', 'transfers'):
            (directory / subdir).mkdir(parents=True, exist_ok=True)
        for identity in required:
            target = directory / 'builds' / identity
            if not target.exists():
                shutil.copytree(builds[identity]['directory'], target)
            inspect_bundle(target)
        for job in manifest['jobs']:
            atomic_json(directory / 'jobs' / (job['id'] + '.json'), job)
            for source in job['inputs'].values():
                if 'sha256' in source:
                    path = directory / 'artifacts' / source['sha256']
                    if not path.exists() or file_hash(path) != source['sha256']:
                        raise ValueError('input artifact not imported: ' + source['sha256'])
        worker_archive(directory / 'worker.pyz')
        atomic_json(directory / 'experiment.json', resolved)
        return cls(directory)

    def repair_exports(self):
        for row in self.db.execute('SELECT id,record FROM attempts WHERE record IS NOT NULL'):
            record = json.loads(row['record'])
            path = self.root / 'attempts' / (row['id'] + '.json')
            if not path.exists():
                atomic_json(path, record)
            if record['category'] != 'success':
                path = self.root / 'failures' / (row['id'] + '.json')
                if not path.exists(): atomic_json(path, record)
        for row in self.db.execute('SELECT j.id,a.record FROM jobs j JOIN attempts a ON j.accepted=a.id'):
            path = self.root / 'results' / (row['id'] + '.json')
            if not path.exists(): atomic_json(path, json.loads(row['record']))

    @property
    def mode(self):
        return self.db.execute("SELECT value FROM settings WHERE key='mode'").fetchone()[0]

    def control(self, mode):
        if mode not in ('running', 'paused', 'draining', 'cancelled'):
            raise ValueError('invalid coordinator control')
        with transaction(self.db):
            if self.mode == 'cancelled' and mode != 'cancelled':
                raise ValueError('cancellation is durable; retry into a new experiment')
            self.db.execute("UPDATE settings SET value=? WHERE key='mode'", (mode,))
            if mode == 'cancelled':
                self.db.execute("UPDATE jobs SET state='cancelled',active=NULL WHERE state IN ('pending','active')")
        return self.status()

    def retry(self, job_id=None):
        if self.mode == 'cancelled':
            raise ValueError('cancelled experiment cannot be retried in place')
        with transaction(self.db):
            rows = self.db.execute("SELECT id FROM jobs WHERE state='failed'" + (' AND id=?' if job_id else ''),
                                   (job_id,) if job_id else ()).fetchall()
            for row in rows:
                self.db.execute("UPDATE jobs SET state='pending',active=NULL WHERE id=?", (row['id'],))
                # Explicit retry resets the budget without erasing failed attempts.
                self.db.execute("UPDATE attempts SET state='manual_retry_history' WHERE job_id=?", (row['id'],))
        return {'retried': [row['id'] for row in rows]}

    def renew(self, host, live, now=None):
        now = time.time() if now is None else now
        with transaction(self.db):
            for attempt in live:
                self.db.execute("UPDATE attempts SET lease_until=? WHERE id=? AND token=? AND host=? AND state='leased' AND lease_until>?",
                                (now + self.settings['lease_seconds'], attempt['id'], attempt['token'], host, now))

    def _retry_or_fail(self, job_id, category):
        family = INFRASTRUCTURE if category in INFRASTRUCTURE else PROCESS_FAILURES
        limit = self.settings['infrastructure_attempts'] if category in INFRASTRUCTURE else self.settings['process_attempts']
        count = sum(1 for row in self.db.execute("SELECT category FROM attempts WHERE job_id=? AND state!='manual_retry_history'", (job_id,)) if row['category'] in family)
        self.db.execute('UPDATE jobs SET state=?,active=NULL WHERE id=? AND accepted IS NULL AND state!=\'cancelled\'',
                        ('pending' if count < limit else 'failed', job_id))

    def expire(self, now=None):
        now = time.time() if now is None else now
        with transaction(self.db):
            for row in self.db.execute("SELECT * FROM attempts WHERE state='leased' AND lease_until<=?", (now,)).fetchall():
                self.db.execute("UPDATE attempts SET state='expired',category='transport_failure' WHERE id=?", (row['id'],))
                # Lease loss is evidence about transport, not a claim that a game crashed.
                diagnostic = {'schema_version': 1, 'id': row['id'], 'token': row['token'], 'host': row['host'],
                              'job_id': row['job_id'], 'category': 'transport_failure', 'lease_expired': now}
                atomic_json(self.root / 'failures' / (row['id'] + '-lease.json'), diagnostic)
                active = self.db.execute('SELECT active FROM jobs WHERE id=?', (row['job_id'],)).fetchone()[0]
                if active == row['id']:
                    self._retry_or_fail(row['job_id'], 'transport_failure')

    def bundle(self, build):
        if build not in self.bundles:
            self.bundles[build] = inspect_bundle(self.root / 'builds' / build, verify=False)
        return self.bundles[build]

    def resolve_inputs(self, job):
        inputs = {}
        for name, source in job['inputs'].items():
            if 'job' in source:
                row = self.db.execute('SELECT accepted,state FROM jobs WHERE id=?', (source['job'],)).fetchone()
                if not row or not row['accepted']:
                    return None
                record = json.loads(self.db.execute('SELECT record FROM attempts WHERE id=?', (row['accepted'],)).fetchone()[0])
                artifact = next((a for a in record['artifacts'] if a['path'] == source['artifact']), None)
                if artifact is None or record['category'] != 'success':
                    return None
                inputs[name] = artifact
            else:
                inputs[name] = source
        return inputs

    def dispatch(self, host, status, now=None):
        now = time.time() if now is None else now
        self.expire(now)
        if self.mode != 'running' or not status['accepting']:
            return []
        # Finished results retain their leases until verified, but no longer consume
        # compute prefetch capacity. Missing attempts remain counted (input delivery).
        states = {a['id']: a.get('state') for a in status.get('attempts', [])}
        computing = [r for r in self.db.execute(
            "SELECT id,job_id FROM attempts WHERE host=? AND state='leased'", (host,))
            if states.get(r['id']) not in ('executed', 'packing', 'done', 'acknowledged', 'cancelled')]
        count = len(computing)
        other_work = sum(item.get('experiment') != self.manifest['id'] and item.get('state') in ('queued','running') for item in status.get('attempts', []))
        room = status['slots'] * (1 + self.settings['prefetch']) - count - other_work
        dispatched = []
        buffered_seconds = sum(json.loads(self.db.execute('SELECT spec FROM jobs WHERE id=?',
                               (r['job_id'],)).fetchone()[0])['limits'].get('estimated_seconds',60)
                               for r in computing)
        for row in self.db.execute("SELECT * FROM jobs WHERE state='pending' ORDER BY ordinal").fetchall():
            if len(dispatched) >= room:
                break
            job = json.loads(row['spec'])
            bundle = self.bundle(job['build'])
            if not eligible(bundle, status, job['type']):
                continue
            parents = [self.db.execute('SELECT state,accepted FROM jobs WHERE id=?', (dep,)).fetchone() for dep in job['depends_on']]
            if any(parent['state'] in ('failed', 'cancelled', 'blocked') for parent in parents):
                self.db.execute("UPDATE jobs SET state='blocked' WHERE id=?", (job['id'],))
                continue
            if any(not parent['accepted'] for parent in parents):
                continue
            inputs = self.resolve_inputs(job)
            if inputs is None:
                self.db.execute("UPDATE jobs SET state='blocked' WHERE id=?", (job['id'],))
                continue
            # Prefer another eligible, recently connected host for a repeat attempt.
            previous = self.db.execute('SELECT host FROM attempts WHERE job_id=? ORDER BY created DESC LIMIT 1', (job['id'],)).fetchone()
            if previous and previous['host'] == host:
                alternatives = self.db.execute('SELECT name,status FROM hosts WHERE name!=? AND updated>? AND error IS NULL', (host, now - self.settings['lease_seconds'])).fetchall()
                if any(eligible(bundle, json.loads(other['status']), job['type']) and json.loads(other['status'])['accepting'] for other in alternatives):
                    continue
            estimate = job['limits'].get('estimated_seconds', 60)
            time_budget = self.settings.get('prefetch_seconds')
            if time_budget is not None and (count or dispatched) and buffered_seconds + estimate > time_budget:
                break
            attempt = {'schema_version': 1, 'id': uuid.uuid4().hex, 'token': uuid.uuid4().hex,
                       'experiment': self.manifest['id'], 'host': host, 'job': job,
                       'package_id': self.manifest['package_id'], 'resolved_inputs': inputs,
                       'lease_seconds': self.settings['lease_seconds']}
            with transaction(self.db):
                if self.mode != 'running':
                    break
                changed = self.db.execute("UPDATE jobs SET state='active',active=? WHERE id=? AND state='pending'", (attempt['id'], job['id'])).rowcount
                if not changed:
                    continue
                self.db.execute('INSERT INTO attempts VALUES (?,?,?,?,?,\'leased\',NULL,?,NULL)',
                                (attempt['id'], job['id'], host, attempt['token'], now + self.settings['lease_seconds'], now))
                atomic_json(self.root / 'transfers' / (attempt['id'] + '.json'), attempt)
            dispatched.append(attempt)
            buffered_seconds += estimate
        return dispatched

    def accept(self, record, now=None):
        now = time.time() if now is None else now
        identifier(record['id'])
        if record.get('schema_version') != 1 or record.get('category') not in INFRASTRUCTURE | PROCESS_FAILURES | TERMINAL_RESULTS:
            raise ValueError('invalid attempt schema or failure category')
        row = self.db.execute('SELECT * FROM attempts WHERE id=?', (record['id'],)).fetchone()
        if not row or row['token'] != record['token'] or row['host'] != record['host']:
            raise ValueError('unknown attempt lease')
        expected_job = json.loads(self.db.execute('SELECT spec FROM jobs WHERE id=?', (row['job_id'],)).fetchone()[0])
        if record['job'] != expected_job or record['package_id'] != self.manifest['package_id']:
            raise ValueError('result configuration/build/package mismatch')
        for artifact in record['artifacts']:
            path = self.root / 'artifacts' / hash_id(artifact['sha256'])
            if not path.exists() or path.stat().st_size != artifact['bytes'] or file_hash(path) != artifact['sha256']:
                raise ValueError('result artifacts not durably verified')
        self.expire(now)
        with transaction(self.db):
            row = self.db.execute('SELECT * FROM attempts WHERE id=?', (record['id'],)).fetchone()
            if row['record']:
                old = json.loads(row['record'])
                if {k: v for k, v in old.items() if k != 'accepted'} != {k: v for k, v in record.items() if k != 'accepted'}:
                    raise ValueError('conflicting duplicate attempt result')
                return old['accepted']
            job = self.db.execute('SELECT * FROM jobs WHERE id=?', (row['job_id'],)).fetchone()
            current = row['state'] == 'leased' and job['active'] == row['id'] and job['accepted'] is None and job['state'] != 'cancelled'
            accepted = current and record['category'] in TERMINAL_RESULTS
            committed = dict(record, accepted=bool(accepted))
            self.db.execute('UPDATE attempts SET record=?,state=?,category=? WHERE id=?',
                            (canonical(committed).decode(), 'accepted' if accepted else 'finished' if current else 'late', row['category'] if row['state']=='expired' else record['category'], row['id']))
            if accepted:
                self.db.execute("UPDATE jobs SET accepted=?,active=NULL,state='completed' WHERE id=?", (row['id'], job['id']))
            elif current:
                self._retry_or_fail(job['id'], record['category'])
        self.repair_exports()
        return bool(accepted)

    def host_status(self, name, status=None, error=None):
        self.db.execute('INSERT OR REPLACE INTO hosts VALUES (?,?,?,?)', (name, time.time(), canonical(status).decode() if status else None, error))

    def status(self):
        measurements = {}
        for row in self.db.execute('SELECT host,record FROM attempts WHERE record IS NOT NULL'):
            record = json.loads(row['record'])
            item = measurements.setdefault(row['host'], {'finished_attempts':0, 'accepted_results':0, 'process_seconds':0.0, 'simulation_ticks':0, 'peak_rss_bytes':0})
            item['finished_attempts'] += 1
            item['accepted_results'] += int(record.get('accepted',False))
            item['process_seconds'] += record.get('seconds',0)
            item['simulation_ticks'] += (record.get('result') or {}).get('ticks',0)
            item['peak_rss_bytes'] = max(item['peak_rss_bytes'],(record.get('resource_usage') or {}).get('peak_rss_bytes',0))
        for item in measurements.values():
            item['ticks_per_process_second'] = item['simulation_ticks']/item['process_seconds'] if item['process_seconds'] else None
        return {'experiment': self.manifest['id'], 'mode': self.mode, 'measurements': measurements,
                'jobs': dict(self.db.execute('SELECT state,count(*) FROM jobs GROUP BY state').fetchall()),
                'attempts': dict(self.db.execute('SELECT state,count(*) FROM attempts GROUP BY state').fetchall()),
                'failures': [dict(row) for row in self.db.execute('SELECT host,category,count(*) AS count FROM attempts WHERE category IS NOT NULL AND category!=\'success\' GROUP BY host,category')],
                'hosts': [dict(row) | {'status': json.loads(row['status']) if row['status'] else None} for row in self.db.execute('SELECT * FROM hosts ORDER BY name')]}

    def archive_bundle(self, identity):
        path = self.root / 'transfers' / (identity + '.zip')
        with lock(path.with_suffix('.lock'), blocking=True):
            if not path.exists():
                source = self.root / 'builds' / identity
                tmp = path.with_suffix('.tmp')
                with zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_STORED, allowZip64=True) as archive:
                    for item in sorted(source.rglob('*')):
                        if item.is_file(): archive.write(item, item.relative_to(source).as_posix())
                tmp.replace(path)
        return path

    def poll_host(self, config, transport, collect_only=False):
        """Control/renewal never waits on bulk input or result transfers."""
        status = transport.rpc('status')
        if not status['daemon_running']: transport.rpc('start')
        self.host_status(config['name'], status)
        self.renew(config['name'], status['attempts'])
        # Keep locally reserved input deliveries alive while this host is reachable.
        live = {a['id'] for a in status['attempts']}
        reserved = [dict(r) for r in self.db.execute(
            "SELECT id,token FROM attempts WHERE host=? AND state='leased'", (config['name'],))
            if r['id'] not in live]
        self.renew(config['name'], reserved)
        transport.rpc('control', experiment=self.manifest['id'], mode=self.mode)
        if not collect_only: self.dispatch(config['name'], status)
        return status

    def deliver_attempt(self, transport, identity):
        row = self.db.execute("SELECT * FROM attempts WHERE id=? AND state='leased'", (identity,)).fetchone()
        if not row: return True
        if self.mode != 'running': return False
        attempt = read_json(self.root / 'transfers' / (identity + '.json'))
        build = attempt['job']['build']
        if not transport.rpc('has_bundle', identity=build)['present']:
            archive = self.archive_bundle(build)
            archive_id = file_hash(archive)
            if not send_file(transport, archive, archive_id): return False
            transport.rpc('install', identity=archive_id, bundle_id=build)
        for artifact in attempt['resolved_inputs'].values():
            if not send_file(transport, self.root / 'artifacts' / artifact['sha256'], artifact['sha256']):
                return False
        # Controls may change during transfer. Enqueue is idempotent if its reply is lost.
        row = self.db.execute("SELECT state FROM attempts WHERE id=?", (identity,)).fetchone()
        if row['state'] != 'leased': return True
        if self.mode != 'running': return False
        return transport.rpc('enqueue', attempt=attempt)['enqueued']

    def collect_attempt(self, transport, item):
        record = transport.rpc('record', identity=item['id'])
        for artifact in record['artifacts']:
            # Two attempts may share the same artifact. Coordinate resumable offsets
            # across all collection lanes, including distinct hosts.
            with lock(self.root / 'transfers' / (artifact['sha256'] + '.receive.lock'), blocking=True):
                if not receive_file(transport, self.root / 'artifacts', artifact['sha256'], artifact['bytes'], budget=8):
                    return False
        self.accept(record)
        transport.rpc('ack', identity=item['id'], token=item['token'])
        return True

    def sync_host(self, config, collect_only=False):
        # Each host loop gets its own SQLite connection. Network, compression and
        # copying never run under the lease authority's database transaction.
        transport = Transport(config, self.root / 'worker.pyz')
        status = transport.rpc('status')
        if not status['daemon_running']: transport.rpc('start')
        self.host_status(config['name'], status)
        self.renew(config['name'], status['attempts'])
        transport.rpc('control', experiment=self.manifest['id'], mode=self.mode)
        for item in status['attempts']:
            if item['experiment'] != self.manifest['id'] or item['state'] != 'done':
                continue
            record = transport.rpc('record', identity=item['id'])
            ready = True
            for artifact in record['artifacts']:
                if not receive_file(transport, self.root / 'artifacts', artifact['sha256'], artifact['bytes'], budget=8):
                    ready = False
                    break
            if ready:
                self.accept(record)
                transport.rpc('ack', identity=item['id'], token=item['token'])
        self.expire()
        if collect_only: return
        self.dispatch(config['name'], status)
        live_ids = {item['id'] for item in status['attempts']}
        rows = self.db.execute("SELECT * FROM attempts WHERE host=? AND state='leased'", (config['name'],)).fetchall()
        for row in rows:
            if row['id'] in live_ids: continue
            attempt = read_json(self.root / 'transfers' / (row['id'] + '.json'))
            # Dispatch may be reserved while the bundle uploads. Renew this live
            # transport's reservations too; disconnected reservations still expire.
            self.renew(config['name'], [{'id': row['id'], 'token': row['token']}])
            identity = attempt['job']['build']
            if not transport.rpc('has_bundle', identity=identity)['present']:
                archive = self.archive_bundle(identity)
                archive_id = file_hash(archive)
                if not send_file(transport, archive, archive_id): break
                transport.rpc('install', identity=archive_id, bundle_id=identity)
            ready = True
            for artifact in attempt['resolved_inputs'].values():
                if not send_file(transport, self.root / 'artifacts' / artifact['sha256'], artifact['sha256']):
                    ready = False
                    break
            if not ready: break
            # Re-check cancellation after potentially slow transfers.
            if self.mode != 'running': break
            transport.rpc('enqueue', attempt=attempt)

    def run(self, hosts, once=False, collect_only=False):
        if not once:
            from .pipeline import run
            return run(self, hosts, collect_only)
        with lock(self.root / 'coordinator.lock'):
            with concurrent.futures.ThreadPoolExecutor(max_workers=min(len(hosts), self.settings.get('transfer_slots', 4))) as pool:
                pending = {}
                next_sync = {}
                while True:
                    for config in hosts:
                        if config['name'] in pending or time.monotonic() < next_sync.get(config['name'], 0): continue
                        def synchronize(config=config):
                            local = Coordinator(self.root)
                            try:
                                local.sync_host(config, collect_only)
                            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                                local.host_status(config['name'], error=str(error))
                            finally:
                                local.close()
                        pending[config['name']] = pool.submit(synchronize)
                    if once:
                        for future in pending.values(): future.result()
                        break
                    for name, future in list(pending.items()):
                        if future.done():
                            future.result()
                            del pending[name]
                            next_sync[name] = time.monotonic() + self.settings['heartbeat_seconds']
                    self.expire()
                    states = self.status()['jobs']
                    if not states.get('active', 0) and not states.get('pending', 0):
                        # One final synchronization publishes controls and picks up
                        # acknowledgements; offline duplicates remain collectible.
                        for future in pending.values(): future.result()
                        break
                    time.sleep(min(1.0, self.settings['heartbeat_seconds']))
        return self.status()


import subprocess
