#!/usr/bin/env python3
"""Generate qualified public download metadata from the final signed package bytes."""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import quote

try:
    from . import qualification
except ImportError:
    import qualification


ROOT = Path(__file__).resolve().parents[2]
REQUIRED = {
    ("windows", "x86_64", "exe"), ("windows", "x86_64", "zip"),
    ("macos", "arm64", "dmg"), ("macos", "x86_64", "dmg"),
    *(("linux", "x86_64", kind) for kind in ("tar.gz", "rpm", "flatpak", "snap")),
    *(("android", arch, "apk") for arch in ("arm64", "armv7", "x86_64")),
}


def file_metadata(directory, filename, base_url):
    if not isinstance(filename, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", filename):
        raise ValueError("artifact filename must be a plain safe basename")
    path = directory / filename
    if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"artifact must be a nonempty regular file: {filename}")
    digest = hashlib.sha256()
    with path.open("rb") as artifact:
        for block in iter(lambda: artifact.read(1024 * 1024), b""):
            digest.update(block)
    return {"filename": filename, "sizeBytes": path.stat().st_size,
            "sha256": digest.hexdigest(), "url": base_url + quote(filename, safe="")}


def generate(directory, inventory, evidence, tag, source_commit, repository="Globulation2/glob2"):
    if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+)+", tag):
        raise ValueError("release tag must be v followed by a numeric version")
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("source commit must be a full lowercase Git SHA")
    if repository != "Globulation2/glob2":
        raise ValueError("public downloads must target Globulation2/glob2")
    if not isinstance(inventory, dict) or inventory.get("schemaVersion") != 1 or inventory.get("sourceCommit") != source_commit:
        raise ValueError("inventory schemaVersion/sourceCommit must match the release")
    packages = inventory.get("packages")
    sources = inventory.get("sources", [])
    if not isinstance(packages, list) or not isinstance(sources, list):
        raise ValueError("inventory packages and sources must be arrays")
    if not sources:
        raise ValueError("at least one source archive is required")
    base = f"https://github.com/{repository}/releases/download/{quote(tag, safe='')}/"
    seen_files, seen_identities, output_packages, output_sources = set(), set(), [], []
    for descriptor in packages:
        if not isinstance(descriptor, dict):
            raise ValueError("package descriptor must be an object")
        identity = tuple(descriptor.get(key) for key in ("platform", "architecture", "format"))
        if not all(isinstance(value, str) for value in identity):
            raise ValueError("package platform, architecture and format must be strings")
        if identity not in REQUIRED or identity in seen_identities:
            raise ValueError(f"unsupported or duplicate package identity: {identity}")
        seen_identities.add(identity)
        minimum = descriptor.get("minimumOs")
        if not isinstance(minimum, str) or not minimum.strip():
            raise ValueError("package minimumOs is required")
        metadata = file_metadata(directory, descriptor.get("filename"), base)
        if not metadata["filename"].endswith("." + identity[2]):
            raise ValueError("package filename extension must match its format")
        if metadata["filename"] in seen_files:
            raise ValueError("duplicate artifact filename")
        seen_files.add(metadata["filename"])
        item = {**{key: descriptor[key] for key in ("platform", "architecture", "format", "minimumOs")}, **metadata}
        if "dependencies" in descriptor:
            dependencies = descriptor["dependencies"]
            if not isinstance(dependencies, list) or not all(isinstance(value, str) and value.strip() for value in dependencies):
                raise ValueError("dependencies must be nonempty strings")
            item["dependencies"] = dependencies
        output_packages.append(item)
    if seen_identities != REQUIRED:
        raise ValueError(f"missing required packages: {sorted(REQUIRED - seen_identities)}")
    for descriptor in sources:
        if not isinstance(descriptor, dict):
            raise ValueError("source descriptor must be an object")
        metadata = file_metadata(directory, descriptor.get("filename"), base)
        if metadata["filename"] in seen_files:
            raise ValueError("source archive must be distinct from playable packages")
        if not metadata["filename"].endswith((".tar.gz", ".tar.xz", ".zip")):
            raise ValueError("source artifact must be an archive")
        seen_files.add(metadata["filename"])
        output_sources.append(metadata)
    digests = {item["filename"]: item["sha256"] for item in output_packages + output_sources}
    summary = qualification.validate(evidence, tag, source_commit, digests)
    return {"schemaVersion": 1, "version": tag[1:], "tag": tag, "sourceCommit": source_commit,
            "releaseNotesUrl": f"https://github.com/{repository}/releases/tag/{tag}",
            "qualification": summary,
            "packages": sorted(output_packages, key=lambda p: (p["platform"], p["architecture"], p["format"])),
            "sources": sorted(output_sources, key=lambda p: p["filename"])}


def validate_checkout(tag, source_commit):
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True, stderr=subprocess.PIPE).strip()
    if git("rev-parse", "HEAD") != source_commit or git("rev-parse", f"refs/tags/{tag}^{{commit}}") != source_commit:
        raise ValueError("checkout HEAD and immutable release tag must identify sourceCommit")
    version = re.search(r'^PACKAGE_VERSION = "([0-9.]+)"$', (ROOT / "scons/build_layout.py").read_text(), re.MULTILINE)
    if not version or tag != "v" + version[1]:
        raise ValueError("release tag must match PACKAGE_VERSION")


def atomic_write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(text)
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--qualification", required=True, type=Path)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--repository", default="Globulation2/glob2")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checksums", type=Path)
    args = parser.parse_args()
    try:
        validate_checkout(args.tag, args.source_commit)
        manifest = generate(args.artifacts, json.loads(args.inventory.read_text()),
                            json.loads(args.qualification.read_text()), args.tag,
                            args.source_commit, args.repository)
        atomic_write(args.output, json.dumps(manifest, indent=2) + "\n")
        if args.checksums:
            files = manifest["packages"] + manifest["sources"]
            atomic_write(args.checksums, "".join(f"{item['sha256']}  {item['filename']}\n" for item in sorted(files, key=lambda p: p["filename"])))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"downloads manifest rejected: {error}\n")
    print(f"qualified downloads manifest: {args.output}")


if __name__ == "__main__":
    main()
