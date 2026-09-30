"""Stage the existing MinGW build as a self-contained GDK PC game."""

import argparse
import os
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image


ASSET_DIRS = ("data", "maps", "campaigns", "scripts")
SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "comdlg32.dll", "crypt32.dll",
    "dwmapi.dll", "gdi32.dll", "imm32.dll", "iphlpapi.dll",
    "kernel32.dll", "mpr.dll", "msvcrt.dll", "ntdll.dll",
    "ole32.dll", "oleaut32.dll", "opengl32.dll", "psapi.dll",
    "rpcrt4.dll", "secur32.dll", "setupapi.dll", "shell32.dll",
    "shlwapi.dll", "user32.dll", "uxtheme.dll", "version.dll",
    "winmm.dll", "ws2_32.dll", "wldap32.dll",
}
DLL_PATTERN = re.compile(r"^\s*DLL Name:\s*(\S+)\s*$", re.MULTILINE)


def stage_dlls(executable: Path, dll_dir: Path, destination: Path) -> None:
    pending = [executable]
    visited = set()
    available = {path.name.lower(): path for path in dll_dir.glob("*.dll")}
    while pending:
        binary = pending.pop()
        imports = DLL_PATTERN.findall(
            subprocess.check_output(["objdump", "-p", str(binary)], text=True)
        )
        for name in imports:
            key = name.lower()
            if key in visited:
                continue
            visited.add(key)
            if key.startswith(("api-ms-win-", "ext-ms-win-")) or key in SYSTEM_DLLS:
                continue
            source = available.get(key)
            if source is None:
                system_root = Path(os.environ.get("SYSTEMROOT", "C:/Windows"))
                if (system_root / "System32" / name).is_file():
                    continue
                raise RuntimeError(f"Unresolved non-system DLL: {name} (imported by {binary})")
            shutil.copy2(source, destination / source.name)
            pending.append(source)


def write_game_config(destination: Path, args: argparse.Namespace) -> None:
    game = ET.Element("Game", {"configVersion": "1"})
    ET.SubElement(game, "Identity", {
        "Name": args.identity_name,
        "Publisher": args.publisher,
        "Version": args.version,
    })
    ET.SubElement(game, "StoreId").text = args.store_id
    resources = ET.SubElement(game, "Resources")
    ET.SubElement(resources, "Resource", {"Language": "en-US"})
    executables = ET.SubElement(game, "ExecutableList")
    ET.SubElement(executables, "Executable", {
        "Name": "glob2.exe", "Id": "Game", "TargetDeviceFamily": "PC",
    })
    ET.SubElement(game, "ShellVisuals", {
        "DefaultDisplayName": "Globulation 2",
        "PublisherDisplayName": args.publisher_display_name,
        "Description": "Globulation 2 real-time strategy game",
        "BackgroundColor": "#17305A",
        "StoreLogo": "StoreLogo.png",
        "Square150x150Logo": "Logo150.png",
        "Square44x44Logo": "Logo44.png",
        "Square480x480Logo": "Logo480.png",
    })
    ET.indent(game)
    ET.ElementTree(game).write(destination / "MicrosoftGame.config", encoding="utf-8", xml_declaration=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--dll-dir", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--identity-name", required=True)
    parser.add_argument("--publisher", required=True)
    parser.add_argument("--publisher-display-name", required=True)
    parser.add_argument("--store-id", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    if (not re.fullmatch(r"[1-9]\d*(?:\.\d+){3}", args.version)
            or any(int(part) > 65535 for part in args.version.split("."))):
        parser.error("--version must have four parts of 0..65535 with a nonzero major version")
    if not re.fullmatch(r"[A-Z0-9]{12}", args.store_id):
        parser.error("--store-id must be the 12-character Partner Center Store ID")
    if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        parser.error("--commit must be the full Git commit SHA")
    if not args.exe.is_file():
        parser.error(f"missing Windows executable: {args.exe}")
    if not args.dll_dir.is_dir():
        parser.error(f"missing MinGW DLL directory: {args.dll_dir}")

    destination = args.dest.resolve()
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    shutil.copy2(args.exe, destination / "glob2.exe")
    for folder in ASSET_DIRS:
        source = args.root / folder
        if not source.is_dir():
            parser.error(f"missing game asset directory: {source}")
        shutil.copytree(source, destination / folder, ignore=shutil.ignore_patterns("SConscript"))
    shutil.copy2(args.root / "COPYING", destination / "COPYING")
    (destination / "SOURCE.txt").write_text(
        "Globulation 2 is licensed under GPL version 3.\n"
        "Corresponding source for this build:\n"
        f"https://github.com/Globulation2/glob2/archive/{args.commit}.zip\n",
        encoding="utf-8",
    )
    stage_dlls(args.exe, args.dll_dir, destination)

    icon = args.root / "data/icons/glob2-icon-128x128.png"
    with Image.open(icon) as source:
        source = source.convert("RGBA")
        for name, size in (("StoreLogo.png", 100), ("Logo150.png", 150),
                           ("Logo44.png", 44), ("Logo480.png", 480)):
            source.resize((size, size), Image.Resampling.LANCZOS).save(destination / name)
    write_game_config(destination, args)
    print(f"Staged {destination} with {len(list(destination.glob('*.dll')))} DLLs")


if __name__ == "__main__":
    main()
