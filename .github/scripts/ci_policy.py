"""Development coverage policy. Labels only add work; unknown inputs stay full."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ('native', 'browser', 'map_generators', 'deployment', 'cross_platform',
         'android', 'windows', 'compatibility', 'variants', 'coverage', 'tsan', 'macos')
LABELS = {'ci:run', 'ci:full', 'ci:windows', 'ci:android', 'ci:browsers'}
SIMULATION = ('src/ai/', 'src/unit/', 'src/building/', 'src/team/', 'src/map/',
              'src/sgsl/', 'src/sim/', 'src/Order')
SIMULATION_FILES = {'src/Game_sync.cpp', 'src/Game.cpp', 'src/EngineRun.cpp',
                    'src/Engine.cpp', 'src/ReplayReader.cpp', 'src/ReplayWriter.cpp'}
THREAD_FILES = {'src/Engine.cpp', 'src/EngineRun.cpp', 'src/GameSessionScreen.cpp',
                'src/gui/GameGUIDraw.cpp', 'src/gui/GameGUIStep.cpp', 'src/gui/GameGUIOrders.cpp',
                'libgag/src/PerformanceTelemetry.cpp'}
RENDER = {'libgag/src/RenderBackend.cpp', 'libgag/src/SoftwareRenderBackend.cpp',
          'libgag/src/SurfaceRaster.cpp'}


def cheap_path(path):
    return (path.startswith(('docs/', 'tests/build_system/test_ci', 'fdroid/', 'fastlane/'))
            or path.endswith('.md') or path in {
                'test/test_run_tests.py', 'test/test_ci_failure_aggregation.py',
                'tools/package_steam_windows.py', 'test/test_steam_windows_package.py',
                'mobile/android_release.py', '.github/workflows/steam-windows-package.yml',
                '.github/workflows/mac-app-store.yml'})


def fingerprint():
    # Coverage evidence from a different selector/workflow cannot activate reductions.
    paths = sorted(list((ROOT / '.github/workflows').glob('*.yml')) +
                   list((ROOT / '.github/scripts').glob('ci_*.py')) +
                   [ROOT / '.github/scripts/ci_browser_matrix.json', ROOT / 'test/ci-compatibility.json',
                    ROOT / 'test/run_tests.py', ROOT / 'test/ci_native_shard_plan.py',
                    ROOT / 'test/ci-native-auxiliary.json'] + list((ROOT / '.github/actions').rglob('*.yml')))
    digest = hashlib.sha256()
    for path in paths:
        digest.update(str(path.relative_to(ROOT)).encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def full():
    return {flag: True for flag in FLAGS}


def select(paths, labels=(), known=False):
    result = {flag: False for flag in FLAGS}
    reasons = []
    def add(path, *flags):
        for flag in flags:
            result[flag] = True
        reasons.append({'path': path, 'checks': list(flags)})
    if not known:
        return full(), [{'path': None, 'checks': list(FLAGS), 'reason': 'diff unavailable'}]
    for path in paths:
        if cheap_path(path):
            continue
        if path == 'test/map-generator-golden.txt':
            add(path, 'map_generators', 'compatibility')
        elif path.endswith(('.h', '.hpp', '.hh')) or path.startswith(('scons/', 'libusl/')) or path in {'SConstruct', 'vcpkg.json'}:
            add(path, *FLAGS)
        elif path.startswith(('test/fixtures/', 'test/support/', '.github/')) or path in {
            'test/run_tests.py', 'test/ci_native_shard_plan.py', 'test/ci-native-auxiliary.json',
            'test/ci-compatibility.json', 'test/build_ci_timing_profile.py', 'test/run-browser-determinism.py',
            'test/check_javascript.py', 'test/check_javascript_corpus.py', 'test/check_javascript_evidence.py',
            'test/build_provenance.py'} or path.startswith('test/ci-timings/'):
            add(path, *FLAGS)
        elif path.startswith(('mobile/',)):
            add(path, 'native', 'android')
        elif path.startswith('darwin/'):
            add(path, 'native', 'macos')
        elif path.startswith('windows/'):
            add(path, 'native', 'windows')
        elif path.startswith(('src/net/', 'src/yog/', 'tests/transport/')) or Path(path).name in {
            'NetConnectionHarness.cpp', 'NativeMultiplayerPeer.cpp', 'WssTransportHarness.cpp',
            'WssListenerHarness.cpp', 'LANDiscoveryHarness.cpp', 'run-network-transport-tests.py'}:
            add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform', 'deployment')
        elif path.startswith(('deploy/', 'tests/deployment/')):
            add(path, 'browser', 'deployment')
        elif path.startswith('browser/'):
            if path == 'browser/toolchain.json':
                add(path, *FLAGS)
            elif path == 'browser/tests/determinism.spec.js' or path.startswith('browser/tests/fixtures/'):
                add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform')
            else:
                add(path, 'browser')
        elif path.startswith(('src/gui/', 'src/render/', 'src/scene/')) or path in RENDER or path.startswith('src/') and 'Screen' in Path(path).name:
            add(path, 'native', 'browser', 'android')
        elif path.startswith(SIMULATION) or path in SIMULATION_FILES or path.startswith('test/Script'):
            add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform')
            if path.startswith('src/map/generator/') or Path(path).name.startswith('MapGenerator'):
                add(path, 'map_generators')
        elif path.startswith('test/') and Path(path).name.startswith('MapGenerator'):
            add(path, 'native', 'map_generators', 'compatibility')
        elif path.startswith('test/') and Path(path).suffix in {'.cpp', '.py'}:
            add(path, 'native')
        else:
            add(path, *FLAGS)
        if path.startswith(('src/sim/', 'src/scene/')) or path in THREAD_FILES:
            add(path, 'tsan')
    labels = set(labels) & LABELS
    if 'ci:full' in labels:
        result = full()
    if 'ci:windows' in labels:
        result.update(native=True, windows=True)
    if 'ci:android' in labels:
        result['android'] = True
    if 'ci:browsers' in labels:
        result['browser'] = True
    for label in sorted(labels - {'ci:run'}):
        reasons.append({'label': label, 'reason': 'explicit expansion'})
    return result, reasons


def browser_matrix(selected, paths, complete=False):
    entries = json.loads((ROOT / '.github/scripts/ci_browser_matrix.json').read_text())
    if not selected['browser']:
        return []
    presentation = any(p.startswith(('browser/', 'src/gui/', 'src/render/', 'src/scene/')) or p in RENDER or 'Screen' in Path(p).name for p in paths)
    if complete:
        return entries
    if presentation:
        if selected['cross_platform']:
            return entries
        import copy
        entries = copy.deepcopy([e for e in entries if 'scripting' not in e['name'] and 'WSS and simulation' not in e['name']])
        for entry in entries:
            if entry['name'].startswith('chromium ') and '/5' in entry['name']:
                entry['command'] = entry['command'].replace("--grep-invert='", "--grep-invert='determinism\\.spec\\.js|multiplayer\\.spec\\.js|")
        return entries
    # Simulation/network changes require real engines in every browser, but do
    # not repeat unrelated presentation/storage test files.
    return [dict(name=f'{browser} compatibility', browsers=browser,
                 command=(('GLOB2_FIREFOX_HEADED=1 ' if browser == 'firefox' else '') +
                          f'xvfb-run -a npx playwright test determinism.spec.js' + (' multiplayer.spec.js' if selected['deployment'] else '') + f' --project={browser}'))
            for browser in ('chromium', 'firefox', 'webkit')]
