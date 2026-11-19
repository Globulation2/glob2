"""Extract a Steam depot transfer archive and verify its source and file manifest."""

import argparse
import hashlib
import ntpath
import re
import tarfile
from pathlib import Path, PurePosixPath


def relative_path(value):
    path = PurePosixPath(value)
    if (not value or path.is_absolute() or '..' in path.parts or '\\' in value
            or ntpath.splitdrive(value)[0] or re.search(r'[<>:"|?*]', value)
            or any(part.endswith((' ', '.')) for part in path.parts)):
        raise ValueError(f'Unsafe depot path: {value}')
    return path


def extract_verify(archive, destination, source_sha):
    destination = Path(destination)
    if destination.exists():
        raise ValueError('Depot destination must not already exist')
    with tarfile.open(archive, 'r:gz') as stream:
        names = set()
        for member in stream.getmembers():
            name = relative_path(member.name).as_posix().casefold()
            if name in names:
                raise ValueError(f'Duplicate depot archive entry: {member.name}')
            names.add(name)
            if not (member.isfile() or member.isdir()):
                raise ValueError(f'Unsupported depot archive entry: {member.name}')
        # Python 3.12's data filter also checks Windows paths and extraction roots.
        stream.extractall(destination, filter='data')
    if (destination / 'source-commit.txt').read_text().strip() != source_sha:
        raise ValueError('Depot source identity mismatch')
    manifest = destination / 'SHA256SUMS.txt'
    verified = set()
    for line in manifest.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64}) [ *](.+)', line)
        if not match:
            raise ValueError('Invalid depot manifest entry')
        expected, name = match.groups()
        path = relative_path(name)
        if path.as_posix() in verified:
            raise ValueError(f'Duplicate depot manifest entry: {name}')
        file = destination / path
        with file.open('rb') as content:
            actual = hashlib.file_digest(content, 'sha256').hexdigest()
        if actual != expected:
            raise ValueError(f'Depot checksum mismatch: {name}')
        verified.add(path.as_posix())
    files = {path.relative_to(destination).as_posix() for path in destination.rglob('*') if path.is_file()}
    if files != verified | {'SHA256SUMS.txt'}:
        raise ValueError('Depot manifest does not cover exactly the extracted files')
    print(f'Verified {len(verified)} depot files from {source_sha}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--source-sha', required=True)
    args = parser.parse_args()
    extract_verify(args.archive, args.destination, args.source_sha)
