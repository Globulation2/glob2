#!/usr/bin/env python3
"""Check a task-owned iOS simulator's native startup and process lifecycle."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.globulation.glob2'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True)
    parser.add_argument('--simulator-set', default='build/mobile-tools/ios-simulators')
    parser.add_argument('--output', default='build/mobile-smoke-ios')
    parser.add_argument('--command-timeout', type=int, default=180, help='Bound simctl commands on slower hosted simulators')
    args = parser.parse_args()
    device_set = (ROOT / args.simulator_set).resolve()
    output = (ROOT / args.output).resolve()
    if not device_set.is_relative_to(ROOT/'build') or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Simulator data and diagnostics must stay inside this worktree build directory')
    if not 30 <= args.command_timeout <= 300: raise ValueError('Command timeout must be between 30 and 300 seconds')
    command = ['xcrun', 'simctl', '--set', str(device_set)]
    commands = []
    def run(*arguments, timeout=None):
        began = time.monotonic()
        success = False
        print('simctl ' + ' '.join(arguments), flush=True)
        try:
            result = subprocess.check_output(command + list(arguments), text=True, stderr=subprocess.STDOUT, timeout=args.command_timeout if timeout is None else timeout)
            success = True
            return result
        finally:
            commands.append({'arguments': list(arguments), 'seconds': round(time.monotonic() - began, 3), 'success': success})
    devices = json.loads(run('list', 'devices', '--json'))['devices']
    selected = [device for group in devices.values() for device in group if device['udid'] == args.device]
    if len(selected) != 1 or selected[0]['name'] != 'Glob2-Mobile' or selected[0]['state'] != 'Booted':
        raise ValueError('Select the booted Glob2-Mobile simulator in the isolated device set')
    output.mkdir(parents=True, exist_ok=True)
    for label in ('startup', 'resumed', 'relaunched'):
        for channel in ('stdout', 'stderr'):
            (output/(label+'-'+channel+'.txt')).unlink(missing_ok=True)
    summary = {'device': args.device, 'checks': [], 'passed': False}
    started_packages = set()
    def start(label, redirect=True):
        stdout, stderr = output/(label+'-stdout.txt'), output/(label+'-stderr.txt')
        options = ['--stdout='+str(stdout), '--stderr='+str(stderr)] if redirect else []
        result = run('launch', *options, args.device, PACKAGE)
        match = re.search(r': (\d+)\s*$', result)
        if not match: raise RuntimeError('Missing native process ID: ' + result)
        started_packages.add(PACKAGE)
        return match.group(1), (stdout, stderr)
    def ready(paths):
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            log = ''.join(path.read_text(errors='replace') for path in paths if path.exists())
            if 'Glob2 screen ready:' in log and 'MainMenuScreen' in log: return
            time.sleep(1)
        raise RuntimeError('Native main menu did not become ready')
    def screenshot(label, timeout=None): run('io', args.device, 'screenshot', str(output/(label+'.png')), timeout=timeout)
    try:
        # Fresh CI simulators have no app process. Avoid asking simctl to
        # terminate an absent process (some hosted CoreSimulator versions hang).
        services = run('spawn', args.device, 'launchctl', 'list')
        if any(PACKAGE in line and line.split()[0].isdigit() for line in services.splitlines()):
            run('terminate', args.device, PACKAGE)
        first, paths = start('startup'); ready(paths); screenshot('startup')
        summary['checks'].append('native startup and bundled assets')
        run('launch', args.device, 'com.apple.Preferences')
        started_packages.add('com.apple.Preferences')
        time.sleep(2)
        # A retained process keeps its original output handles. Do not ask
        # simctl to redirect them again while merely activating the app.
        resumed, _ = start('resumed', redirect=False)
        if resumed != first: raise RuntimeError('Background/resume restarted the native process')
        time.sleep(1); screenshot('resumed')
        summary['checks'].append('retained-process background/resume')
        run('terminate', args.device, PACKAGE)
        second, paths = start('relaunched'); ready(paths)
        if first == second: raise RuntimeError('Termination did not produce a fresh process')
        screenshot('relaunched')
        summary['checks'].append('termination and fresh-process relaunch')
        summary['passed'] = True
    except Exception as failure:
        summary['error'] = str(failure)
        # Keep the original failure. These are best-effort diagnostics from the
        # explicitly selected simulator, with short independent deadlines.
        for name, arguments in (
            ('failure-services.txt', ('spawn', args.device, 'launchctl', 'list')),
            ('failure-system.log', ('spawn', args.device, 'log', 'show', '--last', '3m', '--style', 'compact',
                                    '--predicate', 'process == "Glob2" OR eventMessage CONTAINS "org.globulation.glob2"')),
        ):
            try: (output/name).write_text(run(*arguments, timeout=30)[-2*1024*1024:])
            except Exception as diagnostic:
                summary.setdefault('diagnostic_errors', []).append(name + ': ' + str(diagnostic))
        try: screenshot('failure', timeout=30)
        except Exception: pass
        raise
    finally:
        (output/'devices.json').write_text(json.dumps(devices, indent=2)+'\n')
        log = '\n'.join(path.read_text(errors='replace') for path in output.glob('*-stderr.txt'))
        if re.search(r'Terminating app due to uncaught exception|Fatal signal|Assertion failed', log):
            summary['passed'] = False
            summary['error'] = 'Native crash found in simulator output'
        (output/'result.json').write_text(json.dumps(summary, indent=2)+'\n')
        # Do not terminate never-started apps: absent-app termination can itself
        # hang in CoreSimulator. CI shuts down this whole private set afterward.
        for package in sorted(started_packages):
            try:
                run('terminate', args.device, package, timeout=30)
            except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
                # Preserve the original failure and diagnostics; CI shuts down
                # this entire private simulator after the smoke command.
                pass
        (output/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    if not summary['passed']: raise RuntimeError(summary.get('error', 'iOS smoke failed'))
    print('PASS ' + ', '.join(summary['checks']))


if __name__ == '__main__': main()
