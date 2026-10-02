#!/usr/bin/env python3
"""Bundle a MinGW client and the DLLs it actually links for a Windows ZIP."""

import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
import sys
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.release.windows_runtime import stage_assets, stage_dlls
from tools.release.archives import zip_archive


def stage(binary, root, original_assets=False, runtime=None):
    if not binary.is_file():
        raise SystemExit(f"missing executable: {binary}")
    if root.exists():
        raise SystemExit(f"stage directory already exists: {root}")
    root.mkdir(parents=True)
    shutil.copy2(binary, root / "glob2.exe")
    if runtime is None:
        # MinGW Python's prefix is a native Windows path; cygpath handles an
        # alternate interpreter without parsing ldd's whitespace-delimited text.
        runtime = Path(sys.prefix) / "bin"
        if not (runtime / "SDL2.dll").is_file():
            runtime = Path(
                subprocess.check_output(
                    ["cygpath", "-w", "/mingw64/bin"], text=True
                ).strip()
            )
    stage_dlls(binary, [runtime], root)
    stage_assets(Path.cwd(), root, original_assets)
    return root


def write_manifest(root, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root).as_posix()}\n"
            for path in sorted(root.rglob("*"))
            if path.is_file()
        ),
        encoding="utf-8",
    )


def package(binary, output, original_assets=False, compression_report=None):
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        root = stage(binary, Path(temporary) / "Globulation2", original_assets)
        result = zip_archive(root, output)
        if compression_report:
            compression_report.parent.mkdir(parents=True, exist_ok=True)
            compression_report.write_text(json.dumps(result, indent=2) + "\n")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, nargs="?", help="Windows ZIP output")
    parser.add_argument(
        "--compression-report",
        type=Path,
        help="Archive comparison evidence outside the ZIP",
    )
    parser.add_argument(
        "--original-assets",
        action="store_true",
        help="Size measurement baseline using original image bytes",
    )
    parser.add_argument("--stage-dir", type=Path, help="Epic BuildPatchTool build root")
    parser.add_argument(
        "--manifest", type=Path, help="hash manifest outside the build root"
    )
    args = parser.parse_args()
    if args.stage_dir:
        if (
            args.output
            or args.compression_report
            or not args.manifest
            or args.manifest.resolve().is_relative_to(args.stage_dir.resolve())
        ):
            parser.error(
                "--stage-dir requires --manifest outside the stage and no ZIP output"
            )
        print(stage(args.binary, args.stage_dir, args.original_assets))
        write_manifest(args.stage_dir, args.manifest)
    elif args.output and not args.manifest:
        print(
            package(
                args.binary, args.output, args.original_assets, args.compression_report
            )
        )
    else:
        parser.error("provide a ZIP output or --stage-dir and --manifest")
