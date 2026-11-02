"""Stage the existing MinGW build as a self-contained GDK PC game."""

import argparse
import re
import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))


from tools.release.windows_runtime import stage_assets, stage_dlls as stage_runtime_dlls


def stage_dlls(executable: Path, dll_dir: Path, destination: Path, sdl_runtime: Path | None = None) -> None:
    """Keep the Store helper's public error contract around shared staging."""
    runtimes = [executable.resolve().parent, dll_dir]
    if sdl_runtime:
        runtimes.insert(0, sdl_runtime)
    try:
        stage_runtime_dlls(executable, runtimes, destination)
    except FileNotFoundError as error:
        raise RuntimeError(str(error)) from error



def normalize_store_license_filenames(destination: Path) -> None:
    """Keep text .LIB notices out of the Store's foreign-library check.

    The MinGW license tree is shared with other distribution channels. Rename
    only its staged Store copy, validating the entire plan before changing any
    filename so a collision or unexpected binary cannot produce a partial fix.
    """
    licenses = destination / "licenses"
    if not licenses.is_dir():
        return
    paths = sorted(licenses.rglob("*"), key=lambda path: path.relative_to(licenses).as_posix())
    occupied = {path.relative_to(licenses).as_posix().casefold() for path in paths}
    renames = []
    for source in paths:
        if not source.is_file() or source.suffix.casefold() != ".lib":
            continue
        if source.is_symlink():
            raise RuntimeError(f"Unexpected symlink in Store license text: {source}")
        target = source.with_name(source.name + ".txt")
        key = target.relative_to(licenses).as_posix().casefold()
        if key in occupied:
            raise RuntimeError(f"Case-insensitive Store license filename collision: {target}")
        data = source.read_bytes()
        try:
            text = data.decode("utf-8-sig")
        except UnicodeDecodeError as error:
            raise RuntimeError(f"Non-text Store license file: {source}") from error
        if (data.startswith((b"!<arch>\n", b"!<thin>\n", b"MZ", b"\x7fELF"))
                or not text.strip()
                or any((ord(char) < 32 and char not in "\t\r\n\f") or ord(char) == 127
                       for char in text)):
            raise RuntimeError(f"Non-text Store license file: {source}")
        occupied.add(key)
        renames.append((source, target))
    for source, target in renames:
        source.rename(target)


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
        "SplashScreenImage": "SplashScreen.png",
    })
    ET.indent(game)
    ET.ElementTree(game).write(destination / "MicrosoftGame.config", encoding="utf-8", xml_declaration=True)


def write_shell_images(icon: Path, destination: Path) -> None:
    with Image.open(icon) as image:
        source = image.convert("RGBA")
    for name, size in (("StoreLogo.png", 100), ("Logo150.png", 150),
                       ("Logo44.png", 44), ("Logo480.png", 480)):
        source.resize((size, size), Image.Resampling.LANCZOS).save(destination / name)
    splash = Image.new("RGBA", (1920, 1080), (23, 48, 90, 255))
    mark = source.resize((512, 512), Image.Resampling.LANCZOS)
    splash.alpha_composite(mark, ((1920 - 512) // 2, (1080 - 512) // 2))
    splash.convert("RGB").save(destination / "SplashScreen.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--sdl-runtime", type=Path)
    parser.add_argument("--dll-dir", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--identity-name", required=True)
    parser.add_argument("--publisher", required=True)
    parser.add_argument("--publisher-display-name", required=True)
    parser.add_argument("--store-id", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    if (not re.fullmatch(r"[1-9]\d*(?:\.\d+){2}\.0", args.version)
            or any(int(part) > 65535 for part in args.version.split("."))):
        parser.error("--version must have four parts of 0..65535, a nonzero major version, and a zero revision")
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
    stage_assets(args.root, destination)
    (destination / "SOURCE.txt").write_text(
        "Globulation 2 is licensed under GPL version 3.\n"
        "Corresponding source for this build:\n"
        f"https://github.com/Globulation2/glob2/archive/{args.commit}.zip\n",
        encoding="utf-8",
    )
    stage_dlls(args.exe, args.dll_dir, destination, args.sdl_runtime)
    normalize_store_license_filenames(destination)

    icon = args.root / "content/data/icons/glob2-icon-128x128.png"
    write_shell_images(icon, destination)
    write_game_config(destination, args)
    print(f"Staged {destination} with {len(list(destination.glob('*.dll')))} DLLs")


if __name__ == "__main__":
    main()
