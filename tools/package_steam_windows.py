#!/usr/bin/env python3
"""Stage a portable Windows client directory for a Steam content depot."""

import argparse
import hashlib
import shutil
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.release.windows_runtime import ASSET_DIRS as ASSET_DIRS, imports, stage_assets, stage_dlls


def stage(source: Path, executable: Path, runtime: Path, output: Path) -> None:
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    if not executable.is_file():
        raise FileNotFoundError(executable)
    if not runtime.is_dir():
        raise NotADirectoryError(runtime)

    output.mkdir(parents=True)
    shutil.copy2(executable, output / "glob2.exe")
    stage_assets(source, output)
    stage_dlls(executable, [runtime], output, inspect=imports)

    manifest = output / "SHA256SUMS.txt"
    with manifest.open("w", encoding="utf-8", newline="\n") as stream:
        for path in sorted(output.rglob("*")):
            if path.is_file() and path != manifest:
                digest = hashlib.sha256()
                with path.open("rb") as content:
                    for chunk in iter(lambda: content.read(1024 * 1024), b""):
                        digest.update(chunk)
                stream.write(f"{digest.hexdigest()}  {path.relative_to(output).as_posix()}\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stage(args.source.resolve(), args.exe.resolve(), args.runtime.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
