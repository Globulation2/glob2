"""Independent bounded control, input and result lanes for continuous execution."""
import concurrent.futures
import subprocess
import time
from .common import atomic_json, lock
from .coordinator import Coordinator
from .transport import Transport


def run(coordinator, hosts, collect_only=False):
    configs = {h['name']: h for h in hosts}
    if len(configs) != len(hosts) or not hosts:
        raise ValueError('provide distinct, nonempty hosts')
    for h in hosts:
        for key in ('input_slots', 'result_slots'):
            if type(h.get(key, 2)) is not int or h.get(key, 2) < 1:
                raise ValueError(key + ' must be a positive integer')
    root = coordinator.root
    controls = {name: Transport(h, root / 'worker.pyz') for name, h in configs.items()}
    snapshots, next_poll = {}, {}
    polling, uploading, collecting = {}, {}, {}
    delivered, collected = set(), set()
    serviced, sequence = {}, 0
    next_sample = 0
    interval = min(coordinator.settings.get('poll_seconds', 1), coordinator.settings['heartbeat_seconds'])
    errors = (OSError, ValueError, subprocess.TimeoutExpired)

    def poll(name):
        c = Coordinator(root)
        try:
            return c.poll_host(configs[name], controls[name], collect_only)
        finally:
            c.close()

    def transfer(name, item, direction):
        c = Coordinator(root)
        t = Transport(configs[name], root / 'worker.pyz')
        # A successful control poll has already deployed/configured this package.
        t.deployed = True
        try:
            return (c.deliver_attempt(t, item['id']) if direction == 'input'
                    else c.collect_attempt(t, item))
        finally:
            t.close()
            c.close()

    with lock(root / 'coordinator.lock'):
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=len(hosts)) as control_pool, \
                 concurrent.futures.ThreadPoolExecutor(max_workers=coordinator.settings.get('input_transfer_slots', 8)) as input_pool, \
                 concurrent.futures.ThreadPoolExecutor(max_workers=coordinator.settings.get('result_transfer_slots', 8)) as result_pool:
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
                    for pending, completed in ((uploading, delivered), (collecting, collected)):
                        for identity, (name, future) in list(pending.items()):
                            if not future.done(): continue
                            del pending[identity]
                            try:
                                if future.result(): completed.add(identity)
                            except errors as error:
                                coordinator.host_status(name, error=str(error))
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
                        ('input', uploading, delivered, input_pool, coordinator.settings.get('input_transfer_slots', 8)),
                        ('result', collecting, collected, result_pool, coordinator.settings.get('result_transfer_slots', 8))):
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
                        candidates.sort(key=lambda pair: serviced.get((direction, pair[1]['id']), -1))
                        for name, item in candidates:
                            identity = item['id']
                            if len(pending) >= limit: break
                            if identity in pending or identity in completed: continue
                            per_host = configs[name].get('input_slots' if direction == 'input' else 'result_slots', 2)
                            if sum(h == name for h, _ in pending.values()) >= per_host: continue
                            pending[identity] = (name, pool.submit(transfer, name, item, direction))
                            sequence += 1
                            serviced[(direction, identity)] = sequence
                    if time.monotonic() >= next_sample:
                        atomic_json(root / 'reports' / 'pipeline.json', {
                            'time': time.time(), 'control_active': len(polling),
                            'input_active': len(uploading), 'result_active': len(collecting),
                            'hosts': {name: {'slots': status['slots'], 'counts': status['counts'],
                                             'spool_bytes': status['spool_bytes'], 'accepting': status['accepting']}
                                      for name, status in snapshots.items()}})
                        next_sample = time.monotonic() + 1
                    states = dict(coordinator.db.execute('SELECT state,count(*) FROM jobs GROUP BY state'))
                    if not states.get('active') and not states.get('pending') and not uploading and not collecting:
                        break
                    time.sleep(.05)
        finally:
            for t in controls.values(): t.close()
    return coordinator.status()
