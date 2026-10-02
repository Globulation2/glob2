#!/usr/bin/env python3
"""Package versioned browser assets and verified deterministic gzip sidecars."""

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
        ext: (source / f"index.{ext}").read_bytes()
        for ext in ("html", "js", "wasm", "data")
    }
    version = hashlib.sha256(POLICY + b"".join(files.values())).hexdigest()[:16]
    names = {ext: f"index-{version}.{ext}" for ext in ("js", "wasm", "data")}
    script = files["js"].decode()
    for ext in ("wasm", "data"):
        script = script.replace(f'"index.{ext}"', f'"{names[ext]}"')
    html = files["html"].decode().replace('src="index.js"', f'src="{names["js"]}"')
    html = html.replace("src=index.js>", f'src="{names["js"]}">')
    if names["js"] not in html:
        raise ValueError("Expected Emscripten script tag")
    contents = {
        "index.html": html.encode(),
        names["js"]: script.encode(),
        names["wasm"]: files["wasm"],
        names["data"]: files["data"],
    }
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="browser-static-", dir=destination.parent
    ) as temporary:
        stage = Path(temporary)
        for name, data in contents.items():
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
                }
            )
            + "\n",
            encoding="utf-8",
        )
        (stage / "SHA256SUMS").write_text(
            "".join(
                f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
                for p in sorted(stage.iterdir())
            ),
            encoding="utf-8",
        )
        verify(stage)
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
        if Path(name).name != name or name in expected:
            raise ValueError("Invalid checksum entry")
        expected[name] = digest
    actual = {
        p.name for p in directory.iterdir() if p.is_file() and p.name != "SHA256SUMS"
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
    for ext in ("js", "wasm", "data"):
        if len(list(directory.glob("index-*." + ext))) != 1:
            raise ValueError("Expected one versioned " + ext + " asset")
    marker = json.loads((directory / MARKER).read_text())
    if marker["policy"] != POLICY.decode().rstrip("\0"):
        raise ValueError("Unknown static package policy")
    names = ["index.html"] + [
        f"index-{marker['version']}.{ext}" for ext in ("js", "wasm", "data")
    ]
    required = {MARKER, *names, *(name + ".gz" for name in names)}
    if set(expected) != required:
        raise ValueError(
            "Static package requires all four original files and gzip sidecars"
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
