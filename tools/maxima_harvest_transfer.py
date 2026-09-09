"""Compressed, checksum-verified collection of one remote source game."""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tarfile
import tempfile
import time


PACK_SCRIPT = r'''
import gzip, hashlib, pathlib, sys, tarfile
root = pathlib.Path(sys.argv[1])
files = [root/'engine.log', root/'engine.exit', *sorted((root/'saves').glob('*.game'))]
lines = []
for path in files:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''): digest.update(chunk)
    lines.append(digest.hexdigest()+'  '+str(path.relative_to(root))+'\n')
(root/'SHA256SUMS').write_text(''.join(lines))
with gzip.GzipFile(fileobj=sys.stdout.buffer, mode='wb', compresslevel=1) as zipped:
    with tarfile.open(fileobj=zipped, mode='w|') as bundle:
        for path in [root/'SHA256SUMS', *files]:
            bundle.add(path, arcname=str(path.relative_to(root)), recursive=False)
'''


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def unpack_verified(archive: Path, destination: Path) -> dict[str, str]:
    """Do not publish any files until the entire archive verifies."""
    with tarfile.open(archive, "r:gz") as bundle:
        members = bundle.getmembers()
        names = [member.name for member in members]
        if len(names) != len(set(names)):
            raise ValueError("duplicate archive member")
        for member in members:
            if not member.isfile() or not (
                member.name in {"SHA256SUMS", "engine.log", "engine.exit"}
                or re.fullmatch(r"saves/checkpoint-\d+\.game", member.name)
            ):
                raise ValueError(f"unexpected archive member: {member.name}")
        if not {"SHA256SUMS", "engine.log", "engine.exit"} <= set(names):
            raise ValueError("archive is missing its log, exit status, or checksums")
        with tempfile.TemporaryDirectory(prefix=".verify-", dir=destination) as temporary:
            stage = Path(temporary)
            bundle.extractall(stage, filter="data")
            hashes = {}
            for line in (stage / "SHA256SUMS").read_text().splitlines():
                digest, name = line.split("  ", 1)
                if name not in names or not re.fullmatch(r"[0-9a-f]{64}", digest) or name in hashes:
                    raise ValueError("invalid checkpoint checksum manifest")
                hashes[name] = digest
            if set(hashes) != set(names)-{"SHA256SUMS"}:
                raise ValueError("checkpoint checksum manifest does not cover the archive")
            for name, digest in hashes.items():
                if file_hash(stage / name) != digest:
                    raise ValueError(f"checkpoint transfer checksum mismatch: {name}")
            for name in names:
                target = destination / name
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(stage / name, target)
            return hashes


def collect(ssh: list[str], host: str, remote_run: str, destination: Path,
            timeout: int = 900) -> dict:
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / "checkpoints.tar.gz.part"
    started = time.monotonic()
    command = "python3 -c " + shlex.quote(PACK_SCRIPT) + " " + shlex.quote(remote_run)
    with archive.open("wb") as stream:
        result = subprocess.run([*ssh, host, command], stdout=stream,
                                stderr=subprocess.PIPE, timeout=timeout)
    if result.returncode:
        raise OSError(f"checkpoint archive transfer exited {result.returncode}: "
                      + result.stderr.decode(errors="replace").strip())
    compressed_bytes = archive.stat().st_size
    hashes = unpack_verified(archive, destination)
    raw_bytes = sum((destination/name).stat().st_size for name in hashes)
    archive.unlink()
    return {"transfer_seconds": round(time.monotonic()-started, 3),
            "compressed_bytes": compressed_bytes, "uncompressed_bytes": raw_bytes,
            "verified_files": len(hashes), "file_hashes": hashes}
