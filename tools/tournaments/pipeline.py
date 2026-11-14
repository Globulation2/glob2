"""Independent bounded control, input and result lanes for continuous execution."""
import concurrent.futures
import subprocess
import sys
import threading
import time
from .common import atomic_json, lock
from .coordinator import Coordinator
from .transport import Transport


def transfer_limits(settings, hosts):
    """Concurrent input and result transfers across all hosts.

    An experiment's `input_transfer_slots`/`result_transfer_slots` cap them;
    without one the lanes are as wide as the hosts' own `input_slots` and
    `result_slots` (2 each by default), so adding hosts adds transfer capacity.
    """
    return {direction: settings.get(direction + '_transfer_slots',
                                    sum(h.get(direction + '_slots', 2) for h in hosts))
            for direction in ('input', 'result')}


def run(coordinator, hosts, collect_only=False):
    configs = {h['name']: h for h in hosts}
    if len(configs) != len(hosts) or not hosts:
        raise ValueError('provide distinct, nonempty hosts')
    for h in hosts:
        for key in ('input_slots', 'result_slots'):
            if type(h.get(key, 2)) is not int or h.get(key, 2) < 1:
                raise ValueError(key + ' must be a positive integer')
    root = coordinator.root
    lane_limits = transfer_limits(coordinator.settings, hosts)
    controls = {name: Transport(h, root / 'worker.pyz') for name, h in configs.items()}
    # Open transfer sessions per host. A remote session costs an SSH handshake and
    # a Python start; one per delivered job or per collected chunk batch made
    # session setup, not bandwidth, the limit on remote hosts.
    idle, idle_lock = {name: [] for name in configs}, threading.Lock()
    lane_errors = {}
    snapshots, next_poll = {}, {}
    polling, uploading, collecting = {}, {}, {}
    delivered, collected = set(), set()
    serviced, host_serviced, sequence = {}, {}, 0
    next_sample = 0
    interval = min(coordinator.settings.get('poll_seconds', 1), coordinator.settings['heartbeat_seconds'])
    errors = (OSError, ValueError, subprocess.TimeoutExpired)

    def poll(name):
        c = Coordinator(root, repair=False, manifest=coordinator.manifest)
        try:
            return c.poll_host(configs[name], controls[name], collect_only)
        finally:
            c.close()

    def transfer(name, item, direction):
        c = Coordinator(root, repair=False, manifest=coordinator.manifest)
        with idle_lock:
            t = idle[name].pop() if idle[name] else None
        if t is None:
            t = Transport(configs[name], root / 'worker.pyz')
            # A successful control poll has already deployed/configured this package.
            t.deployed = True
        reusable = False
        try:
            result = (c.deliver_attempt(t, item['id']) if direction == 'input'
                      else c.collect_attempt(t, item))
            reusable = True
            return result
        finally:
            c.close()
            if reusable:
                with idle_lock:
                    idle[name].append(t)
            else:
                t.close()

    def report_lane_error(name, direction, error):
        # A lane error is otherwise overwritten by the next successful control
        # poll within a second, and a host that cannot install a bundle retries
        # forever in silence. Keep the latest per host and say each new one once.
        message = f'{direction} transfer: {error}'
        if lane_errors.get(name, {}).get('message') != message:
            print(f'{time.strftime("%H:%M:%S")} {name}: {message}', file=sys.stderr, flush=True)
        lane_errors[name] = {'time': time.time(), 'direction': direction, 'message': message}
        coordinator.set_lane_error(name, lane_errors[name])

    with lock(root / 'coordinator.lock'):
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=len(hosts)) as control_pool, \
                 concurrent.futures.ThreadPoolExecutor(max_workers=lane_limits['input']) as input_pool, \
                 concurrent.futures.ThreadPoolExecutor(max_workers=lane_limits['result']) as result_pool:
                while True:
                    for name, future in list(polling.items()):
                        if not future.done(): continue
                        del polling[name]
                        next_poll[name] = time.monotonic() + interval
                        try:
                            status = future.result()
                            snapshots[name] = status
                            # A fresh worker snapshot supersedes enqueue acknowledgements.
                            # If a queue was lost, redeliver the same idempotent attempt.
                            delivered.difference_update(r['id'] for r in coordinator.db.execute(
                                "SELECT id FROM attempts WHERE host=? AND state='leased'", (name,)))
                        except errors as error:
                            coordinator.host_status(name, error=str(error))
                            snapshots.pop(name, None)
                            controls[name].close()
                    for direction, pending, completed in (('input', uploading, delivered), ('result', collecting, collected)):
                        for identity, (name, future) in list(pending.items()):
                            if not future.done(): continue
                            del pending[identity]
                            try:
                                if future.result(): completed.add(identity)
                                if name in lane_errors:
                                    del lane_errors[name]
                                    coordinator.set_lane_error(name, None)
                            except errors as error:
                                coordinator.host_status(name, error=str(error))
                                report_lane_error(name, direction, error)
                                # Wait for a fresh control poll before retrying a broken lane.
                                snapshots.pop(name, None)
                    for name in configs:
                        if name not in polling and time.monotonic() >= next_poll.get(name, 0):
                            polling[name] = control_pool.submit(poll, name)
                    coordinator.expire()
                    # Transfer task queues are bounded, not just the executor thread count.
                    leased = {r['id']: r['host'] for r in coordinator.db.execute(
                        "SELECT id,host FROM attempts WHERE state='leased'")}
                    for direction, pending, completed, pool, limit in (
                        ('input', uploading, delivered, input_pool, lane_limits['input']),
                        ('result', collecting, collected, result_pool, lane_limits['result'])):
                        candidates = []
                        for name, status in snapshots.items():
                            live = {a['id']: a for a in status['attempts']}
                            if direction == 'input':
                                if collect_only or coordinator.mode != 'running' or not status['accepting']: continue
                                candidates += [(name, {'id': i}) for i, h in leased.items() if h == name and i not in live]
                            else:
                                candidates += [(name, a) for a in status['attempts']
                                               if a['experiment'] == coordinator.manifest['id'] and a['state'] == 'done']
                        # Round robin among hosts and partial large transfers; a slow
                        # artifact does not monopolize the next available slot.
                        candidates.sort(key=lambda pair: (host_serviced.get((direction, pair[0]), -1),
                                                         serviced.get((direction, pair[1]['id']), -1)))
                        for name, item in candidates:
                            identity = item['id']
                            if len(pending) >= limit: break
                            if identity in pending or identity in completed: continue
                            per_host = configs[name].get('input_slots' if direction == 'input' else 'result_slots', 2)
                            if sum(h == name for h, _ in pending.values()) >= per_host: continue
                            pending[identity] = (name, pool.submit(transfer, name, item, direction))
                            sequence += 1
                            serviced[(direction, identity)] = sequence
                            host_serviced[(direction, name)] = sequence
                    if time.monotonic() >= next_sample:
                        atomic_json(root / 'reports' / 'pipeline.json', {
                            'time': time.time(), 'control_active': len(polling),
                            'input_active': len(uploading), 'result_active': len(collecting),
                            'hosts': {name: {'slots': status['slots'], 'counts': status['counts'],
                                             'spool_bytes': status['spool_bytes'], 'accepting': status['accepting']}
                                      for name, status in snapshots.items()},
                            'lane_errors': lane_errors})
                        next_sample = time.monotonic() + 1
                    states = dict(coordinator.db.execute('SELECT state,count(*) FROM jobs GROUP BY state'))
                    if not states.get('active') and not states.get('pending') and not uploading and not collecting:
                        # Cancellation can make every job terminal just after the
                        # last running-mode poll. Drain those polls, then send the
                        # final durable control before releasing the coordinator.
                        for name, future in polling.items():
                            try:
                                future.result()
                            except errors as error:
                                coordinator.host_status(name, error=str(error))
                                controls[name].close()
                        final_polls = {name: control_pool.submit(poll, name) for name in configs}
                        for name, future in final_polls.items():
                            try:
                                future.result()
                            except errors as error:
                                coordinator.host_status(name, error=str(error))
                        break
                    time.sleep(.05)
        finally:
            for t in controls.values(): t.close()
            for sessions in idle.values():
                for t in sessions: t.close()
    return coordinator.status()
