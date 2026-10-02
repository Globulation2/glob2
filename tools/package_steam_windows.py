#!/usr/bin/env python3
"""Stage a portable Windows client directory for a Steam content depot."""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import deque
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.package_assets import export_assets

ASSET_DIRS = ("data", "maps", "campaigns", "scripts")
SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "comdlg32.dll", "crypt32.dll",
    "dwmapi.dll", "gdi32.dll", "imm32.dll", "iphlpapi.dll",
    "kernel32.dll", "msvcrt.dll", "ntdll.dll", "ole32.dll",
    "oleaut32.dll", "opengl32.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "userenv.dll", "uxtheme.dll", "version.dll", "winhttp.dll",
    "winmm.dll", "ws2_32.dll", "wtsapi32.dll",
}
DLL_PATTERN = re.compile(r"^\s*DLL Name:\s*(\S+\.dll)\s*$", re.IGNORECASE | re.MULTILINE)


def imports(binary: Path) -> set[str]:
    result = subprocess.run(
        ["objdump", "-p", str(binary)], check=True, capture_output=True, text=True
    )
    return {name.lower() for name in DLL_PATTERN.findall(result.stdout)}


def is_system_dll(name: str, windows_dlls: set[str]) -> bool:
    return (name in SYSTEM_DLLS or name in windows_dlls or
            name.startswith(("api-ms-win-", "ext-ms-win-")))


def stage(source: Path, executable: Path, runtime: Path, output: Path, sdl_runtime: Path | None = None) -> None:
    if output.exists():
        raise ValueError(f"Output already exists: {output}")
    if not executable.is_file():
        raise FileNotFoundError(executable)
    if not runtime.is_dir():
        raise NotADirectoryError(runtime)

    output.mkdir(parents=True)
    shutil.copy2(executable, output / "glob2.exe")
    for directory in ASSET_DIRS:
        if not (source / directory).is_dir():
            raise FileNotFoundError(source / directory)
    with tempfile.TemporaryDirectory(prefix="glob2-assets-") as temporary:
        assets = Path(temporary) / "runtime"
        export_assets(source, assets, platform="windows")
        for directory in ASSET_DIRS:
            if (assets / directory).is_dir():
                shutil.copytree(assets / directory, output / directory)
    shutil.copy2(source / "COPYING", output / "COPYING")
    attribution = source / "docs/assets/source-attribution.md"
    shutil.copy2(attribution, output / "source-attribution.md")

    available = {path.name.lower(): path for path in runtime.glob("*.dll")}
    if sdl_runtime:
        available.update({path.name.lower(): path for path in sdl_runtime.glob('*.dll')})
        licenses = sdl_runtime.parent / 'share/licenses'
        if licenses.is_dir():
            shutil.copytree(licenses, output / 'licenses', dirs_exist_ok=True)
    windows_root = Path(os.environ.get("WINDIR", "")) / "System32"
    windows_dlls = ({path.name.lower() for path in windows_root.glob("*.dll")}
                    if windows_root.is_dir() else set())
    pending = deque([output / "glob2.exe"])
    scanned = set()
    while pending:
        binary = pending.popleft()
        if binary.name.lower() in scanned:
            continue
        scanned.add(binary.name.lower())
        for name in sorted(imports(binary)):
            if is_system_dll(name, windows_dlls) or name in scanned:
                continue
            dll = available.get(name)
            if dll is None:
                raise FileNotFoundError(f"Required runtime DLL unavailable: {name} (from {binary.name})")
            staged = output / dll.name
            if not staged.exists():
                shutil.copy2(dll, staged)
            pending.append(staged)

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
    parser.add_argument("--sdl-runtime", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stage(args.source.resolve(), args.exe.resolve(), args.runtime.resolve(), args.output.resolve(), args.sdl_runtime.resolve() if args.sdl_runtime else None)


if __name__ == "__main__":
    main()
