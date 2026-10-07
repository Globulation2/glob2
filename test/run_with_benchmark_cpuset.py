#!/usr/bin/env python3
"""Temporarily reserve complete SMT cores in a balanced cgroup-v2 partition.

Run unprivileged after authorization. Only operations on one fresh cgroup use sudo;
existing cgroups, process affinities and controller configuration are never edited.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import select
import re
import signal
import subprocess
import sys
import time
import uuid

CGROUP_ROOT = Path('/sys/fs/cgroup')
CPU_ROOT = Path('/sys/devices/system/cpu')


def cpu_list(value):
    result = set()
    for part in value.strip().split(','):
        if not part:
            raise ValueError('empty CPU list or component')
        limits = part.split('-')
        if len(limits) not in (1, 2) or any(not v.isdigit() for v in limits):
            raise ValueError(f'invalid CPU list: {value}')
        first, last = int(limits[0]), int(limits[-1])
        if first > last:
            raise ValueError('descending CPU range')
        values = set(range(first, last + 1))
        if result & values:
            raise ValueError('duplicate CPU')
        result |= values
    return result


def format_cpus(values):
    return ','.join(map(str, sorted(values)))


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


class System:
    """Small injectable boundary for tests; real writes stay within the owned path."""
    def read(self, path):
        return path.read_text().strip()

    def mkdir(self, path):
        subprocess.run(['sudo', '-n', 'mkdir', '--', str(path)], check=True)

    def write(self, path, value):
        subprocess.run(['sudo', '-n', 'tee', str(path)], input=value + '\n', text=True,
                       stdout=subprocess.DEVNULL, check=True)

    def rmdir(self, path):
        subprocess.run(['sudo', '-n', 'rmdir', '--', str(path)], check=True)

    def exists(self, path):
        return path.exists()

    def namespace(self, pid):
        path = f'/proc/{pid}/ns/cgroup'
        try:
            value = os.readlink(path)
        except PermissionError:
            value = ''
        # Some procfs restrictions return an empty link instead of EACCES.
        # Never let two unavailable namespace identities compare equal.
        if not re.fullmatch(r'cgroup:\[\d+\]', value):
            value = subprocess.check_output(['sudo', '-n', '/usr/bin/readlink', path], text=True).strip()
        if not re.fullmatch(r'cgroup:\[\d+\]', value):
            raise RuntimeError(f'Cannot verify cgroup namespace identity for PID {pid}')
        return value


class Partition:
    def __init__(self, system, engine, reserved, path):
        self.system, self.engine, self.reserved, self.path = system, set(engine), set(reserved), path
        self.created = False
        self.before = None

    def parent(self):
        return {name: self.system.read(CGROUP_ROOT / name) for name in
                ('cgroup.subtree_control', 'cpuset.cpus.effective', 'cpuset.mems.effective')}

    def preflight(self):
        if self.path.parent != CGROUP_ROOT or not self.path.name.startswith('glob2-benchmark-'):
            raise ValueError('only a fresh direct child of the cgroup root is supported')
        if self.system.exists(self.path) or self.system.exists(CGROUP_ROOT / 'cpuset.cpus.partition'):
            raise RuntimeError('existing target or non-root cgroup mount')
        mount = [line.split() for line in self.system.read(Path('/proc/self/mountinfo')).splitlines()
                 if ' - cgroup2 ' in line and line.split()[4] == str(CGROUP_ROOT)]
        if len(mount) != 1 or mount[0][3] != '/':
            raise RuntimeError('a full cgroup-v2 root mount is required')
        namespaces = {pid: self.system.namespace(pid) for pid in ('self', '1')}
        if namespaces['self'] != namespaces['1']:
            raise RuntimeError('caller and host init cgroup namespaces differ')
        self.before = self.parent()
        if 'cpuset' not in self.before['cgroup.subtree_control'].split():
            raise RuntimeError('cpuset must already be enabled; existing controllers are never changed')
        available = cpu_list(self.before['cpuset.cpus.effective'])
        online = cpu_list(self.system.read(CPU_ROOT / 'online'))
        if not self.engine or not self.engine <= self.reserved or not self.reserved < available or not self.reserved <= online:
            raise ValueError('reserve online engine CPUs while leaving CPUs for ordinary work')
        siblings = {cpu: cpu_list(self.system.read(CPU_ROOT / f'cpu{cpu}/topology/thread_siblings_list'))
                    for cpu in self.reserved}
        if any(not group <= self.reserved for group in siblings.values()):
            raise ValueError('every reserved physical core must include all SMT siblings')
        return {'parent': self.before, 'namespaces': namespaces,
                'engine_cpus': sorted(self.engine), 'reserved_cpus': sorted(self.reserved),
                'ordinary_cpus': sorted(available - self.reserved),
                'smt_siblings': {str(k): sorted(v) for k, v in siblings.items()}}

    def create(self):
        # Only successful mkdir establishes ownership. Defer catchable signals
        # across that operation and the flag so cleanup cannot target EEXIST.
        blocked = {signal.SIGINT, signal.SIGTERM, signal.SIGHUP}
        previous = signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
        try:
            self.system.mkdir(self.path)
            self.created = True
        finally:
            signal.pthread_sigmask(signal.SIG_SETMASK, previous)
        for name, value in (
            ('cpuset.mems', self.before['cpuset.mems.effective']),
            ('cpuset.cpus', format_cpus(self.reserved)),
            ('cpuset.cpus.exclusive', format_cpus(self.reserved)),
            ('cpuset.cpus.partition', 'root'),
        ):
            self.system.write(self.path / name, value)
        return self.validate()

    def validate(self):
        values = {name: self.system.read(self.path / name) for name in
                  ('cpuset.cpus.partition', 'cpuset.cpus', 'cpuset.cpus.effective',
                   'cpuset.cpus.exclusive', 'cpuset.cpus.exclusive.effective', 'cpuset.mems.effective')}
        if values['cpuset.cpus.partition'] != 'root':
            raise RuntimeError(f'partition is not a valid balanced root: {values}')
        for name in ('cpuset.cpus', 'cpuset.cpus.effective', 'cpuset.cpus.exclusive', 'cpuset.cpus.exclusive.effective'):
            if cpu_list(values[name]) != self.reserved:
                raise RuntimeError(f'partition CPU set changed: {values}')
        parent = self.parent()
        if cpu_list(parent['cpuset.cpus.effective']) != cpu_list(self.before['cpuset.cpus.effective']) - self.reserved:
            raise RuntimeError('ordinary root partition does not have the expected complementary CPUs')
        if parent['cgroup.subtree_control'] != self.before['cgroup.subtree_control'] or parent['cpuset.mems.effective'] != self.before['cpuset.mems.effective']:
            raise RuntimeError('parent controller or memory-node configuration changed')
        if values['cpuset.mems.effective'] != self.before['cpuset.mems.effective']:
            raise RuntimeError('partition memory-node set changed')
        return values

    def pids(self):
        return [int(pid) for pid in self.system.read(self.path / 'cgroup.procs').split()]

    def populated(self):
        values = dict(line.split() for line in self.system.read(self.path / 'cgroup.events').splitlines())
        return values.get('populated') != '0'

    def remove(self):
        if not self.created or not self.system.exists(self.path):
            return
        if self.populated():
            raise RuntimeError('refusing to remove populated benchmark cgroup')
        self.system.write(self.path / 'cpuset.cpus.partition', 'member')
        self.system.rmdir(self.path)
        if self.system.exists(self.path) or self.parent() != self.before:
            raise RuntimeError('cgroup removal or original parent-state verification failed')


# No privilege change: the gate inherits the invoking UID, GID and supplementary
# groups. It cannot exec the command until the parent migrates and verifies it.
GATE = r'''
import json,os,sys
read_fd,write_fd=int(sys.argv[1]),int(sys.argv[2])
cpus={int(n) for n in sys.argv[3].split(',')}
if os.read(read_fd,1)!=b'P':raise SystemExit('gate closed before placement')
os.sched_setaffinity(0,cpus)
proof={'uid':os.getuid(),'gid':os.getgid(),'groups':os.getgroups(),
       'affinity':sorted(os.sched_getaffinity(0)),'cgroup':open('/proc/self/cgroup').read().strip()}
os.write(write_fd,json.dumps(proof).encode()+b'\n');os.close(write_fd)
if os.read(read_fd,1)!=b'R':raise SystemExit('gate closed before execution')
os.close(read_fd)
os.execvpe(sys.argv[4],sys.argv[4:],os.environ)
'''


def shutdown(partition, child, audit, grace_seconds=60):
    """Give nested governor wrappers time to restore before a last-resort kill."""
    if not partition.created or not partition.system.exists(partition.path):
        return
    if partition.populated():
        audit['graceful_shutdown_started'] = now()
        if child is not None and child.poll() is None:
            try:
                os.killpg(child.pid, signal.SIGINT)
            except ProcessLookupError:
                pass
        else:
            # Leader already exited; signal remaining owned members, never other groups.
            for pid in partition.pids():
                try:
                    os.kill(pid, signal.SIGINT)
                except ProcessLookupError:
                    pass
                except PermissionError:
                    audit.setdefault('graceful_signal_denied_pids', []).append(pid)
        deadline = time.monotonic() + grace_seconds
        while partition.populated() and time.monotonic() < deadline:
            if child is not None:
                child.poll()
            time.sleep(0.1)
        if partition.populated():
            audit['forced_cgroup_kill'] = True
            audit['nested_governor_restoration_risk'] = 'Graceful shutdown did not empty the group; forced kill may have prevented nested governor restoration. Inspect its separate audit and saved originals.'
            partition.system.write(partition.path / 'cgroup.kill', '1')
            deadline = time.monotonic() + 5
            while partition.populated() and time.monotonic() < deadline:
                if child is not None:
                    child.poll()
                time.sleep(0.1)
            if partition.populated():
                raise RuntimeError('owned cgroup remains populated after cgroup.kill')
    if child is not None:
        child.wait(timeout=5)
    partition.remove()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', required=True, type=Path)
    parser.add_argument('--cpus', default='0-7', help='Engine affinity, CPU-list syntax')
    parser.add_argument('--reserve-cpus', default='0-7,16-23', help='Exclusive CPUs including all SMT siblings')
    parser.add_argument('--timeout-seconds', type=float, default=43200)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('run unprivileged; only individual cgroup operations use sudo')
    if not math.isfinite(args.timeout_seconds) or args.timeout_seconds <= 0:
        parser.error('timeout must be positive')
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or args.audit.exists():
        parser.error('supply a command and a fresh audit path')
    partition = Partition(System(), cpu_list(args.cpus), cpu_list(args.reserve_cpus),
                          CGROUP_ROOT / ('glob2-benchmark-' + uuid.uuid4().hex))
    audit = {'started_utc': now(), 'state': 'preflight', 'command': command,
             'wrapper_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
             'uid': os.getuid(), 'gid': os.getgid(), 'groups': os.getgroups(),
             'original_affinity': sorted(os.sched_getaffinity(0)),
             'original_cgroup': Path('/proc/self/cgroup').read_text().strip(),
             'kernel': os.uname().release,
             'cgroup_path': str(partition.path), 'dry_run': args.dry_run,
             'forced_cgroup_kill': False, 'cgroup_restored': False,
             'limits': 'Exclusive scheduler CPUs do not isolate memory bandwidth, package power, interrupts, kernel activity or shared caches. Nested governors have a separate restoration audit.'}
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    def save():
        write_json(args.audit, audit)
    save()
    child = None
    descriptors = []
    old_handlers = {}
    starting_child = False
    deferred_signal = None
    def interrupted(signum, frame):
        nonlocal deferred_signal
        if starting_child:
            deferred_signal = signum
            return
        raise InterruptedError(f'received signal {signum}')
    try:
        audit['preflight'] = partition.preflight()
        save()
        if args.dry_run:
            audit.update(state='dry-run', cgroup_restored=True)
            return 0
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            old_handlers[signum] = signal.signal(signum, interrupted)
        audit['partition_before_command'] = partition.create()
        save()
        # A signal between Popen's fork and assignment must not lose the gate.
        # Defer Python exceptions here, without blocking signals in the child.
        starting_child = True
        try:
            go_read, go_write = os.pipe()
            descriptors.extend((go_read, go_write))
            proof_read, proof_write = os.pipe()
            descriptors.extend((proof_read, proof_write))
            child = subprocess.Popen([sys.executable, '-c', GATE, str(go_read), str(proof_write),
                                      format_cpus(partition.engine), *command],
                                     pass_fds=(go_read, proof_write), start_new_session=True)
            audit['child_pid'] = child.pid
        finally:
            starting_child = False
        if deferred_signal is not None:
            raise InterruptedError(f'received signal {deferred_signal} during gate startup')
        os.close(go_read); descriptors.remove(go_read)
        os.close(proof_write); descriptors.remove(proof_write)
        partition.system.write(partition.path / 'cgroup.procs', str(child.pid))
        partition.validate()
        if child.pid not in partition.pids():
            raise RuntimeError('gate was not moved into the owned partition')
        os.write(go_write, b'P')
        if not select.select([proof_read], [], [], 10)[0]:
            raise RuntimeError('unprivileged gate did not confirm placement/affinity')
        proof = json.loads(os.read(proof_read, 65536))
        if (proof['uid'] != audit['uid'] or proof['gid'] != audit['gid'] or
                sorted(proof['groups']) != sorted(audit['groups']) or
                proof['affinity'] != sorted(partition.engine) or
                proof['cgroup'] != '0::/' + partition.path.name):
            raise RuntimeError(f'gate identity/affinity/cgroup mismatch: {proof}')
        audit.update(state='running', child_identity=proof, validity_checks=0)
        save()
        os.write(go_write, b'R')
        os.close(go_write); descriptors.remove(go_write)
        deadline = time.monotonic() + args.timeout_seconds
        while True:
            audit['last_partition_check'] = partition.validate()
            audit['validity_checks'] += 1
            try:
                affinity = sorted(os.sched_getaffinity(child.pid))
            except ProcessLookupError:
                affinity = None  # Child may have exited between checks.
            if affinity is not None and affinity != sorted(partition.engine):
                raise RuntimeError(f'command leader affinity changed: {affinity}')
            audit['last_child_affinity'] = affinity
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('benchmark cpuset command timed out')
            try:
                code = child.wait(timeout=min(1.0, remaining))
                break
            except subprocess.TimeoutExpired:
                pass
        audit['partition_after_command'] = partition.validate()
        audit.update(state='completed', exit_code=code)
        return code
    except BaseException as error:
        audit.update(state='failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        for signum in old_handlers:
            signal.signal(signum, signal.SIG_IGN)
        for descriptor in descriptors:
            try:
                os.close(descriptor)
            except OSError:
                pass  # A signal may arrive just after an earlier close.
        cleanup_error = None
        try:
            shutdown(partition, child, audit)
            audit['cgroup_restored'] = not partition.system.exists(partition.path)
        except BaseException as error:
            cleanup_error = error
            audit['cleanup_error'] = f'{type(error).__name__}: {error}'
        audit['final_wrapper_affinity'] = sorted(os.sched_getaffinity(0))
        audit['wrapper_affinity_restored'] = audit['final_wrapper_affinity'] == audit['original_affinity']
        if not audit['wrapper_affinity_restored'] and cleanup_error is None:
            cleanup_error = RuntimeError('original wrapper affinity was not restored by the kernel')
            audit['cleanup_error'] = str(cleanup_error)
        if cleanup_error or audit['forced_cgroup_kill']:
            audit['state'] = 'failed'
        audit['finished_utc'] = now()
        try:
            save()
        finally:
            for signum, handler in old_handlers.items():
                signal.signal(signum, handler)
        if cleanup_error:
            raise RuntimeError('cpuset cleanup failed; inspect audit') from cleanup_error
        if audit['forced_cgroup_kill']:
            raise RuntimeError('forced cleanup used; nested governor restoration requires verification')


if __name__ == '__main__':
    raise SystemExit(main())
