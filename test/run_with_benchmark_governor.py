"""Run an unprivileged benchmark with explicitly selected, temporary CPU governors.

Use only after authorization to change the selected CPU policies. This wrapper
does not pin the command: include taskset when affinity control is required.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


CPU_ROOT = Path('/sys/devices/system/cpu')


def policies(cpus):
    result = {}
    cpus = set(cpus)
    for cpu in sorted(cpus):
        policy = (CPU_ROOT / f'cpu{cpu}/cpufreq').resolve()
        if policy.parent != CPU_ROOT / 'cpufreq':
            raise RuntimeError(f'unexpected CPU policy path: {policy}')
        related = {int(value) for value in (policy / 'related_cpus').read_text().split()}
        if not related or cpu not in related or not related.issubset(cpus):
            raise RuntimeError(f'policy extends beyond authorized CPUs: {policy}: {related}')
        if policy in result:
            continue
        available = (policy / 'scaling_available_governors').read_text().split()
        if 'performance' not in available:
            raise RuntimeError(f'performance governor unavailable: {policy}')
        result[policy] = (policy / 'scaling_governor').read_text().strip()
    return result


def snapshot(original):
    return {str(p): {name: (p / name).read_text().strip()
                    for name in ('scaling_governor', 'scaling_cur_freq',
                                 'scaling_min_freq', 'scaling_max_freq', 'related_cpus')
                    if (p / name).exists()} for p in original}


def set_governor(policy, value):
    target = policy / 'scaling_governor'
    subprocess.run(['sudo', '-n', 'tee', str(target)], input=value + '\n',
                   text=True, stdout=subprocess.DEVNULL, check=True)
    actual = target.read_text().strip()
    if actual != value:
        raise RuntimeError(f'{target}: wanted {value}, observed {actual}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', required=True, type=Path)
    parser.add_argument('--cpus', default=list(range(8)), type=int, nargs='+',
                        help='Authorized CPU IDs (default: 0-7); every shared policy must remain within this set')
    parser.add_argument('--timeout-seconds', type=int, default=43200)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('run this wrapper unprivileged; only individual sysfs writes use sudo')
    if args.timeout_seconds <= 0:
        parser.error('timeout must be positive')
    if any(cpu < 0 for cpu in args.cpus):
        parser.error('CPU IDs must be nonnegative')
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('an unprivileged benchmark command is required after --')
    if args.audit.exists():
        parser.error('audit path exists; retain prior evidence and choose a fresh path')
    original = policies(args.cpus)
    audit = dict(started_utc=now(), command=command, uid=os.getuid(),
                 wrapper_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                 selected_cpus=sorted(set(args.cpus)),
                 original_governors={str(p): v for p, v in original.items()},
                 before=snapshot(original), dry_run=args.dry_run, state='prepared')
    args.audit.parent.mkdir(parents=True, exist_ok=True)

    def save():
        temporary = args.audit.with_suffix(args.audit.suffix + '.tmp')
        temporary.write_text(json.dumps(audit, indent=2) + '\n')
        temporary.replace(args.audit)

    save()
    if args.dry_run:
        return 0
    child = None
    touched = []
    old_handlers = {}

    def interrupted(signum, _frame):
        raise InterruptedError(f'received signal {signum}')

    try:
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            old_handlers[signum] = signal.signal(signum, interrupted)
        for policy in original:
            # Include a policy even if tee succeeds but a subsequent verification fails.
            touched.append(policy)
            set_governor(policy, 'performance')
        audit.update(state='running', stabilized=snapshot(original))
        save()
        # Only the sysfs writes are privileged. Preserve benchmark uid and environment.
        child = subprocess.Popen(command, start_new_session=True)
        audit['child_pid'] = child.pid
        save()
        code = child.wait(timeout=args.timeout_seconds)
        audit.update(state='completed', exit_code=code)
        return code
    except BaseException as error:
        audit.update(state='failed', error=f'{type(error).__name__}: {error}')
        raise
    finally:
        # Keep cleanup running if a second interrupt arrives.
        for signum in old_handlers:
            signal.signal(signum, signal.SIG_IGN)
        try:
            if child is not None:
                # The driver can exit before its engine descendants. Check the
                # entire owned session's process group, not only its leader.
                def group_alive():
                    child.poll()  # Reap the leader if it has exited.
                    try:
                        os.killpg(child.pid, 0)
                    except ProcessLookupError:
                        return False
                    return True

                for sig, wait in ((signal.SIGINT, 15), (signal.SIGTERM, 10), (signal.SIGKILL, 2)):
                    if not group_alive():
                        break
                    try:
                        os.killpg(child.pid, sig)
                    except ProcessLookupError:
                        break
                    deadline = time.monotonic() + wait
                    while group_alive() and time.monotonic() < deadline:
                        time.sleep(0.1)
                if group_alive():
                    audit['child_cleanup_error'] = 'owned process group remains after SIGKILL (possibly zombies)'
        except BaseException as error:
            audit['child_cleanup_error'] = str(error)
        try:
            audit['before_restore'] = snapshot(original)
        except Exception as error:
            audit['before_restore_snapshot_error'] = str(error)
        errors = []
        for policy in reversed(touched):
            try:
                set_governor(policy, original[policy])
            except Exception as error:
                errors.append(f'{policy}: {error}')
        audit.update(restoration_errors=errors, finished_utc=now(), restored=False)
        try:
            audit['after_restore'] = snapshot(original)
            audit['restored'] = not errors and all(
                (p / 'scaling_governor').read_text().strip() == value
                for p, value in original.items())
        except Exception as error:
            audit['restore_verification_error'] = str(error)
        try:
            save()
        finally:
            for signum, handler in old_handlers.items():
                signal.signal(signum, handler)
        if not audit['restored']:
            raise RuntimeError(f'Governor restoration failed; inspect {args.audit}: {errors}')
        if audit.get('child_cleanup_error'):
            raise RuntimeError(f'Benchmark child cleanup failed; inspect {args.audit}')


if __name__ == '__main__':
    sys.exit(main())
