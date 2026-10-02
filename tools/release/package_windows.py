#!/usr/bin/env python3
"""Bundle a MinGW client and the DLLs it actually links for a Windows ZIP."""

import argparse
import hashlib
import re
import shutil
import subprocess
import tempfile
import sys
import zipfile
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.package_assets import export_assets, source_files


def stage(binary, root, original_assets=False):
    if not binary.is_file():
        raise SystemExit(f"missing executable: {binary}")
    if root.exists():
        raise SystemExit(f"stage directory already exists: {root}")
    root.mkdir(parents=True)
    shutil.copy2(binary, root / "glob2.exe")
    result = subprocess.run(["ldd", str(binary)], check=True,
                            capture_output=True, text=True)
    paths = re.findall(r"(?:/[^\s()]+|[A-Za-z]:\\[^\s()]+)\.dll",
                       result.stdout, flags=re.IGNORECASE)
    dependencies = {path for path in paths if "/mingw64/bin/" in path.lower()
                    or "\\mingw64\\bin\\" in path.lower()}
    for path in sorted(dependencies):
        native = (subprocess.check_output(["cygpath", "-w", path], text=True).strip()
                  if path.startswith("/") else path)
        shutil.copy2(native, root / Path(native).name)
    if not list(root.glob("SDL2*.dll")):
        raise SystemExit("no SDL2 DLLs found in executable dependencies")
    with tempfile.TemporaryDirectory(prefix="glob2-assets-") as temporary:
        assets = Path(temporary) / "runtime"
        if original_assets:
            # Apply the shipping content policy to both sides of the comparison.
            # Only image encodings differ; build helpers never enter the baseline.
            for path in source_files(Path.cwd(), "windows"):
                target = assets / path.relative_to(Path.cwd())
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
        else:
            export_assets(Path.cwd(), assets, platform="windows")
        for directory in ("data", "maps", "campaigns", "scripts"):
            if (assets / directory).is_dir():
                shutil.copytree(assets / directory, root / directory)
    shutil.copy2("COPYING", root / "COPYING")
    return root


def write_manifest(root, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(root).as_posix()}\n"
        for path in sorted(root.rglob("*")) if path.is_file()), encoding="utf-8")


def package(binary, output, original_assets=False):
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        root = stage(binary, Path(temporary) / "Globulation2", original_assets)
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(root.parent))
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, nargs="?", help="Windows ZIP output")
    parser.add_argument("--original-assets", action="store_true", help="Size measurement baseline using original image bytes")
    parser.add_argument("--stage-dir", type=Path, help="Epic BuildPatchTool build root")
    parser.add_argument("--manifest", type=Path, help="hash manifest outside the build root")
    args = parser.parse_args()
    if args.stage_dir:
        if args.output or not args.manifest or args.manifest.resolve().is_relative_to(args.stage_dir.resolve()):
            parser.error("--stage-dir requires --manifest outside the stage and no ZIP output")
        print(stage(args.binary, args.stage_dir, args.original_assets))
        write_manifest(args.stage_dir, args.manifest)
    elif args.output and not args.manifest:
        print(package(args.binary, args.output, args.original_assets))
    else:
        parser.error("provide a ZIP output or --stage-dir and --manifest")
