#!/usr/bin/env python3
"""Package versioned browser assets and verified deterministic gzip sidecars.

Game data packages (scons/web_assets.py) are already named by their content and
listed inside both runtimes, so they keep their names under assets/."""

import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import shutil
import tempfile

POLICY = b"browser-static-gzip-v1\0"
MARKER = "package.json"
ROOT = Path(__file__).resolve().parents[1]


def package(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve() or source.resolve().is_relative_to(
        destination.resolve()
    ):
        raise ValueError("Package destination must not overlap build inputs")
    files = {
        ext: (source / f"index.{ext}").read_bytes() for ext in ("html", "js", "wasm")
    }
    packages = {
        "assets/" + p.name: p.read_bytes() for p in sorted((source / "assets").glob("*.data"))
    }
    if not packages:
        raise ValueError("Expected game data packages in assets/")
    threaded = 'src="loader.js"' in files["html"].decode()
    if threaded:
        files["loader"] = (source / "loader.js").read_bytes()
        for ext in ("js", "wasm"):
            files["threaded/" + ext] = (
                source / "threaded" / ("index." + ext)
            ).read_bytes()
    hive = {name: (source / name).read_bytes() for name in ("hive-worker.js", "hive-runtime.js", "hive-runtime.wasm") if (source / name).exists()}
    if hive and len(hive) != 3:
        raise ValueError("Incomplete Hive Mind runtime")
    recording = {name: (source / name).read_bytes() for name in
                 ('recording-worker.js', 'recording-storage.js', 'recording-video.js', 'recording-runtime.js', 'recording-runtime.wasm') if (source / name).exists()}
    if recording and len(recording) != 5:
        raise ValueError("Incomplete recording runtime")
    version = hashlib.sha256(
        POLICY + b"".join(hive.values()) + b"".join(recording.values()) + b"".join(files.values()) + b"".join(name.encode() for name in packages)
    ).hexdigest()[:16]
    names = {ext: f"index-{version}.{ext}" for ext in ("js", "wasm")}
    recording_names = {name: name.replace("recording-", f"recording-{version}-", 1) for name in recording}
    def recording_references(text):
        for old, new in recording_names.items():
            text = text.replace(old, new)
        return text
    recording = {recording_names[name]: recording_references(data.decode()).encode() if name.endswith(".js") else data for name, data in recording.items()}
    script = recording_references(files["js"].decode())
    script = script.replace('"index.wasm"', f'"{names["wasm"]}"')
    html = files["html"].decode().replace('src="index.js"', f'src="{names["js"]}"')
    html = html.replace("src=index.js>", f'src="{names["js"]}">')
    if not threaded and names["js"] not in html:
        raise ValueError("Expected Emscripten script tag")
    notices = {p.relative_to(source).as_posix(): p.read_bytes() for p in sorted((source / "licenses/recording").glob("*")) if p.is_file()}
    contents = {
        "index.html": html.encode(),
        names["js"]: script.encode(),
        names["wasm"]: files["wasm"],
        **packages,
        **hive,
        **recording,
        **notices,
    }
    if threaded:
        loader = f"loader-{version}.js"
        # The runtime map is data, so loader selection and fallback remain shared.
        mapping = {"serial": names["js"], "threaded": "threaded/" + names["js"]}
        tag = '<script>Module.glob2RuntimeFiles=' + json.dumps(mapping) + ';</script>'
        contents["index.html"] = html.replace(
            '<script src="loader.js"></script>',
            tag + f'<script src="{loader}"></script>',
        ).encode()
        contents[loader] = files["loader"]
        thread_script = recording_references(files["threaded/js"].decode())
        thread_script = thread_script.replace('"index.wasm"', f'"{names["wasm"]}"')
        contents["threaded/" + names["js"]] = thread_script.encode()
        contents["threaded/" + names["wasm"]] = files["threaded/wasm"]
    # The Studio entry uses the identical runtime but a temporary profile and bridge.
    contents["studio.html"] = contents["index.html"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="browser-static-", dir=destination.parent
    ) as temporary:
        stage = Path(temporary)
        for name, data in contents.items():
            (stage / name).parent.mkdir(parents=True, exist_ok=True)
            (stage / name).write_bytes(data)
            stream = io.BytesIO()
            with gzip.GzipFile(
                fileobj=stream, filename="", mode="wb", compresslevel=9, mtime=0
            ) as compressor:
                compressor.write(data)
            compressed = stream.getvalue()
            if gzip.decompress(compressed) != data:
                raise ValueError("Gzip integrity failure: " + name)
            (stage / (name + ".gz")).write_bytes(compressed)
        (stage / MARKER).write_text(
            json.dumps(
                {
                    "policy": POLICY.decode().rstrip("\0"),
                    "version": version,
                    "threaded": threaded,
                }
            )
            + "\n",
            encoding="utf-8",
        )
        (stage / "SHA256SUMS").write_text(
            "".join(
                f"{hashlib.sha256(p.read_bytes()).hexdigest()}  "
                f"{p.relative_to(stage).as_posix()}\n"
                for p in sorted(stage.rglob("*"))
                if p.is_file()
            ),
            encoding="utf-8",
        )
        verify(stage)
        # These are public assets mounted by the unprivileged web service.
        # TemporaryDirectory starts at 0700, and the caller may use umask 077.
        stage.chmod(0o755)
        for path in stage.rglob("*"):
            path.chmod(0o755 if path.is_dir() else 0o644)
        # The marker owns this entire generated directory; an HTML file alone
        # is not evidence that another website can safely be replaced.
        if destination.exists():
            try:
                owned = json.loads((destination / MARKER).read_text())[
                    "policy"
                ] == POLICY.decode().rstrip("\0")
            except (OSError, ValueError, KeyError, TypeError):
                owned = False
            if destination.is_symlink() or not owned:
                raise ValueError("Refusing to replace an unowned static package")
        backup = destination.with_name(destination.name + "-previous")
        if backup.exists():
            raise ValueError("Unresolved previous static package: " + str(backup))
        if destination.exists():
            destination.rename(backup)
        try:
            stage.rename(destination)
        except BaseException:
            if backup.exists():
                backup.rename(destination)
            raise
        if backup.exists():
            shutil.rmtree(backup)
    return version


def verify(directory):
    """Check the complete package, including every required gzip representation."""
    directory = Path(directory)
    expected = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        path = Path(name)
        if (
            path.is_absolute()
            or ".." in path.parts
            or path.as_posix() != name
            or name in expected
        ):
            raise ValueError("Invalid checksum entry")
        expected[name] = digest
    actual = {
        p.relative_to(directory).as_posix()
        for p in directory.rglob("*")
        if p.is_file() and p != directory / "SHA256SUMS"
    }
    if actual != set(expected):
        raise ValueError("Static package contains unexpected or missing files")
    for name, digest in expected.items():
        data = (directory / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("Static checksum mismatch: " + name)
        if (
            name.endswith(".gz")
            and gzip.decompress(data) != (directory / name[:-3]).read_bytes()
        ):
            raise ValueError("Sidecar does not match original: " + name)
    for ext in ("js", "wasm"):
        if len(list(directory.glob("index-*." + ext))) != 1:
            raise ValueError("Expected one versioned " + ext + " asset")
    marker = json.loads((directory / MARKER).read_text())
    if marker["policy"] != POLICY.decode().rstrip("\0"):
        raise ValueError("Unknown static package policy")
    names = ["index.html", "studio.html"] + [
        f"index-{marker['version']}.{ext}" for ext in ("js", "wasm")
    ]
    names += [
        name
        for name in expected
        if name.startswith("assets/") and name.endswith(".data")
    ]
    if not any(name.startswith("assets/") for name in names):
        raise ValueError("Static package lacks game data packages")
    names += [name for name in ("hive-worker.js", "hive-runtime.js", "hive-runtime.wasm") if name in expected]
    recording = [f"recording-{marker['version']}-{suffix}" for suffix in
                 ("worker.js", "storage.js", "video.js", "runtime.js", "runtime.wasm")]
    if any(name.startswith("recording-") for name in expected):
        names += recording
    names += [name for name in expected if name.startswith("licenses/recording/") and not name.endswith(".gz")]
    if marker.get("threaded"):
        names += [f"loader-{marker['version']}.js"] + [
            f"threaded/index-{marker['version']}.{ext}" for ext in ("js", "wasm")
        ]
    required = {MARKER, *names, *(name + ".gz" for name in names)}
    if set(expected) != required:
        raise ValueError(
            "Static package requires every original file and its gzip sidecar"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source", type=Path, default=ROOT / "build/emscripten/client/release"
    )
    parser.add_argument("--output", type=Path, default=ROOT / "build/browser-static")
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
        print("Verified static package and gzip sidecars")
    else:
        print(
            "Packaged " + package(args.source, args.output) + " in " + str(args.output)
        )


if __name__ == "__main__":
    main()
