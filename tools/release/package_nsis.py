#!/usr/bin/env python3
"""Compile the x64 installer from the exact portable Windows staging tree."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INVENTORY_BEGIN = ":GLOB2-OWNED-BEGIN:v1"
INVENTORY_END = ":GLOB2-OWNED-END:v1"


def quote(value, runtime=True):
    """Escape NSIS strings, including literal dollar signs in native paths."""
    value = str(value)
    if any(character in value for character in "\r\n\x00"):
        raise ValueError("Installer paths cannot contain line breaks or NUL")
    if "${" in value:
        raise ValueError("Installer paths cannot contain NSIS preprocessor expressions")
    if runtime:
        value = value.replace("$", "$$")
    return value.replace('"', '$\\"')


def file_lists(stage, output):
    stage, output = Path(stage).resolve(), Path(output)
    if not (stage / "glob2.exe").is_file():
        raise ValueError("Installer stage must contain glob2.exe")
    files = []
    directories = set()
    install = []
    for path in sorted(stage.rglob("*")):
        if path.is_symlink():
            raise ValueError("Installer stage cannot contain symlinks")
        if not path.is_file():
            continue
        relative = path.relative_to(stage)
        if any(part in (".", "..") or any(character in part for character in ":*?") for part in relative.parts):
            raise ValueError("Unsafe installer path: " + str(relative))
        native = str(relative).replace("/", "\\")
        files.append(native)
        parent = relative.parent
        while parent != Path("."):
            directories.add(str(parent).replace("/", "\\"))
            parent = parent.parent
        location = "$INSTDIR" + (
            "\\" + quote(str(relative.parent).replace("/", "\\"))
            if relative.parent != Path(".")
            else ""
        )
        install += [f'SetOutPath "{location}"', f'File "{quote(path, runtime=False)}"']
        if relative.suffix.lower() == ".webp":
            # First upgrade from a legacy installer has no ownership inventory.
            # Remove only original PNGs at paths now supplied as WebP.
            install.insert(
                len(install) - 2,
                f'Delete "$INSTDIR\\{quote(str(relative.with_suffix(".png")).replace("/", chr(92)))}"',
            )
    output.mkdir(parents=True, exist_ok=True)
    (output / "install.nsh").write_text("\n".join(install) + "\n", encoding="utf-8")
    # Unicode filenames must survive inventory reads during upgrades/uninstall.
    (output / "owned.txt").write_text("\n".join([INVENTORY_BEGIN, *files, INVENTORY_END]) + "\n", encoding="utf-16-le")
    (output / "directories.nsh").write_text(
        "\n".join(
            f'RMDir "$INSTDIR\\{quote(name)}"'
            for name in sorted(directories, key=lambda name: (-name.count("\\"), name))
        )
        + "\n",
        encoding="utf-8",
    )


def package(stage, output, version, compiler="makensis"):
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="glob2-nsis-") as temporary:
        generated = Path(temporary)
        file_lists(stage, generated)
        # Definitions live in a generated wrapper, avoiding differences between
        # Unix -D and Windows /D command-line parsing and quoting.
        wrapper = generated / "installer.nsi"
        wrapper.write_text(
            "\n".join(
                f'!define {key} "{quote(value, runtime=key == "VERSION")}"'
                for key, value in (
                    ("STAGE_DIR", Path(stage).resolve()),
                    ("LIST_DIR", generated),
                    ("OUT_FILE", output),
                    ("VERSION", version),
                )
            )
            + f'\n!include "{quote(ROOT / "windows/win32_installer.nsi", runtime=False)}"\n',
            encoding="utf-8",
        )
        subprocess.run([compiler, "-WX", str(wrapper)], check=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    print(package(args.stage, args.output, args.version))


if __name__ == "__main__":
    main()
