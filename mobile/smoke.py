#!/usr/bin/env python3
"""Exercise startup/lifecycle on a named Glob2 emulator; retain diagnostics."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.globulation.glob2'


def validate_target(serial, adb_port, avd):
    if not re.fullmatch(r'emulator-\d+', serial):
        raise ValueError('Smoke checks require an emulator serial, never a physical device')
    if adb_port == 5037 or not 1024 <= adb_port <= 65535:
        raise ValueError('Use a separate unprivileged ADB server port')
    if avd not in ('glob2-api35-arm64', 'glob2-api35-x64'):
        raise ValueError('Smoke checks require a task-owned Glob2 AVD')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--avd', required=True)
    parser.add_argument('--adb-port', type=int, default=15037)
    parser.add_argument('--output', default='build/mobile-smoke')
    args = parser.parse_args()
    validate_target(args.serial, args.adb_port, args.avd)
    output = (ROOT / args.output).resolve()
    if not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Diagnostics must stay inside the worktree build directory')
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.pop('ADB_SERVER_SOCKET', None)
    env.pop('ANDROID_SERIAL', None)
    adb = [str(ROOT/'build/mobile-tools/android-sdk/platform-tools/adb'), '-P', str(args.adb_port), '-s', args.serial]

    def run(*command, binary=False, timeout=30):
        result = subprocess.run(adb + list(command), capture_output=True, text=not binary, env=env, timeout=timeout)
        if result.returncode:
            raise RuntimeError(f'ADB {command!r} failed: {result.stderr!r}')
        return result.stdout

    if run('emu', 'avd', 'name').splitlines()[0] != args.avd:
        raise ValueError('Selected emulator belongs to another AVD')
    if run('shell', 'getprop', 'sys.boot_completed').strip() != '1':
        raise RuntimeError('Emulator has not finished booting')
    if run('shell', 'getprop', 'ro.kernel.qemu').strip() != '1':
        raise ValueError('Selected target is not an emulator')

    def logs(): return run('logcat', '-d', '-v', 'threadtime')
    def pid(): return run('shell', 'pidof', PACKAGE).strip()
    def wait_for(predicate, description, timeout=90):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate(): return
            time.sleep(1)
        raise RuntimeError('Timed out waiting for ' + description)
    def screenshot(name):
        png = run('exec-out', 'screencap', '-p', binary=True)
        if png[:8] != b'\x89PNG\r\n\x1a\n': raise RuntimeError('Screenshot is not a PNG')
        (output/(name+'.png')).write_bytes(png)
        return struct.unpack('>II', png[16:24])
    def start():
        result = run('shell', 'am', 'start', '-W', '-n', PACKAGE+'/.Glob2Activity', timeout=60)
        if 'Error:' in result or 'Status: ok' not in result:
            raise RuntimeError('Activity did not start: ' + result)
    def foreground():
        state = run('shell', 'dumpsys', 'activity', 'activities')
        return any(PACKAGE in line and ('mResumedActivity' in line or 'topResumedActivity' in line) for line in state.splitlines())

    original_rotation = run('shell', 'settings', 'get', 'system', 'user_rotation').strip()
    original_auto = run('shell', 'settings', 'get', 'system', 'accelerometer_rotation').strip()
    summary = {'serial': args.serial, 'avd': args.avd, 'checks': [], 'passed': False}
    first_log = ''
    try:
        run('shell', 'am', 'force-stop', PACKAGE)
        run('logcat', '-c')
        start()
        wait_for(lambda: 'Glob2 screen ready:' in logs() and 'MainMenuScreen' in logs(), 'native main menu and bundled assets')
        wait_for(foreground, 'foreground activity')
        first_pid = pid()
        if not first_pid: raise RuntimeError('Native process is missing')
        screenshot('startup')
        summary['checks'].append('native startup and bundled assets')
        run('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        wait_for(lambda: not foreground(), 'background transition')
        time.sleep(2)
        start(); wait_for(foreground, 'resume')
        if pid() != first_pid: raise RuntimeError('Background/resume unexpectedly restarted the process')
        screenshot('resumed')
        summary['checks'].append('retained-process background/resume')
        run('shell', 'settings', 'put', 'system', 'accelerometer_rotation', '0')
        for rotation in ('1', '0'):
            run('shell', 'settings', 'put', 'system', 'user_rotation', rotation)
            time.sleep(2)
            if pid() != first_pid or not foreground(): raise RuntimeError('Rotation lost the foreground process')
            width, height = screenshot('rotation-'+rotation)
            if (width > height) != (rotation == '1'):
                raise RuntimeError('Display did not rotate to the requested orientation')
        summary['checks'].append('landscape/portrait rotation')
        first_log = logs()
        run('shell', 'am', 'force-stop', PACKAGE)
        run('logcat', '-c')
        start()
        wait_for(lambda: 'Glob2 screen ready:' in logs() and 'MainMenuScreen' in logs(), 'fresh-process startup')
        if not pid() or pid() == first_pid: raise RuntimeError('Force-stop did not produce a fresh process')
        screenshot('relaunched')
        summary['checks'].append('force-stop and fresh-process relaunch')
        summary['passed'] = True
    finally:
        # Save diagnostics even on failure, and restore emulator settings.
        for key, value in [('user_rotation', original_rotation), ('accelerometer_rotation', original_auto)]:
            run('shell', 'settings', 'delete' if value == 'null' else 'put', 'system', key, *([] if value == 'null' else [value]))
        log = first_log + '\n' + logs()
        (output/'logcat.txt').write_text(log)
        (output/'activity.txt').write_text(run('shell', 'dumpsys', 'activity', 'activities'))
        (output/'memory.txt').write_text(run('shell', 'dumpsys', 'meminfo', PACKAGE))
        if re.search(r'ANR in org\.globulation\.glob2|Fatal signal[^\n]*org\.globulation\.glob2|Process: org\.globulation\.glob2, PID:', log):
            summary['passed'] = False
            summary['error'] = 'App crash or ANR found in logcat'
        (output/'result.json').write_text(json.dumps(summary, indent=2)+'\n')
        run('shell', 'am', 'force-stop', PACKAGE)
    if not summary['passed']: raise RuntimeError(summary.get('error', 'Smoke checks failed'))
    print('PASS ' + ', '.join(summary['checks']))


if __name__ == '__main__':
    main()
