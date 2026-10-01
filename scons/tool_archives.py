"""Checksum-verified archive installation with atomic directory publication."""

import hashlib
import json
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path


def digest(path, algorithm="sha256"):
    result = hashlib.new(algorithm)
    with Path(path).open("rb") as source:
        while block := source.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def download(artifact, downloads, candidates=()):
    downloads = Path(downloads)
    downloads.mkdir(parents=True, exist_ok=True)
    archive = downloads / artifact["url"].rsplit("/", 1)[-1]
    algorithm = next(name for name in ("sha256", "sha512", "sha1") if name in artifact)
    expected = artifact[algorithm]
    if archive.exists() and digest(archive, algorithm) == expected:
        return archive
    with tempfile.NamedTemporaryFile(dir=downloads, delete=False) as output:
        temporary = Path(output.name)
        try:
            candidate = next(
                (
                    Path(p) / archive.name
                    for p in candidates
                    if (Path(p) / archive.name).is_file()
                    and digest(Path(p) / archive.name, algorithm) == expected
                ),
                None,
            )
            if candidate:
                with candidate.open("rb") as source:
                    shutil.copyfileobj(source, output)
            else:
                with urllib.request.urlopen(artifact["url"], timeout=60) as source:
                    shutil.copyfileobj(source, output)
            output.close()
            if digest(temporary, algorithm) != expected:
                raise ValueError("Tool archive checksum mismatch: " + artifact["url"])
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    return archive


def _install_direct(artifact, destination, downloads, candidates=(), trusted=False):
    destination = Path(destination)
    marker = destination / ".glob2-archive.json"
    proof = {
        name: artifact[name]
        for name in ("url", "sha256", "sha512", "sha1")
        if name in artifact
    }
    if destination.exists():
        if trusted and marker.is_file() and json.loads(marker.read_text()) == proof:
            return destination
        # Legacy isolated installs retain their existing behavior; shared installs require proof.
        if not trusted:
            return destination
        raise ValueError("Unverified shared installation: " + str(destination))
    archive = download(artifact, downloads, candidates)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        dir=destination.parent, prefix=".install-"
    ) as staging:
        if archive.suffix == ".zip":
            subprocess.run(["unzip", "-q", str(archive), "-d", staging], check=True)
        else:
            with tarfile.open(archive) as source:
                source.extractall(staging, filter="data")
        extracted = Path(staging) / artifact.get(
            "archive_directory", artifact["directory"]
        )
        (extracted / ".glob2-archive.json").write_text(
            json.dumps(proof, sort_keys=True) + "\n"
        )
        extracted.rename(destination)
    return destination


def install(artifact, destination, downloads, candidates=(), trusted=False):
    """Share identical archive components even when a mobile bundle pin changes."""
    destination = Path(destination)
    if not trusted:
        return _install_direct(
            artifact, destination, downloads, candidates, trusted=False
        )
    from dev_store import Lease, home

    if not destination.is_relative_to(home() / "toolchains"):
        return _install_direct(
            artifact, destination, downloads, candidates, trusted=True
        )
    import platform

    proof = {
        name: artifact[name]
        for name in ("url", "sha256", "sha512", "sha1")
        if name in artifact
    }
    identity = (
        platform.system()
        + "-"
        + platform.machine()
        + "-"
        + hashlib.sha256(json.dumps(proof, sort_keys=True).encode()).hexdigest()[:24]
    )
    component = home() / "toolchains/artifacts" / identity
    component.parent.mkdir(parents=True, exist_ok=True)
    with Lease(component, exclusive=True):
        if destination.is_symlink():
            if destination.resolve() != component:
                raise ValueError(
                    "Tool link points to another component: " + str(destination)
                )
        elif destination.exists():
            # Convert a previous verified managed installation without discarding any files.
            marker = destination / ".glob2-archive.json"
            if not marker.is_file() or json.loads(marker.read_text()) != proof:
                raise ValueError("Unverified shared installation: " + str(destination))
            if component.exists():
                raise ValueError(
                    "Duplicate managed installation needs inspection: "
                    + str(destination)
                )
            destination.rename(component)
        _install_direct(artifact, component, downloads, candidates, trusted=True)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.is_symlink():
            destination.symlink_to(component, target_is_directory=True)
    return destination
