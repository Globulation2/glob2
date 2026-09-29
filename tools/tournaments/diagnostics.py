"""Best-effort postmortem diagnostics using already-installed tools."""
import hashlib
from pathlib import Path
import shutil
import subprocess


def postmortem(executable, pid, directory, enabled=False, started=None):
    directory = Path(directory)
    info = {'requested': enabled, 'stack_available': False, 'core_available': False,
            'debugger': shutil.which('gdb') or shutil.which('lldb')}
    if not enabled:
        return info
    pattern = Path('/proc/sys/kernel/core_pattern')
    if pattern.exists():
        info['core_pattern'] = pattern.read_text().strip()
    core = directory / 'core'
    coredumpctl = shutil.which('coredumpctl')
    if coredumpctl:
        try:
            # Bound lookup to this attempt; a recycled PID must not select an older core.
            import datetime
            since = datetime.datetime.fromtimestamp(started, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC') if started is not None else 'now'
            result = subprocess.run([coredumpctl, '--quiet', '--since', since, '--output', str(core), 'dump', str(pid)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            info['core_collection'] = result.stdout.decode(errors='replace')[-4096:]
        except (OSError, subprocess.TimeoutExpired) as error:
            info['core_collection'] = str(error)
    info['core_available'] = core.exists() and core.stat().st_size > 0
    if not info['core_available']:
        info['reason'] = 'no accessible core; core placement is controlled by the host OS'
        return info
    debugger = info['debugger']
    if not debugger:
        info['reason'] = 'no installed gdb/lldb'
        return info
    args = ([debugger, '-batch', '-ex', 'thread apply all bt', str(executable), str(core)]
            if Path(debugger).name == 'gdb' else [debugger, '-b', '-c', str(core), str(executable), '-o', 'bt all', '-o', 'quit'])
    try:
        result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (directory / 'stack.txt').write_bytes(result.stdout)
        info['stack_available'] = result.returncode == 0
        if info['stack_available']:
            import re
            normalized = re.sub(r'0x[0-9a-fA-F]+', 'ADDR', result.stdout.decode(errors='replace'))
            info['stack_signature'] = hashlib.sha256(normalized.encode()).hexdigest()
    except (OSError, subprocess.TimeoutExpired) as error:
        info['reason'] = str(error)
    return info
