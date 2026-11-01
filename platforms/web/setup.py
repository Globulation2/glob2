#!/usr/bin/env python3
"""Install the pinned Emscripten SDK once per host/version."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scons"))
from dev_store import Lease, browser_sdk, isolated

root = Path(__file__).resolve().parents[1]
lock = json.loads((root / "browser/toolchain.json").read_text())
sdk = browser_sdk(root, lease=False)
sdk.parent.mkdir(parents=True, exist_ok=True)
guard = Lease(sdk, exclusive=True)
with guard:
    marker = sdk / ".glob2-toolchain.json"
    if marker.is_file() and json.loads(marker.read_text()) == lock:
        print(sdk)
    else:
        if sdk.exists():
            revision = subprocess.check_output(
                ["git", "-C", str(sdk), "rev-parse", "HEAD"], text=True
            ).strip()
            if not isolated() and revision != lock["emsdk_commit"]:
                raise ValueError("Unverified shared Emscripten revision: " + str(sdk))
            destination = sdk
            staging = None
        else:
            staging = tempfile.TemporaryDirectory(dir=sdk.parent, prefix=".install-")
            destination = Path(staging.name) / "emsdk"
            subprocess.run(
                [
                    "git",
                    "clone",
                    "https://github.com/emscripten-core/emsdk.git",
                    str(destination),
                ],
                check=True,
            )
        try:
            subprocess.run(
                [
                    "git",
                    "-C",
                    str(destination),
                    "checkout",
                    "--detach",
                    lock["emsdk_commit"],
                ],
                check=True,
            )
            for action in ("install", "activate"):
                subprocess.run(
                    [str(destination / "emsdk"), action, lock["emscripten"]], check=True
                )
            # emsdk activation embeds absolute installation paths; activate again after publication.
            if staging:
                destination.rename(sdk)
                subprocess.run(
                    [str(sdk / "emsdk"), "activate", lock["emscripten"]], check=True
                )
            marker.write_text(json.dumps(lock, sort_keys=True) + "\n")
        finally:
            if staging:
                staging.cleanup()
        print(sdk)
