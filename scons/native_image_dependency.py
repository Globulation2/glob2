"""Private lean SDL_image builds for self-contained Linux and MinGW packages."""

import hashlib
import json
import os
import platform
import shlex
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from dev_store import Lease, cache, hold
from image_codecs import ARTIFACT, lean_options, verified
from tool_archives import digest, download

PACKAGES = ("sdl2", "libpng", "libjpeg", "libwebp", "libwebpdemux")


def native_path(value):
    if os.name == "nt" and value.startswith("/"):
        value = subprocess.check_output(["cygpath", "-w", value], text=True).strip()
    return Path(value)


def dependencies():
    """Hash selected import/shared libraries and their actual Windows DLLs."""
    result = {}
    for package in PACKAGES:

        def pkg(option, package=package):
            return subprocess.check_output(
                ["pkg-config", option, package], text=True
            ).strip()

        libdir = native_path(pkg("--variable=libdir"))
        dirs = [libdir] + [
            native_path(flag[2:])
            for flag in shlex.split(pkg("--libs-only-L"))
            if flag.startswith("-L")
        ]
        files = {}
        for flag in shlex.split(pkg("--libs-only-l")):
            if not flag.startswith("-l"):
                continue
            name = "lib" + flag[2:]
            selected = next(
                (
                    directory / (name + suffix)
                    for directory in dirs
                    for suffix in (".so", ".dll.a", ".a")
                    if (directory / (name + suffix)).is_file()
                ),
                None,
            )
            if selected is None:
                raise ValueError(f"Cannot fingerprint {package} dependency: {name}")
            files[str(selected.resolve())] = digest(selected.resolve())
            if selected.name.endswith(".dll.a"):
                # Import archives can remain unchanged after a runtime update.
                for binary in (libdir.parent / "bin").glob("*.dll"):
                    stem = binary.stem.lower()
                    for alias in (name.lower(), name.removeprefix("lib").lower()):
                        if stem == alias or stem.startswith(alias + "-"):
                            files[str(binary.resolve())] = digest(binary.resolve())
        result[package] = dict(version=pkg("--modversion"), files=files)
    return result


def ensure(root, cc="gcc", cxx="g++", jobs=2):
    identity = dict(
        archive=ARTIFACT,
        options=lean_options(),
        platform=platform.system(),
        architecture=platform.machine(),
        dependencies=dependencies(),
        cc=subprocess.check_output([cc, "--version"], text=True),
        cxx=subprocess.check_output([cxx, "--version"], text=True),
        cmake=subprocess.check_output(["cmake", "--version"], text=True),
    )
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:24]
    location = cache(root, "native-sdl-image-" + key, lease=False)
    prefix = location / "prefix"
    with Lease(location, exclusive=True):
        if not verified(prefix, identity):
            location.mkdir(parents=True, exist_ok=True)
            archive = download(ARTIFACT, location / "downloads")
            with tempfile.TemporaryDirectory(
                prefix="build-", dir=location
            ) as temporary:
                task = Path(temporary)
                with tarfile.open(archive) as source:
                    source.extractall(task, filter="data")
                staged = task / "prefix"
                dependency_prefix = native_path(
                    subprocess.check_output(
                        ["pkg-config", "--variable=prefix", "sdl2"], text=True
                    ).strip()
                )
                subprocess.run(
                    [
                        "cmake",
                        "-S",
                        str(task / "SDL2_image-2.8.12"),
                        "-B",
                        str(task / "build"),
                        "-G",
                        "Ninja",
                        "-DCMAKE_PREFIX_PATH=" + str(dependency_prefix),
                        "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                        "-DCMAKE_INSTALL_LIBDIR=lib",
                        "-DCMAKE_C_COMPILER=" + cc,
                        "-DCMAKE_CXX_COMPILER=" + cxx,
                        *lean_options(),
                    ],
                    check=True,
                )
                subprocess.run(
                    ["cmake", "--build", str(task / "build"), "--parallel", str(jobs)],
                    check=True,
                )
                subprocess.run(
                    [
                        "cmake",
                        "--install",
                        str(task / "build"),
                        "--prefix",
                        str(staged),
                    ],
                    check=True,
                )
                record = dict(
                    identity=identity,
                    files={
                        path.relative_to(staged).as_posix(): digest(path)
                        for path in sorted(staged.rglob("*"))
                        if path.is_file()
                    },
                )
                (staged / "manifest.json").write_text(
                    json.dumps(record, indent=2, sort_keys=True) + "\n"
                )
                if prefix.exists():
                    shutil.rmtree(prefix)
                staged.rename(prefix)
    hold(location)
    return prefix
