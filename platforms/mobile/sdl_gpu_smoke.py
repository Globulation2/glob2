#!/usr/bin/env python3
"""SDL GPU smoke run on one explicitly selected Android emulator or device.

Launches the installed package with the SDL GPU renderer, confirms that SDL GPU
is the active backend rather than a fallback, then drives an AI demo game on a
large bundled map: panning and zooming, background/foreground (surface loss and
recovery), a low-memory event and a rotation/resize. Frame presentation times
come from SurfaceFlinger, so they are measured outside the game; each phase
reports percentiles and its worst frame, and frames over --hitch-ms are flagged.

Emulators (SwiftShader or host GPU) are a smoke test, not hardware validation.
Physical devices need --hardware; the run never uninstalls the app or clears
its data, and it restores the rotation settings it changes.
"""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'buildsys'))
from asset_bundle import verify_apk_sdl_gpu
from dev_paths import android_sdk
from smoke import wait_for_main_menu

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = 'org.globulation2.glob2'
ACTIVITY = PACKAGE + '/org.globulation2.glob2.Glob2Activity'
EXTRA = 'org.globulation2.glob2.extra.'
NO_FENCE = (0, (1 << 63) - 1)


class Adb:
    def __init__(self, sdk, port, serial):
        self.base = [str(Path(sdk) / 'platform-tools/adb'), '-P', str(port), '-s', serial]
        self.env = dict(os.environ)
        self.env.pop('ADB_SERVER_SOCKET', None)
        self.env.pop('ANDROID_SERIAL', None)

    def __call__(self, *command, binary=False, timeout=60, check=True):
        result = subprocess.run(self.base + list(command), capture_output=True, text=not binary,
                                env=self.env, timeout=timeout)
        if check and result.returncode:
            raise RuntimeError(f'ADB {command!r} failed: {result.stderr!r}')
        return result.stdout


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return None
    index = min(len(ordered) - 1, max(0, round(fraction * (len(ordered) - 1))))
    return ordered[index]


def summarize(intervals_ms, hitch_ms):
    if not intervals_ms:
        return {'frames': 0}
    return {'frames': len(intervals_ms) + 1,
            'p50_ms': round(percentile(intervals_ms, 0.50), 2),
            'p90_ms': round(percentile(intervals_ms, 0.90), 2),
            'p99_ms': round(percentile(intervals_ms, 0.99), 2),
            'max_ms': round(max(intervals_ms), 2),
            'mean_ms': round(statistics.fmean(intervals_ms), 2),
            'hitches_over_' + str(int(hitch_ms)) + 'ms': sum(1 for value in intervals_ms if value > hitch_ms),
            'worst_ms': [round(value, 1) for value in sorted(intervals_ms, reverse=True)[:5]]}


class FrameRecorder:
    """Poll SurfaceFlinger's per-layer present history (the last ~127 frames)."""

    def __init__(self, adb):
        self.adb = adb
        self.presents = set()
        self.layer = None

    def find_layer(self):
        layers = self.adb('shell', 'dumpsys', 'SurfaceFlinger', '--list').splitlines()
        # SDL renders into a SurfaceView; prefer its buffer layer over the window.
        candidates = [name.strip() for name in layers if PACKAGE in name and 'SurfaceView' in name]
        buffered = [name for name in candidates if 'BLAST' in name or 'Background' not in name]
        self.layer = (buffered or candidates or [None])[-1]
        return self.layer

    def sample(self):
        if not self.layer and not self.find_layer():
            return
        output = self.adb('shell', 'dumpsys', 'SurfaceFlinger', '--latency', self.layer, check=False)
        rows = output.splitlines()[1:]
        if not rows:
            self.find_layer()
            return
        for row in rows:
            fields = row.split()
            if len(fields) == 3:
                actual = int(fields[1])
                if actual not in NO_FENCE:
                    self.presents.add(actual)

    def record(self, seconds, action=None, interval=0.25):
        """Sample while running ``action`` (called repeatedly) for ``seconds``."""
        self.presents.clear()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if action:
                action()
            self.sample()
            time.sleep(interval)
        self.sample()
        times = sorted(self.presents)
        return [(b - a) / 1e6 for a, b in zip(times, times[1:])]


def device_identity(adb):
    prop = lambda key: adb('shell', 'getprop', key).strip()
    identity = {key: prop(key) for key in ('ro.product.manufacturer', 'ro.product.model', 'ro.build.version.release',
                                           'ro.build.version.sdk', 'ro.product.cpu.abilist', 'ro.hardware.vulkan',
                                           'ro.hardware.egl', 'ro.board.platform', 'ro.kernel.qemu')}
    identity['emulator'] = identity.pop('ro.kernel.qemu') == '1'
    flinger = adb('shell', 'dumpsys', 'SurfaceFlinger', check=False)
    gles = re.search(r'^GLES: (.+)$', flinger, re.M)
    identity['gles'] = gles.group(1).strip() if gles else ''
    # Android 10+ reports every Vulkan physical device and its API version.
    vulkan = adb('shell', 'cmd', 'gpu', 'vkjson', check=False, timeout=60)
    try:
        start = vulkan.index('{')
        devices = json.loads(vulkan[start:]).get('devices', [])
        identity['vulkan'] = []
        for device in devices:
            properties = device.get('properties', {})
            version = properties.get('apiVersion', 0)
            identity['vulkan'].append({'device': properties.get('deviceName'),
                                       'api': f'{version >> 22}.{(version >> 12) & 0x3ff}.{version & 0xfff}',
                                       'driver_version': properties.get('driverVersion'),
                                       'vendor_id': properties.get('vendorID')})
    except (ValueError, AttributeError):
        identity['vulkan'] = []
        identity['vulkan_report'] = vulkan.strip()[:400]
    return identity


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--serial', required=True, help='Exact adb serial; never chosen automatically')
    parser.add_argument('--adb-port', type=int, default=5037, help='ADB server port (15037 for emulator.py)')
    parser.add_argument('--android-sdk', help='Explicit pinned SDK (default: managed mobile tools)')
    parser.add_argument('--hardware', action='store_true', help='Allow a physical device')
    parser.add_argument('--renderer', default='sdl-gpu', choices=('sdl-gpu', 'programmable-gl', 'portable-sdl'),
                        help='Backend to select and require (other values give comparison baselines)')
    parser.add_argument('--map', default='Oazis', help='Bundled map for the AI demo game')
    parser.add_argument('--hitch-ms', type=float, default=50.0)
    parser.add_argument('--max-hitches', type=int, help='Fail when more frames than this exceed --hitch-ms')
    parser.add_argument('--phase-seconds', type=float, default=8.0)
    parser.add_argument('--skip-package-check', action='store_true', help='Do not pull the APK to check its shaders')
    parser.add_argument('--output', default='build/mobile-smoke-sdl-gpu')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9._:-]+', args.serial):
        raise ValueError('Invalid adb serial')
    if not re.fullmatch(r'[A-Za-z0-9_]{1,64}', args.map):
        raise ValueError('Map must be a bundled map name such as Oazis')
    output = (ROOT / args.output).resolve()
    if not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Diagnostics must stay inside the worktree build directory')
    output.mkdir(parents=True, exist_ok=True)
    adb = Adb(android_sdk(ROOT, args.android_sdk), args.adb_port, args.serial)
    if adb('get-state').strip() != 'device':
        raise RuntimeError('Selected device is not authorized')
    identity = device_identity(adb)
    if not identity['emulator'] and not args.hardware:
        raise ValueError('Selected target is a physical device; pass --hardware to validate it')
    summary = {'serial': args.serial, 'renderer': args.renderer, 'map': args.map,
               'classification': 'emulator smoke (not hardware validation)' if identity['emulator'] else 'hardware',
               'device': identity, 'checks': [], 'phases': {}, 'notes': [], 'passed': False}
    print(json.dumps(identity, indent=2))

    def write():
        (output / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')

    if not adb('shell', 'pm', 'path', PACKAGE).strip():
        raise RuntimeError(PACKAGE + ' is not installed; build and install it with platforms/mobile/android.py')
    if not args.skip_package_check:
        with tempfile.TemporaryDirectory() as directory:
            remote = adb('shell', 'pm', 'path', PACKAGE).splitlines()[0].removeprefix('package:').strip()
            local = Path(directory) / 'base.apk'
            adb('pull', remote, str(local), timeout=300)
            summary['packaged_vulkan_shaders'] = verify_apk_sdl_gpu(local)
        summary['checks'].append('installed APK contains verified SDL GPU SPIR-V')

    def logs():
        return adb('logcat', '-d', '-v', 'threadtime', timeout=60)

    def pid():
        return adb('shell', 'pidof', PACKAGE, check=False).strip()

    def foreground():
        state = adb('shell', 'dumpsys', 'activity', 'activities')
        return any(PACKAGE in line and ('mResumedActivity' in line or 'topResumedActivity' in line)
                   for line in state.splitlines())

    def wait_for(predicate, description, timeout=120):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(1)
        raise RuntimeError('Timed out waiting for ' + description)

    def start(*extras):
        result = adb('shell', 'am', 'start', '-W', '-n', ACTIVITY, *extras, timeout=90)
        if 'Error:' in result:
            raise RuntimeError('Activity did not start: ' + result)

    def renderer_lines():
        return re.findall(r'Glob2 renderer active: ([^\r\n]+)', logs())

    def require_renderer(context):
        lines = renderer_lines()
        if not lines:
            raise RuntimeError('No renderer activation logged ' + context)
        active = lines[-1].split()[0]
        summary.setdefault('renderer_log', []).append({'context': context, 'line': lines[-1]})
        if active != args.renderer:
            raise RuntimeError(f'{args.renderer} fell back to {active} {context}: {lines[-1]}')
        return lines[-1]

    def screenshot(name):
        png = adb('exec-out', 'screencap', '-p', binary=True)
        if png[:8] == b'\x89PNG\r\n\x1a\n':
            (output / (name + '.png')).write_bytes(png)

    frames = FrameRecorder(adb)
    all_intervals = []

    def phase(name, seconds, action=None):
        intervals = frames.record(seconds, action)
        all_intervals.extend(intervals)
        summary['phases'][name] = summarize(intervals, args.hitch_ms)
        summary['phases'][name]['layer'] = frames.layer
        print(name, json.dumps(summary['phases'][name]), flush=True)
        if len(intervals) < 2:
            raise RuntimeError('No frames presented during ' + name)
        return intervals

    rotation = adb('shell', 'settings', 'get', 'system', 'user_rotation').strip()
    auto_rotation = adb('shell', 'settings', 'get', 'system', 'accelerometer_rotation').strip()
    renderer_extra = ['--es', EXTRA + 'RENDERER', args.renderer]
    try:
        # 1. Menu startup: assets installed, first-run screens done, backend confirmed.
        adb('shell', 'am', 'force-stop', PACKAGE)
        adb('logcat', '-c')
        start(*renderer_extra)
        wait_for_main_menu(lambda *command, **kw: adb(*command, **kw), logs,
                           lambda predicate, description: wait_for(predicate, description, 180))
        summary['menu_renderer'] = require_renderer('at menu startup')
        screenshot('menu')
        summary['checks'].append('menu startup on ' + args.renderer + ' without fallback')

        # 2. AI demo game on a large map: the scripted game view.
        adb('shell', 'am', 'force-stop', PACKAGE)
        adb('logcat', '-c')
        start(*renderer_extra, '--es', EXTRA + 'DEMO_MAP', args.map, '--ei', EXTRA + 'DEMO_TICKS', '200000')
        wait_for(lambda: renderer_lines() and pid(), 'demo game renderer activation', 180)
        wait_for(foreground, 'foreground demo game')
        game_pid = pid()
        time.sleep(10)  # map load and first HD requests
        summary['game_renderer'] = require_renderer('in the demo game')
        size = re.search(r'(\d+)x(\d+)', adb('shell', 'wm', 'size'))
        width, height = (int(size.group(1)), int(size.group(2))) if size else (1080, 1920)
        screenshot('game')
        phase('game idle', args.phase_seconds)
        swipes = iter([(0.7, 0.5, 0.3, 0.5), (0.5, 0.7, 0.5, 0.3), (0.3, 0.5, 0.7, 0.5), (0.5, 0.3, 0.5, 0.7)] * 50)

        def pan():
            x1, y1, x2, y2 = next(swipes)
            adb('shell', 'input', 'swipe', str(int(x1 * width)), str(int(y1 * height)),
                str(int(x2 * width)), str(int(y2 * height)), '250')
        phase('pan', args.phase_seconds, pan)
        zoom_steps = iter([1, 1, 1, -1, -1, -1] * 50)

        def zoom():
            adb('shell', 'input', 'mouse', 'scroll', str(width // 2), str(height // 2),
                '--axis', 'VSCROLL,' + str(next(zoom_steps)), check=False)
        phase('zoom', args.phase_seconds, zoom)
        screenshot('after-pan-zoom')
        summary['checks'].append('pan and zoom over ' + args.map)

        # 3. Background/foreground: Android destroys the surface and recreates it.
        adb('logcat', '-c')
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        wait_for(lambda: not foreground(), 'background transition')
        time.sleep(3)
        start()
        wait_for(foreground, 'resume')
        if pid() != game_pid:
            raise RuntimeError('Background/resume restarted the process')
        phase('resume', args.phase_seconds)
        resumed = logs()
        summary['resume_log'] = [line for line in resumed.splitlines()
                                 if re.search(r'Graphics device recovery|Glob2 renderer active|SDL GPU|surface', line, re.I)][-20:]
        if renderer_lines():
            require_renderer('after resume')
        screenshot('resumed')
        summary['checks'].append('background/foreground surface loss and recovery')

        # 4. Low memory: SDL turns onTrimMemory into SDL_EVENT_LOW_MEMORY.
        adb('shell', 'am', 'send-trim-memory', PACKAGE, 'RUNNING_CRITICAL', check=False)
        phase('low memory', args.phase_seconds, pan)
        if pid() != game_pid:
            raise RuntimeError('Low-memory event killed the process')
        summary['checks'].append('low-memory event')

        # 5. Rotation: a resize of the presentation surface.
        adb('shell', 'settings', 'put', 'system', 'accelerometer_rotation', '0')
        rotations = iter(['1', '0'])

        def rotate():
            value = next(rotations, None)
            if value is not None:
                adb('shell', 'settings', 'put', 'system', 'user_rotation', value)
        phase('rotation', max(args.phase_seconds, 8.0), rotate)
        if pid() != game_pid or not foreground():
            raise RuntimeError('Rotation lost the foreground process')
        if renderer_lines():
            require_renderer('after rotation')
        screenshot('rotated')
        summary['checks'].append('rotation/resize')
        summary['overall'] = summarize(all_intervals, args.hitch_ms)
        hitches = summary['overall'].get('hitches_over_' + str(int(args.hitch_ms)) + 'ms', 0)
        if args.max_hitches is not None and hitches > args.max_hitches:
            raise RuntimeError(f'{hitches} frames exceeded {args.hitch_ms} ms')
        summary['passed'] = True
    except Exception as failure:
        summary['error'] = str(failure)
        screenshot('failure')
        raise
    finally:
        for key, value in (('user_rotation', rotation), ('accelerometer_rotation', auto_rotation)):
            adb('shell', 'settings', 'delete' if value == 'null' else 'put', 'system', key,
                *([] if value == 'null' else [value]), check=False)
        log = logs()
        (output / 'logcat.txt').write_text(log)
        (output / 'memory.txt').write_text(adb('shell', 'dumpsys', 'meminfo', PACKAGE, check=False))
        escaped = re.escape(PACKAGE)
        if re.search(r'ANR in ' + escaped + r'|Fatal signal[^\n]*' + escaped + r'|Process: ' + escaped + r', PID:', log):
            summary['passed'] = False
            summary['error'] = 'App crash or ANR found in logcat'
        summary.setdefault('overall', summarize(all_intervals, args.hitch_ms))
        write()
        adb('shell', 'am', 'force-stop', PACKAGE, check=False)
    if not summary['passed']:
        raise RuntimeError(summary.get('error', 'SDL GPU smoke failed'))
    print('PASS ' + ', '.join(summary['checks']))
    print('Frames:', json.dumps(summary['overall']))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
