"""Verify and reconstruct portable logical files from the published CPU archive."""
from pathlib import Path
import argparse
import hashlib
import json
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
output = args.output.resolve()
receipt = json.loads((root / 'CAS-VERIFIED.json').read_text())
archive = root / 'raw-cpu-cas.tar.gz'
if hashlib.sha256(archive.read_bytes()).hexdigest() != receipt['archive_sha256']:
    raise ValueError('Archive hash mismatch')
index_path = root / 'CAS.json'
if hashlib.sha256(index_path.read_bytes()).hexdigest() != receipt['index_sha256']:
    raise ValueError('Index hash mismatch')
with tarfile.open(archive) as tar:
    for entry in json.loads(index_path.read_text())['entries']:
        data = tar.extractfile('objects/' + entry['sha256']).read()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Object hash mismatch')
        path = (output / entry['path']).resolve()
        if not path.is_relative_to(output):
            raise ValueError('Unsafe logical path')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
print('Verified all raw CPU cells and frozen inputs.')
