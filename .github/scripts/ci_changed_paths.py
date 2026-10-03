#!/usr/bin/env python3
"""Select CI jobs from a PR diff; master checks cover cumulative changes since a successful checkpoint."""

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
    "tests/build_system/test_ci_concurrency.py",
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
    from ci_policy import FLAGS, full, select, browser_matrix, fingerprint
    from ci_coverage_baseline import activated, master_checkpoint
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", help="PR base; master uses a successful ancestor checkpoint")
    args = parser.parse_args()
    event = os.environ.get("GITHUB_EVENT_NAME", "workflow_dispatch")
    payload = {}
    if os.environ.get('GITHUB_EVENT_PATH'):
        payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    labels = [label['name'] for label in payload.get('pull_request', {}).get('labels', [])]
    draft = event == 'pull_request' and (os.environ.get('DRAFT') == 'true' or
            payload.get('pull_request', {}).get('draft') is True) and 'ci:run' not in labels
    enabled = False if draft else event in ('pull_request', 'push') and activated()
    paths, known, checkpoint = [], False, None
    base = args.base if event == 'pull_request' else None
    if event == 'push' and enabled:
        checkpoint = master_checkpoint(os.environ['GITHUB_REPOSITORY'], os.environ['GH_TOKEN'])
        base = checkpoint
    if base and event in ('pull_request', 'push'):
        try:
            paths = changed_paths(base)
            known = True
        except (OSError, subprocess.CalledProcessError) as error:
            print(f"Could not inspect changed paths ({error}); running full CI", file=sys.stderr)
    desired_selection, reasons = select(paths, labels, known)
    complete = event in ('schedule', 'workflow_dispatch') or 'ci:full' in labels
    if complete:
        desired_selection = full()
    if os.environ.get('CI_CALLED_FULL') == 'true':
        complete = True
        desired_selection = full()
    desired = dict(desired_selection, reasons=reasons,
                   profile='compatibility' if desired_selection['coverage'] else 'primary')
    if enabled or complete:
        selected = dict(desired_selection)
    else:
        # Preserve the existing job family selection until a new full hosted
        # baseline validates the changed policy. Cheap docs/contracts stay cheap.
        legacy = classify(paths) if known else {job: True for job in JOBS}
        selected = dict(desired_selection)
        selected.update(legacy)
        selected.update(compatibility=legacy['native'] or legacy['map_generators'], variants=legacy['native'],
                        coverage=legacy['native'], windows=legacy['native'], macos=False,
                        android=coverage_profile(paths, event, legacy)['android'])
    if draft:
        selected = {flag: False for flag in FLAGS}
    browser_only = event == 'workflow_dispatch' and os.environ.get('BROWSER_ONLY') == 'true'
    if browser_only:
        selected = {flag: flag in ('browser', 'deployment') for flag in FLAGS}
    full_matrix = not draft and not browser_only and all(selected.values())
    # Unknown/shared boundaries request the complete development matrix.
    exhaustive = selected['coverage'] or complete or (not enabled and not draft)
    entries = browser_matrix(selected, paths, exhaustive or 'ci:browsers' in labels)
    arches = ['arm64-v8a', 'armeabi-v7a', 'x86_64'] if exhaustive else ['arm64-v8a', 'x86_64']
    effective = dict(selected, browsers_all=bool(entries and {e['browsers'] for e in entries} == {'chromium','firefox','webkit'}), android_arches=arches)
    policy = fingerprint()
    inventory = {'jobs': selected, 'native_secondary': 'full' if exhaustive else 'compatibility',
                 'browsers': entries, 'android_arches': arches if selected['android'] else []}
    import hashlib
    inventory_hash = hashlib.sha256(json.dumps(inventory, sort_keys=True).encode()).hexdigest()
    observed_sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip() if os.environ.get('CI_CALLED_FULL') == 'true' else os.environ.get('GITHUB_SHA')
    observation = dict(schema=2, selection=selected, event=event, sha=observed_sha,
                       full_matrix=full_matrix, desired=desired, effective=effective, tiers_enabled=enabled,
                       draft=draft, paths=paths, checkpoint=checkpoint, policy_fingerprint=policy,
                       inventory=inventory, inventory_fingerprint=inventory_hash,
                       checkpoint_eligible=event in ('push','schedule','workflow_dispatch') and not browser_only and os.environ.get('CI_CALLED_FULL') != 'true')
    artifact = Path('artifacts/ci-selection.json')
    artifact.parent.mkdir(parents=True, exist_ok=True)
    artifact.write_text(json.dumps(observation, indent=2) + '\n')
    outputs = dict(selected, browsers_all=effective['browsers_all'],
                   android_arches=json.dumps(arches, separators=(',', ':')),
                   browser_matrix=json.dumps({'include': entries}, separators=(',', ':')),
                   secondary_profile='full' if exhaustive or 'ci:windows' in labels or any(p.startswith(('windows/', 'darwin/')) for p in paths) else 'compatibility', draft=draft,
                   full_matrix=full_matrix, inventory_fingerprint=inventory_hash)
    output = ''.join(f'{key}={str(value).lower() if isinstance(value,bool) else value}\n' for key,value in outputs.items())
    print(json.dumps(observation, indent=2))
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as target:
            target.write(output)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a') as target:
            target.write('## Selected development checks\n')
            if draft:
                target.write('Draft: expensive verification deferred; mark ready or add `ci:run`.\n')
            target.write('```json\n' + json.dumps(observation, indent=2) + '\n```\n')


if __name__ == "__main__":
    main()
