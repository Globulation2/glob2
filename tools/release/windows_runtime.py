"""Stage the Windows runtime once for every distribution channel.

Only directly or transitively imported MinGW DLLs are copied. System DLLs stay
on Windows; missing imports fail closed instead of producing a partial package.
Extra runtime directories take precedence, allowing a private lean SDL_image.
"""

import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from tools.package_assets import ASSET_DIRS, export_assets, source_files

SYSTEM_DLLS = {
    "advapi32.dll",
    "bcrypt.dll",
    "comdlg32.dll",
    "crypt32.dll",
    "dwmapi.dll",
    "gdi32.dll",
    "imm32.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "mpr.dll",
    "msvcrt.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "opengl32.dll",
    "psapi.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "setupapi.dll",
    "shell32.dll",
    "shlwapi.dll",
    "user32.dll",
    "userenv.dll",
    "uxtheme.dll",
    "version.dll",
    "winhttp.dll",
    "winmm.dll",
    "wldap32.dll",
    "ws2_32.dll",
    "wtsapi32.dll",
}
DLL_PATTERN = re.compile(r"^\s*DLL Name:\s*(\S+\.dll)\s*$", re.I | re.M)


def imports(binary):
    output = subprocess.check_output(["objdump", "-p", str(binary)], text=True)
    return {name.lower() for name in DLL_PATTERN.findall(output)}


def system_dlls():
    root = Path(os.environ.get("WINDIR", os.environ.get("SYSTEMROOT", "C:/Windows")))
    return {
        path.name.lower()
        for path in (root / "System32").glob("*")
        if path.suffix.lower() == ".dll"
    }


def stage_dlls(executable, runtime_dirs, destination, inspect=imports):
    marker = Path(executable).resolve().parent.parent / "image-runtime.json"
    if marker.is_file():
        prefix = Path(json.loads(marker.read_text())["prefix"])
        runtime_dirs = [prefix / "bin", *runtime_dirs]
    available = {}
    for runtime in runtime_dirs:
        runtime = Path(runtime)
        if not runtime.is_dir():
            raise NotADirectoryError(runtime)
        local = {}
        for path in sorted(runtime.iterdir()):
            if path.is_file() and path.suffix.lower() == ".dll":
                name = path.name.lower()
                if name in local:
                    raise ValueError("Ambiguous case-insensitive runtime DLL: " + name)
                local[name] = path
        for name, path in local.items():
            available.setdefault(name, path)
    windows = system_dlls()
    pending, scanned = [Path(executable)], set()
    while pending:
        binary = pending.pop()
        key = binary.name.lower()
        if key in scanned:
            continue
        scanned.add(key)
        for name in sorted(inspect(binary)):
            name = name.lower()
            if (
                name in scanned
                or name in SYSTEM_DLLS
                or name in windows
                or name.startswith(("api-ms-win-", "ext-ms-win-"))
            ):
                continue
            source = available.get(name)
            if source is None:
                raise FileNotFoundError(
                    f"Required runtime DLL unavailable: {name} (from {binary.name})"
                )
            target = Path(destination) / source.name
            if not target.exists():
                shutil.copy2(source, target)
            pending.append(target)


def stage_assets(source, destination, original_assets=False):
    source, destination = Path(source), Path(destination)
    for directory in ASSET_DIRS:
        if not (source / directory).is_dir():
            raise FileNotFoundError(source / directory)
    with tempfile.TemporaryDirectory(prefix="glob2-assets-") as temporary:
        assets = Path(temporary) / "runtime"
        if original_assets:
            for path in source_files(source, "windows"):
                target = assets / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
        else:
            export_assets(source, assets, platform="windows")
        for directory in ASSET_DIRS:
            if (assets / directory).is_dir():
                shutil.copytree(assets / directory, destination / directory)
    shutil.copy2(source / "COPYING", destination / "COPYING")
    shutil.copy2(
        source / "docs/assets/source-attribution.md",
        destination / "source-attribution.md",
    )
