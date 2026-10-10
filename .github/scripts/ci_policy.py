"""Development coverage policy. Labels only add work; unknown inputs stay full."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ('native', 'browser', 'map_generators', 'deployment', 'cross_platform',
         'android', 'windows', 'compatibility', 'variants', 'coverage', 'tsan', 'macos', 'platform',
         'platform_stack', 'music')
# Cheap jobs that run on pull requests without `ci:run`, like the selector's own
# contracts: they do not build the game and gate no engine verification.
# Music tooling has inexpensive unit tests; deployed processor and shared WASM
# inputs also select their affected runtime coverage when requested.
MUSIC_TOOL_PATHS = ('tools/music/',)
MUSIC_SHARED_PATHS = ('tools/music/web/', 'tools/music/build_web.py')
MUSIC_PROCESSOR_PATHS = ('tools/music/glob2music/studio/','tools/music/glob2music/community.py', 'tools/encode_music.py')
CHEAP_PATHS = {'music': MUSIC_TOOL_PATHS}
CHEAP_FLAGS = tuple(CHEAP_PATHS)
LABELS = {'ci:run', 'ci:full', 'ci:windows', 'ci:android', 'ci:browsers'}
SIMULATION = ('src/ai/', 'src/unit/', 'src/building/', 'src/team/', 'src/map/',
              'src/scripting/sgsl/', 'src/engine/sim/', 'src/game/orders/')
PRESENTATION = ('src/hud/', 'src/render/', 'src/unit/render/', 'src/building/hud/')
# Code inside the directories above that belongs to neither class: every check.
UNCLASSIFIED = ('data/buildings/', 'src/render/torus/', 'src/render/clouds/', 'src/render/overlay/', 'src/unit/types/',
                'src/building/types/', 'src/team/stats/', 'src/map/preview/', 'src/map/tools/',
                'src/net/lan/screens/')
UNCLASSIFIED_FILES = {'libgag/include/RenderFramePacer.h', 'src/team/BaseTeam.cpp', 'src/map/MapTiling.cpp', 'src/map/FertilityCalculator.cpp',
                      'src/map/Brush.cpp', 'src/map/BrushCoverage.cpp', 'src/unit/UnitDisplayNames.cpp',
                      'src/net/ConnectionOverlay.cpp', 'src/net/turn/TurnMatchPresenter.cpp',
                      'src/map/editor/screens/EditorMainMenu.cpp'}
# Tests sit beside the code they test and are told apart by name.
TEST_SOURCE = re.compile(r'(Test|Harness|Benchmark|Fixture)\.(cpp|mm|py)$')
TEST_SOURCE_NAMES = {'RuntimePackCheck.cpp', 'MaximaStrategyDump.cpp', 'source_contracts.py', 'MapGeneratorStudy.cpp',
                     'RecordingMultiplayerPeer.cpp', 'OnlineProbeFileManager.cpp', 'PlatformClientProbe.cpp',
                     'OnlineScreensProbe.cpp', 'RelayTestMain.cpp', 'ResourceGrowthFixtures.cpp'}
# Build-system and service suites: unknown to the selector, so every check.
TOOLING_TESTS = ('test/build_system/', 'test/baselines/', 'test/relay_service/', 'test/online_service/')
TEST_ROOTS = ('src/', 'libgag/', 'libusl/', 'natsort/', 'mobile/')
SIMULATION_FILES = {'src/game/Game_sync.cpp', 'src/game/Game.cpp', 'src/engine/EngineRun.cpp',
                    'src/engine/Engine.cpp', 'src/replay/ReplayReader.cpp', 'src/replay/ReplayWriter.cpp'}
THREAD_FILES = {'src/map/ResourceGrowth.cpp', 'src/map/ResourceGrowth.h', 'src/game/diagnostics/GameDiagnostics.cpp', 'src/engine/Engine.cpp', 'src/engine/EngineRun.cpp', 'src/game/screens/GameSessionScreen.cpp',
                'src/hud/draw/GameGUIDraw.cpp', 'src/hud/GameGUIStep.cpp', 'src/hud/GameGUIOrders.cpp',
                'libgag/src/PerformanceTelemetry.cpp', 'libgag/src/AssetLoader.cpp',
                'libgag/include/AssetLoader.h', 'libgag/src/SpriteLoad.cpp',
                'src/audio/SoundMixer.cpp', 'src/audio/MusicBuffer.h', 'browser/Audio.cpp'}
# Paths whose changes rebuild and smoke-test the whole self-hosted stack
# (deploy/compose.yaml). Its images compile the engine, so engine changes that
# do not otherwise select every check skip it rather than adding a second
# client build to every PR.
PLATFORM_STACK_PATHS = (
    'deploy/', 'test/deployment/', 'src/relay/', 'platform/package-lock.json',
    'platform/packages/db/migrations/', 'platform/apps/api/src/main.ts',
    'platform/apps/worker/src/main.ts', 'platform/apps/engine-agent/src/main.ts',
    'platform/apps/music-worker/', 'platform/apps/ai-music-worker/',
)
TRANSPORT_HARNESSES = {'NetConnectionHarness.cpp', 'NativeMultiplayerPeer.cpp', 'WssTransportHarness.cpp',
                       'WssListenerHarness.cpp', 'LANDiscoveryHarness.cpp', 'run-network-transport-tests.py'}
RENDER = {'libgag/src/RenderBackend.cpp', 'libgag/src/SoftwareRenderBackend.cpp',
          'libgag/src/SurfaceRaster.cpp'}


# The mirror-only deployment of app.glob2online.com (deploy-online.yml runs
# nothing in this repository) and its scripts, which no image build or CI job runs.
# Their tests run with the rest of test/deployment whenever those are selected.
MIRROR_DEPLOY_FILES = {'.github/workflows/deploy-online.yml', 'deploy/online-deploy.sh',
                       'deploy/online_remote.py', 'test/deployment/test_online_deploy.py'}


def music_set_path(path):
    """A file inside one soundtrack set directory, data/zik/<set>/...: game data that
    the native and browser packaging checks cover (data/zik/SConscript stays shared)."""
    return path.startswith('data/zik/') and path.count('/') >= 3


def is_test_source(path):
    """A test translation unit or script living beside production code."""
    name = Path(path).name
    return path.startswith(TEST_ROOTS) and (bool(TEST_SOURCE.search(name)) or name in TEST_SOURCE_NAMES)


def unclassified(path):
    return path.startswith(UNCLASSIFIED) or path in UNCLASSIFIED_FILES


def cheap_path(path):
    return (path.startswith(('docs/', 'test/build_system/test_ci', 'fdroid/', 'fastlane/'))
            or path.endswith('.md') or path in MIRROR_DEPLOY_FILES or path in {
                'INSTALL', 'AUTHORS', 'tools/README', 'debian/README.Debian', 'debian/README.source',
                'requirements-dev.txt', 'test/test_run_tests.py', 'test/test_ci_failure_aggregation.py',
                'tools/check_docs.py', 'tools/docs/navigation.json', 'tools/docs/requirements.txt', 'test/test_check_docs.py',
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
        if path.startswith('platform/'):
            # The TypeScript platform has its own job. Generated contract fixtures
            # are also consumed by the C++ contract tests.
            if path.startswith('platform/packages/protocol/fixtures/'):
                add(path, 'native', 'platform')
            else:
                add(path, 'platform')
            continue
        if path.startswith(MUSIC_SHARED_PATHS):
            add(path, *FLAGS)
            continue
        if path.startswith(MUSIC_PROCESSOR_PATHS):
            add(path, 'music', 'platform', 'platform_stack')
            continue
        if path.startswith(MUSIC_TOOL_PATHS):
            add(path, 'music')
            continue
        if music_set_path(path):
            add(path, 'native', 'browser')
            continue
        if path == 'test/map-generator-golden.txt':
            add(path, 'map_generators', 'compatibility')
        elif path.endswith(('.h', '.hpp', '.hh')):
            add(path, *FLAGS)
        elif path.startswith('src/app/cli/') or path in {'src/app/Glob2.cpp', 'src/app/GlobalContainerArgs.cpp', 'tools/cli_reference.py', 'test/test_cli_smoke.py'}:
            add(path, 'native', 'browser', 'windows', 'android', 'macos', 'compatibility', 'cross_platform', 'platform', 'deployment')
        elif is_test_source(path):
            name = Path(path).name
            if name.startswith('Hive'):
                # Sandbox, scheduling and order boundaries span every client target.
                add(path, *FLAGS)
            elif name in TRANSPORT_HARNESSES:
                add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform', 'deployment')
            elif name.startswith('Script'):
                add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform')
            elif name.startswith('MapGenerator'):
                add(path, 'native', 'map_generators', 'compatibility')
            else:
                add(path, 'native')
        elif path.startswith(('scons/', 'libusl/', 'data/terrain/', 'data/resources/')) or path in {'SConstruct', 'vcpkg.json', 'libgag/include/AudioFormat.h', 'tools/image_encoding.json', 'tools/terrain_tileset.py', 'tools/test_terrain_tileset.py'} or unclassified(path):
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
        elif path.startswith('src/hive/'):
            add(path, *FLAGS)
        elif path.startswith(('src/net/', 'src/yog/', 'test/transport/')) or Path(path).name in TRANSPORT_HARNESSES:
            add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform', 'deployment')
        elif path.startswith(('deploy/', 'test/deployment/')):
            add(path, 'browser', 'deployment')
        elif path.startswith('browser/'):
            if path == 'browser/toolchain.json':
                add(path, *FLAGS)
            elif path == 'browser/tests/determinism.spec.js' or path.startswith('browser/tests/fixtures/'):
                add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform')
            else:
                add(path, 'browser')
        elif path.startswith(PRESENTATION) or path in RENDER or path.startswith('src/') and 'Screen' in Path(path).name:
            add(path, 'native', 'browser', 'android')
        elif path.startswith(SIMULATION) or path in SIMULATION_FILES:
            add(path, 'native', 'browser', 'windows', 'compatibility', 'cross_platform')
            if path.startswith('src/map/generator/') or Path(path).name.startswith('MapGenerator'):
                add(path, 'map_generators')
        elif path.startswith(TOOLING_TESTS):
            add(path, *FLAGS)
        elif path.startswith('test/') and Path(path).suffix in {'.cpp', '.py'}:
            add(path, 'native')
        else:
            add(path, *FLAGS)
        if path.startswith(('src/engine/sim/', 'src/render/scene/', 'src/ai/engine/', 'src/ai/observation/')) and not is_test_source(path) or path in THREAD_FILES:
            add(path, 'tsan')
    # The stack's own inputs run the stack smoke; shared and unknown paths
    # above already select every check, the stack included.
    for path in paths:
        if path.startswith(PLATFORM_STACK_PATHS) and not cheap_path(path) and not is_test_source(path):
            add(path, 'platform_stack')
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
    presentation = any(p.startswith(('browser/',) + PRESENTATION) or p in RENDER or 'Screen' in Path(p).name for p in paths)
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
                          f'xvfb-run -a npx playwright test determinism.spec.js --project={browser}'))
            for browser in ('chromium', 'firefox', 'webkit')]
