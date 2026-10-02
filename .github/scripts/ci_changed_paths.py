#!/usr/bin/env python3
"""Select CI jobs from a PR or master push diff, defaulting to full CI."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


JOBS = ("native", "browser", "map_generators", "deployment", "cross_platform", "platform", "platform_stack")
# Paths whose changes rebuild and smoke-test the whole self-hosted stack
# (deploy/compose.yaml). Its images compile the engine, so other engine changes
# leave it to full CI rather than adding a second client build to every PR.
PLATFORM_STACK_PATHS = (
    "deploy/", "tests/deployment/", "src/relay/", "platform/package-lock.json",
    "platform/packages/db/migrations/", "platform/apps/api/src/main.ts",
    "platform/apps/worker/src/main.ts", "platform/apps/engine-agent/src/main.ts",
)

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

    native = browser = map_generators = deployment = cross_platform = platform = False
    platform_stack = any(path.startswith(PLATFORM_STACK_PATHS) and not path.endswith(".md") for path in paths)
    for path in paths:
        if path.startswith("docs/") or path.endswith(".md"):
            continue
        if path.startswith("platform/packages/protocol/fixtures/"):
            # Generated contract fixtures: checked by the platform job and
            # consumed by the C++ contract tests.
            native = platform = True
            continue
        if path.startswith("platform/"):
            platform = True
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
        # The match relay builds and runs in the native-programs job only.
        if path.startswith(("src/relay/", "tests/relay/", "test/relay/", "test/fixtures/relay-tickets/")):
            browser = True
            continue
        if path.startswith("test/") and path not in {
            "test/run-browser-determinism.py",
        } and not Path(path).name.startswith("MapGenerator"):
            native = True
            continue
        if path.startswith(("src/ai/", "src/gui/", "src/render/")):
            native = browser = cross_platform = True
            continue
        if path.startswith(("src/net/", "src/yog/")):
            native = browser = deployment = cross_platform = True
            continue
        return {**{job: True for job in JOBS}, "platform_stack": platform_stack}

    return {
        "native": native,
        "browser": browser,
        "map_generators": map_generators,
        "deployment": deployment,
        "cross_platform": cross_platform,
        "platform": platform,
        "platform_stack": platform_stack,
    }


def browser_only(path):
    if path.startswith("browser/tests/fixtures/") or path == "browser/tests/determinism.spec.js":
        return False
    if path == "browser/toolchain.json":
        return False
    return Path(path).suffix in {".js", ".css", ".html", ".json", ".md"}


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
    if args.base:
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
            "platform": False,
            "platform_stack": False,
        }

    output = "".join(f"{job}={str(enabled).lower()}\n" for job, enabled in selected.items())
    print(output, end="")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as destination:
            destination.write(output)


if __name__ == "__main__":
    main()
