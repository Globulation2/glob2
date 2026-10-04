"""Cross-experiment visibility and staleness auditing for worker installs on a
host. A `doctor`/`run` deployment only ever looks at the one worker directory
named in --hosts; nothing previously surfaced that other, unrelated worker
installs (other sessions' tournaments, or abandoned ones) were also present
and consuming the same cores. This module discovers every worker install
under the host's home directory by walking for the fixed
`<directory>/workers/<package_id>/{host.json,daemon.json,queue.sqlite}` layout
every install shares, regardless of which package/protocol version it runs --
it never speaks that install's RPC protocol, only reads its on-disk state, so
a version mismatch between concurrent sessions' bundles can't break audit.
"""
import json
import time
from .transport import Transport

MAX_DEPTH = 6

_DISCOVER_SCRIPT = f'''
import json, os, sqlite3, sys
home = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~')
results = []
for dirpath, dirnames, filenames in os.walk(home):
    depth = dirpath[len(home):].count(os.sep)
    if depth >= {MAX_DEPTH}:
        dirnames[:] = []
    if 'daemon.json' not in filenames or 'host.json' not in filenames:
        continue
    try:
        daemon = json.load(open(os.path.join(dirpath, 'daemon.json')))
        host = json.load(open(os.path.join(dirpath, 'host.json')))
    except (OSError, ValueError):
        continue
    pid = daemon.get('pid')
    alive = False
    if isinstance(pid, int):
        try:
            os.kill(pid, 0); alive = True
        except ProcessLookupError:
            alive = False
        except PermissionError:
            alive = True  # exists, just owned by someone else
    running = queued = None
    activity = None
    db_path = os.path.join(dirpath, 'queue.sqlite')
    if os.path.exists(db_path):
        activity = os.path.getmtime(db_path)
        try:
            conn = sqlite3.connect(db_path)
            running = conn.execute("SELECT count(*) FROM queue WHERE state='running'").fetchone()[0]
            queued = conn.execute("SELECT count(*) FROM queue WHERE state='queued'").fetchone()[0]
            conn.close()
        except sqlite3.Error:
            pass
    # dirpath is .../workers/<package_id>; its grandparent is the --hosts "directory".
    directory = os.path.dirname(os.path.dirname(dirpath))
    results.append({{'directory': directory, 'package_id': daemon.get('package_id'), 'pid': pid,
                     'alive': alive, 'slots': host.get('slots'), 'last_activity': activity,
                     'running': running, 'queued': queued}})
print(json.dumps(results))
'''

_KILL_SCRIPT = '''
import os, sys
pid = int(sys.argv[1])
try:
    os.kill(pid, 15)
    print('signalled')
except ProcessLookupError:
    print('already gone')
'''


def discover(transport, root=None):
    args = [transport.python, '-c', _DISCOVER_SCRIPT] + ([root] if root else [])
    output = transport.call(args)
    return json.loads(output)


def audit(hosts, stale_hours=24.0, root=None):
    """Read-only. Reports every worker install per host and flags reap
    candidates two ways: a dead daemon (nothing will ever pick its queue back
    up), or a daemon that's still alive but has had no running/queued work for
    stale_hours -- a coordinator that died or a session that ended without
    cancelling, leaving its worker idling indefinitely. Never modifies or
    deletes anything. `root` overrides the search root (defaults to the target
    host's home directory) -- exposed for tests.
    """
    now = time.time()
    report = []
    for config in hosts:
        transport = Transport(config, '/dev/null')
        entry = {'host': config['name'], 'own_directory': config['directory']}
        try:
            installs = discover(transport, root)
            for install in installs:
                install['own'] = install['directory'] == config['directory']
                age_hours = (now - install['last_activity']) / 3600 if install['last_activity'] else None
                install['idle_hours'] = age_hours
                idle_no_work = not install.get('running') and not install.get('queued')
                old_enough = age_hours is not None and age_hours >= stale_hours
                install['stale'] = (not install['alive']) or (idle_no_work and old_enough)
            entry['installs'] = installs
        except Exception as error:
            entry['error'] = str(error)
        report.append(entry)
    return report


def reap(host_config, directory, confirm=False, root=None):
    """Stop a specific install's daemon by exact PID (never a pattern match, to
    avoid the classic pkill-matches-its-own-invocation accident). Only stops the
    process; deleting the install's files is a separate, deliberate decision
    left to a human, never done here.
    """
    transport = Transport(host_config, '/dev/null')
    installs = discover(transport, root)
    target = next((i for i in installs if i['directory'] == directory), None)
    if target is None:
        raise ValueError(f'no worker install at {directory!r} on {host_config["name"]!r}')
    if not target['alive']:
        return {'host': host_config['name'], 'directory': directory, 'action': 'none', 'reason': 'daemon not running'}
    if not confirm:
        return {'host': host_config['name'], 'directory': directory, 'action': 'dry-run',
                'pid': target['pid'], 'note': 'pass confirm=True / --confirm to actually stop it'}
    output = transport.call([transport.python, '-c', _KILL_SCRIPT, str(target['pid'])])
    return {'host': host_config['name'], 'directory': directory, 'pid': target['pid'],
            'action': output.decode().strip()}
