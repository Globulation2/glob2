#!/usr/bin/env python3
"""Select CI jobs from a PR diff; retained master revisions always run full CI."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


JOBS = ("native", "browser", "map_generators", "deployment", "cross_platform")
# Implementation-only drawing changes retain native, browser, and equivalence
# checks. Shared headers, file I/O, fonts and unknown library paths stay full CI.
RENDER_IMPLEMENTATIONS = {
    "libgag/src/RenderBackend.cpp",
    "libgag/src/SoftwareRenderBackend.cpp",
    "libgag/src/SurfaceRaster.cpp",
}
CI_TOOL_TESTS = {
    "test/test_run_tests.py",
    "test/test_ci_failure_aggregation.py",
    "tests/build_system/test_ci_changed_paths.py",
    "tests/build_system/test_ci_tiers.py",
    "tests/build_system/test_ci_run_metrics.py",
}
TRANSPORT_TESTS = {
    "test/NetConnectionHarness.cpp",
    "test/NativeMultiplayerPeer.cpp",
    "test/WssTransportHarness.cpp",
    "test/WssListenerHarness.cpp",
    "test/LANDiscoveryHarness.cpp",
    "test/run-network-transport-tests.py",
}


def classify(paths):
    if not paths:
        return {job: True for job in JOBS}

    native = browser = map_generators = deployment = cross_platform = False
    for path in paths:
        # These Python suites execute directly in the selector job, without
        # compiling a client or launching platform/browser regressions.
        if path in CI_TOOL_TESTS:
            continue
        if path.startswith("docs/") or path.endswith(".md"):
            continue
        if path == "test/map-generator-golden.txt":
            map_generators = True
            continue
        if path in TRANSPORT_TESTS:
            browser = True
            continue
        if path.startswith(("test/fixtures/javascript/", "test/Script", "test/support/ScriptCorpus")) or path in {
            "test/check_javascript.py", "test/check_javascript_corpus.py", "test/check_javascript_evidence.py",
            "test/build_provenance.py", "test/support/TestMain.cpp",
            "test/ImageAssetTest.cpp",
        }:
            # These cases and fixtures are compiled/executed in the production
            # WebAssembly harness too; native-only CI would leave that boundary untested.
            native = browser = cross_platform = True
            continue
        if path.startswith("test/fixtures/multiplayer/"):
            # The committed match record is verified natively and in every browser,
            # and the comparison job requires identical traces.
            native = browser = cross_platform = True
            continue
        if path.startswith("browser/") and browser_only(path):
            browser = True
            continue
        if path.startswith("deploy/") or path.startswith("tests/deployment/"):
            browser = True
            deployment = True
            continue
        if path.startswith(("tests/transport/",)):
            browser = True
            continue
        if path.startswith("test/") and path not in {
            "test/run-browser-determinism.py",
        } and not Path(path).name.startswith("MapGenerator"):
            native = True
            continue
        if path in RENDER_IMPLEMENTATIONS or path.startswith(("src/ai/", "src/gui/", "src/render/")):
            native = browser = cross_platform = True
            continue
        if path.startswith(("src/net/", "src/yog/")):
            native = browser = deployment = cross_platform = True
            continue
        return {job: True for job in JOBS}

    return {
        "native": native,
        "browser": browser,
        "map_generators": map_generators,
        "deployment": deployment,
        "cross_platform": cross_platform,
    }


def browser_only(path):
    if path.startswith("browser/tests/fixtures/") or path == "browser/tests/determinism.spec.js":
        return False
    if path == "browser/toolchain.json":
        return False
    return Path(path).suffix in {".js", ".css", ".html", ".json", ".md"}



def coverage_profile(paths, event, selected):
    compatibility = event != 'pull_request' or not paths
    browsers_all = compatibility
    android = event != 'pull_request' or not paths
    reasons = []
    for path in paths:
        if path.startswith('docs/') or path.endswith('.md') or path in CI_TOOL_TESTS:
            continue
        if path.startswith(('src/', 'libgag/', 'libusl/', 'mobile/', 'scons/', 'data/', 'darwin/', 'windows/', 'flatpak/', 'snap/', 'fdroid/', 'fastlane/')) or path in ('SConstruct','vcpkg.json','tools/package_assets.py','tools/asset-requirements.txt','.github/workflows/mobile.yml','.github/scripts/ci_changed_paths.py','.github/scripts/ci_coverage_baseline.py'):
            android = True
        if path.startswith(('browser/', 'src/gui/', 'src/render/')) or path in RENDER_IMPLEMENTATIONS:
            browsers_all = True
        # Conservative omissions only for known test-only and implementation-only boundaries.
        primary_safe = (path.startswith('test/') and not path.startswith(('test/Script','test/fixtures/','test/support/'))
                        and path not in ('test/run-browser-determinism.py','test/check_javascript.py','test/check_javascript_corpus.py','test/check_javascript_evidence.py','test/build_provenance.py','test/ImageAssetTest.cpp'))
        primary_safe = primary_safe or path in RENDER_IMPLEMENTATIONS or (path.startswith(('src/gui/','src/render/')) and path.endswith('.cpp')) or (path.startswith('browser/') and browser_only(path)) or path.startswith(('deploy/','tests/deployment/','tests/transport/'))
        # Shard/runtime configuration changes must exercise the oldest supported
        # platform too, even though their files live under test/.
        if path.startswith('test/ci-timings/') or path in {
            'test/run_tests.py', 'test/ci_native_shard_plan.py',
            'test/ci-native-auxiliary.json', 'test/build_ci_timing_profile.py',
        }:
            primary_safe = False
        if path.endswith(('.h','.hpp','.hh')) or not primary_safe:
            compatibility = True
            if not path.startswith(('test/', 'tests/', '.github/')):
                android = True
            reasons.append('compatibility or unknown path: '+path)
    browsers_all = browsers_all or compatibility
    return {'compatibility': compatibility, 'browsers_all': browsers_all,
            'android': android, 'android_arches': ['arm64-v8a','armeabi-v7a','x86_64'] if compatibility else ['arm64-v8a'],
            'profile': 'compatibility' if compatibility else ('primary' if any(selected.values()) else 'lightweight'),
            'reasons': reasons or ['known relevant boundaries; primary platforms suffice']}

def changed_paths(base):
    subprocess.run(
        ["git", "fetch", "--no-tags", "--depth=1", "origin", base],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    diff = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", base, "HEAD"],
        check=True,
        capture_output=True,
    )
    return [os.fsdecode(path) for path in diff.stdout.split(b"\0") if path]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", help="PR base or pre-push commit; omitted for full CI")
    args = parser.parse_args()
    paths = []
    event = os.environ.get("GITHUB_EVENT_NAME", "workflow_dispatch")
    if args.base and event == "pull_request":
        try:
            paths = changed_paths(args.base)
            selected = classify(paths)
        except (OSError, subprocess.CalledProcessError) as error:
            print(f"Could not inspect changed paths ({error}); running full CI", file=sys.stderr)
            selected = {job: True for job in JOBS}
    else:
        selected = {job: True for job in JOBS}

    if os.environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch" and os.environ.get("BROWSER_ONLY") == "true":
        selected = {
            "native": False,
            "browser": True,
            "map_generators": False,
            "deployment": True,
            "cross_platform": False,
        }

    desired = coverage_profile(paths, event, selected)
    from ci_coverage_baseline import activated
    enabled = event == 'pull_request' and activated()
    effective = dict(desired)
    if not enabled:
        effective.update(compatibility=True, browsers_all=True, android_arches=['arm64-v8a','armeabi-v7a','x86_64'])
    # Mobile is independently relevant; native test-only diffs do not compile APKs.
    legacy_android = any(path.startswith(('mobile/', 'scons/', 'libgag/', 'src/', 'tests/build_system/', 'fdroid/', 'fastlane/')) or path in ('SConstruct', 'tools/package_assets.py', 'tools/asset-requirements.txt', 'test/ci_step_summary.py', '.github/workflows/mobile.yml') for path in paths if path not in CI_TOOL_TESTS)
    selected['android'] = desired['android'] or (not enabled and legacy_android)
    if event == 'workflow_dispatch' and os.environ.get('BROWSER_ONLY') == 'true':
        selected['android'] = False
    effective['android'] = selected['android']
    artifact = Path('artifacts/ci-selection.json')
    artifact.parent.mkdir(parents=True, exist_ok=True)
    artifact.write_text(json.dumps({'selection': selected, 'event': event, 'sha': os.environ.get('GITHUB_SHA'), 'full_matrix': event != 'pull_request' and all(selected.values()), 'desired': desired, 'effective': effective, 'tiers_enabled': enabled}, indent=2) + '\n')
    output = "".join(f"{job}={str(enabled).lower()}\n" for job, enabled in selected.items())
    output += 'compatibility=' + str(effective['compatibility']).lower() + '\n'
    output += 'browsers_all=' + str(effective['browsers_all']).lower() + '\n'
    output += 'android_arches=' + json.dumps(effective['android_arches'], separators=(',', ':')) + '\n'
    browser_entries = json.loads(Path(__file__).with_name('ci_browser_matrix.json').read_text())
    if not effective['browsers_all']:
        browser_entries = [entry for entry in browser_entries if entry['browsers'] == 'chromium']
    output += 'browser_matrix=' + json.dumps({'include': browser_entries}, separators=(',', ':')) + '\n'
    output += 'profile=' + desired['profile'] + '\n'
    print(json.dumps({'desired': desired, 'effective': effective, 'tiers_enabled': enabled}, indent=2))
    print(output, end="")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as destination:
            destination.write(output)


if __name__ == "__main__":
    main()
