#!/usr/bin/env python3
"""Prepare checksum-pinned encoder sources, or build them without network access."""

import argparse
import json
import os
import shlex
from pathlib import Path
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "scons"))
from tool_archives import download, digest

SOURCES = json.loads((ROOT / "tools/asset-encoder-sources.json").read_text())


def source_trees(archives, destination):
    result = {}
    for name, artifact in SOURCES.items():
        archive = Path(archives) / Path(artifact["url"]).name
        if not archive.is_file() or digest(archive) != artifact["sha256"]:
            raise ValueError(
                "Missing or corrupt offline encoder source: " + str(archive)
            )
        folder = destination / name
        folder.mkdir()
        with tarfile.open(archive) as packed:
            packed.extractall(folder, filter="data")
        children = list(folder.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise ValueError("Unexpected encoder archive layout: " + name)
        result[name] = children[0]
    return result


def build(archives, output, jobs=2):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Encoder output must be a fresh build directory")
    output.mkdir(parents=True)
    sources = source_trees(archives, output)
    native = output / "native"
    subprocess.run(
        [
            "cmake",
            "-S",
            str(sources["webp"]),
            "-B",
            str(output / "webp-build"),
            "-DCMAKE_INSTALL_PREFIX=" + str(native),
            "-DCMAKE_INSTALL_LIBDIR=lib",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DBUILD_SHARED_LIBS=ON",
            "-DWEBP_BUILD_EXTRAS=OFF",
        ],
        check=True,
    )
    subprocess.run(
        ["cmake", "--build", str(output / "webp-build"), "--parallel", str(jobs)],
        check=True,
    )
    subprocess.run(["cmake", "--install", str(output / "webp-build")], check=True)
    subprocess.run(
        [sys.executable, "-m", "venv", "--system-site-packages", str(output / "venv")],
        check=True,
    )
    python = output / "venv/bin/python"
    env = dict(
        os.environ,
        CFLAGS=shlex.quote("-I" + str(native / "include")),
        LDFLAGS=shlex.quote("-L" + str(native / "lib"))
        + " "
        + shlex.quote("-Wl,-rpath," + str(native / "lib")),
        PKG_CONFIG_PATH=str(native / "lib/pkgconfig"),
        # Pillow imports the header-only pybind11 build helper directly. Its
        # package build backend would add unrelated offline dependencies.
        PYTHONPATH=str(sources["pybind11"]),
    )
    for name in ("setuptools", "pillow"):
        # The encoder needs PNG/JPEG/WebP only; optional system codecs must not
        # change the offline build or introduce accidental build dependencies.
        options = (
            (
                ["-C", "webp=enable"]
                + [
                    option
                    for feature in (
                        "tiff",
                        "freetype",
                        "raqm",
                        "lcms",
                        "jpeg2000",
                        "imagequant",
                        "xcb",
                        "avif",
                    )
                    for option in ("-C", feature + "=disable")
                ]
            )
            if name == "pillow"
            else []
        )
        subprocess.run(
            [
                str(python),
                "-m",
                "pip",
                "install",
                "--no-index",
                "--no-build-isolation",
                "--no-deps",
                *options,
                str(sources[name]),
            ],
            check=True,
            env=env,
        )
    from tools.package_assets import encoder_probe_command

    subprocess.run(encoder_probe_command(python), check=True)
    (output / "sources.json").write_text(json.dumps(SOURCES, indent=2) + "\n")
    return python


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("fetch", "build"))
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.command == "fetch":
        for source in SOURCES.values():
            download(source, args.sources)
    elif args.output is None:
        parser.error("--output is required for build")
    else:
        print(build(args.sources, args.output))


if __name__ == "__main__":
    main()
