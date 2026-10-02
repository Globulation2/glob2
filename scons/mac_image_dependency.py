"""Checksum-pinned, cached lean SDL_image for Mac release packages."""

import hashlib
import json
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tempfile
from dev_store import cache, Lease, hold
from tool_archives import download, digest
from sdl3_dependencies import patch_image_exports, static_webp_link_options, static_webp_archive

from image_codecs import ARTIFACT, CODECS, DEPENDENCIES, lean_options, verified


def dependency_identity(environment=None):
    """Fingerprint linked libraries, not every unrelated dylib in Homebrew/lib."""
    dependencies = {}
    hashes = {}
    for package in DEPENDENCIES:
        version = subprocess.check_output(
            ["pkg-config", "--modversion", package], text=True, env=environment
        ).strip()
        libdir = Path(
            subprocess.check_output(
                ["pkg-config", "--variable=libdir", package], text=True, env=environment
            ).strip()
        )
        flags = shlex.split(
            subprocess.check_output(
                ["pkg-config", "--libs-only-l", package],
                text=True, env=environment,
            )
        )
        directories = [libdir] + [
            Path(flag[2:])
            for flag in shlex.split(
                subprocess.check_output(
                    ["pkg-config", "--libs-only-L", package], text=True, env=environment
                )
            )
            if flag.startswith("-L")
        ]
        files = {}
        for flag in flags:
            if not flag.startswith("-l"):
                continue
            name = "lib" + flag[2:]
            library = next(
                (
                    directory / (name + suffix)
                    for directory in directories
                    for suffix in (".dylib", ".a")
                    if (directory / (name + suffix)).is_file()
                ),
                None,
            )
            if library is None:
                raise RuntimeError(
                    "Could not fingerprint " + package + " library: " + name
                )
            library = library.resolve()
            # libwebp is also linked by libwebpdemux; hash shared real files once.
            if library not in hashes:
                hashes[library] = digest(library)
            files[str(library)] = hashes[library]
        if not files:
            raise RuntimeError("No linked libraries found for " + package)
        dependencies[package] = dict(version=version, files=files)
    return dependencies


def ensure(root, jobs=2, environment=None):
    """Return a leased, verified private build of the required image codecs."""
    brew = Path(subprocess.check_output(["brew", "--prefix"], text=True, env=environment).strip())
    options = lean_options()
    sdl_prefix = (environment or {}).get("GLOB2_SDL3_PREFIX")
    webp_archive = static_webp_archive(Path(sdl_prefix).resolve(), required=False) if sdl_prefix else None
    if webp_archive:
        options += static_webp_link_options(Path(sdl_prefix).resolve(), required=False)
    identity = dict(
        archive=ARTIFACT,
        exports_patch=1,
        options=options,
        static_webp={str(webp_archive): digest(webp_archive)} if webp_archive else {},
        arch=platform.machine(),
        compiler=subprocess.check_output(["clang", "--version"], text=True, env=environment),
        sdk=subprocess.check_output(["xcrun", "--show-sdk-version"], text=True, env=environment),
        dependencies=dependency_identity(environment),
    )
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:24]
    location = cache(root, "mac-sdl-image-" + key, lease=False)
    prefix = location / "prefix"
    with Lease(location, exclusive=True):
        if not verified(prefix, identity):
            location.mkdir(parents=True, exist_ok=True)
            archive = download(ARTIFACT, location / "downloads")
            with tempfile.TemporaryDirectory(
                prefix="build-", dir=location
            ) as temporary:
                task = Path(temporary)
                import tarfile

                with tarfile.open(archive) as source:
                    source.extractall(task, filter="data")
                source = task / "SDL3_image-3.4.6"
                patch_image_exports(source)
                build = task / "build"
                staged = task / "prefix"
                subprocess.run(
                    [
                        "cmake",
                        "-S",
                        str(source),
                        "-B",
                        str(build),
                        "-DCMAKE_PREFIX_PATH=" + str(Path((environment or {}).get("GLOB2_SDL3_PREFIX", str(brew))).resolve()) + ";" + str(brew),
                        "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                        "-DCMAKE_INSTALL_NAME_DIR=" + str(prefix / "lib"),
                        *options,
                    ],
                    check=True, env=environment,
                )
                subprocess.run(
                    ["cmake", "--build", str(build), "--parallel", str(jobs)],
                    check=True, env=environment,
                )
                subprocess.run(
                    ["cmake", "--install", str(build), "--prefix", str(staged)],
                    check=True, env=environment,
                )
                record = dict(
                    identity=identity,
                    files={
                        p.relative_to(staged).as_posix(): digest(p)
                        for p in sorted(staged.rglob("*"))
                        if p.is_file()
                    },
                )
                (staged / "manifest.json").write_text(
                    json.dumps(record, indent=2, sort_keys=True) + "\n"
                )
                if prefix.exists():
                    shutil.rmtree(prefix)
                staged.rename(prefix)
        if not verified(prefix, identity):
            raise RuntimeError("Lean SDL_image verification failed")
    hold(location)
    return prefix
