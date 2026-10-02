#!/usr/bin/env python3
"""Bundle a MinGW client and the DLLs it actually links for a Windows ZIP."""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import sys
import zipfile
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.package_assets import export_assets


def stage(binary, root):
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
    prefix = os.environ.get("GLOB2_SDL3_PREFIX")
    prefix_bin = (str(Path(prefix).resolve() / "bin").replace("\\", "/").lower() + "/") if prefix else None
    for path in sorted(set(paths)):
        native = (subprocess.check_output(["cygpath", "-w", path], text=True).strip()
                  if path.startswith("/") else path)
        normalized = path.replace("\\", "/").lower()
        if not (Path(native).resolve().parent == binary.resolve().parent
                or (prefix_bin and normalized.startswith(prefix_bin))
                or "/mingw64/bin/" in normalized
                or "/sdl3-ci/prefix/bin/" in normalized):
            continue
        shutil.copy2(native, root / Path(native).name)
        licenses = Path(native).parent.parent / "share/licenses"
        if licenses.is_dir():
            shutil.copytree(licenses, root / "licenses", dirs_exist_ok=True)
    if not list(root.glob("SDL3*.dll")):
        raise SystemExit("no SDL3 DLLs found in executable dependencies")
    with tempfile.TemporaryDirectory(prefix="glob2-assets-") as temporary:
        assets = Path(temporary) / "runtime"
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


def package(binary, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        root = stage(binary, Path(temporary) / "Globulation2")
        with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(root.parent))
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, nargs="?", help="Windows ZIP output")
    parser.add_argument("--stage-dir", type=Path, help="Epic BuildPatchTool build root")
    parser.add_argument("--manifest", type=Path, help="hash manifest outside the build root")
    args = parser.parse_args()
    if args.stage_dir:
        if args.output or not args.manifest or args.manifest.resolve().is_relative_to(args.stage_dir.resolve()):
            parser.error("--stage-dir requires --manifest outside the stage and no ZIP output")
        print(stage(args.binary, args.stage_dir))
        write_manifest(args.stage_dir, args.manifest)
    elif args.output and not args.manifest:
        print(package(args.binary, args.output))
    else:
        parser.error("provide a ZIP output or --stage-dir and --manifest")
