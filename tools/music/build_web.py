#!/usr/bin/env python3
"""Build the website's small decoder using the game's pinned Opus/SDK sources."""

from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "scons"))
from dev_store import browser_sdk
from opus_dependencies import build

sdk = browser_sdk(root, lease=False)
cc = sdk / "upstream/emscripten"
if not (cc / "em++").exists():
    subprocess.run([sys.executable, str(root / "browser/setup.py")], check=True)
prefix = build(
    root / "build/music-web/opus", root / "build/music-web/sources", cc, jobs=4
)
out = root / "platform/apps/web/public/music"
out.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        str(cc / "em++"),
        "-std=c++20",
        "-O2",
        "-fwasm-exceptions",
        str(root / "src/audio/MusicStream.cpp"),
        str(root / "tools/music/web/exports.cpp"),
        "-I" + str(root / "src/audio"),
        "-I" + str(prefix / "include"),
        "-I" + str(prefix / "include/opus"),
        *[
            str(prefix / "lib" / ("lib" + name + ".a"))
            for name in ("opusfile", "opus", "ogg")
        ],
        "-sMODULARIZE=1",
        "-sEXPORT_ES6=1",
        "-sENVIRONMENT=worker",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sMAXIMUM_MEMORY=134217728",
        '-sEXPORTED_FUNCTIONS=["_malloc","_free"]',
        '-sEXPORTED_RUNTIME_METHODS=["HEAPU8","HEAP16"]',
        "-o",
        str(out / "decoder.js"),
    ],
    check=True,
)
