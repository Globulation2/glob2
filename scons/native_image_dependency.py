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
from sdl3_dependencies import patch_image_exports, static_webp_archive, static_webp_link_options

PACKAGES = ("sdl3", "libpng", "libjpeg", "libwebp", "libwebpdemux")


def native_path(value):
    if os.name == "nt" and value.startswith("/"):
        value = subprocess.check_output(["cygpath", "-w", value], text=True).strip()
    return Path(value)


def dependencies(environment=None):
    """Hash selected import/shared libraries and their actual Windows DLLs."""
    result = {}
    for package in PACKAGES:

        def pkg(option, package=package):
            return subprocess.check_output(
                ["pkg-config", option, package], text=True, env=environment
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


def compiler_options(cc, cxx):
    """Use the same driver/launcher split for probes and CMake builds."""
    result = []
    for language, command in (('C', cc), ('CXX', cxx)):
        tokens = shlex.split(command)
        if not tokens:
            raise ValueError('Compiler command cannot be empty')
        result.append('-DCMAKE_'+language+'_COMPILER='+tokens[-1])
        if len(tokens) > 1:
            result.append('-DCMAKE_'+language+'_COMPILER_LAUNCHER='+';'.join(tokens[:-1]))
    return result


def ensure(root, cc="gcc", cxx="g++", jobs=2, environment=None):
    options = lean_options()
    sdl_prefix = (environment or {}).get("GLOB2_SDL3_PREFIX")
    webp_archive = static_webp_archive(Path(sdl_prefix).resolve(), required=False) if sdl_prefix else None
    if webp_archive:
        options += static_webp_link_options(Path(sdl_prefix).resolve(), required=False)
    identity = dict(
        archive=ARTIFACT,
        options=options,
        exports_patch=1,
        static_webp={str(webp_archive): digest(webp_archive)} if webp_archive else {},
        platform=platform.system(),
        architecture=platform.machine(),
        dependencies=dependencies(environment),
        cc=subprocess.check_output([*shlex.split(cc), "--version"], text=True, env=environment),
        cxx=subprocess.check_output([*shlex.split(cxx), "--version"], text=True, env=environment),
        cmake=subprocess.check_output(["cmake", "--version"], text=True, env=environment),
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
                        ["pkg-config", "--variable=prefix", "sdl3"], text=True, env=environment
                    ).strip()
                )
                patch_image_exports(task / "SDL3_image-3.4.6")
                subprocess.run(
                    [
                        "cmake",
                        "-S",
                        str(task / "SDL3_image-3.4.6"),
                        "-B",
                        str(task / "build"),
                        "-G",
                        "Ninja",
                        "-DCMAKE_PREFIX_PATH=" + str(dependency_prefix),
                        "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                        "-DCMAKE_INSTALL_LIBDIR=lib",
                        *compiler_options(cc, cxx),
                        *options,
                    ],
                    check=True, env=environment,
                )
                subprocess.run(
                    ["cmake", "--build", str(task / "build"), "--parallel", str(jobs)],
                    check=True, env=environment,
                )
                subprocess.run(
                    [
                        "cmake",
                        "--install",
                        str(task / "build"),
                        "--prefix",
                        str(staged),
                    ],
                    check=True, env=environment,
                )
                notice = staged / "share/licenses/SDL3_image/LICENSE.txt"
                notice.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(task / "SDL3_image-3.4.6/LICENSE.txt", notice)
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
