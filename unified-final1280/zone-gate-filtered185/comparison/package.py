"""Publish all raw CPU cells and exact175inputs as verified content-addressed data.

Binaries, profiles, objects and dependency payloads are deliberately excluded.
The original absolute paths remain provenance; extracted logical paths are portable.
"""
from pathlib import Path
import argparse
import gzip
import hashlib
import io
import json
import tarfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
source = args.source.resolve()
objects = {}
entries = []


def add(path, logical, expected=None):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected is not None and digest != expected:
        raise ValueError('Frozen input hash mismatch: ' + str(path))
    objects.setdefault(digest, data)
    entries.append(dict(path=str(logical), sha256=digest, bytes=len(data), original_path=str(path)))


for path in sorted(source.rglob('*')):
    if not path.is_file() or path.name in {'glob2-filtered185', 'experiment.lock'}:
        continue
    if any(part in {'profile', '__pycache__', 'build'} for part in path.relative_to(source).parts):
        continue
    add(path, Path('study') / path.relative_to(source))
plan = json.loads((source / 'PLAN.json').read_text())
for item in plan['maps']:
    for key, name in [('map_path', 'initial.map.gz'), ('warm_path', 'warm.game.gz')]:
        add(Path(item[key]), Path('inputs') / item['group'] / name, item[key.replace('_path', '_sha256')])
index = dict(schema=1, entries=entries, logical_files=len(entries), unique_objects=len(objects),
             scope='Final185 filtered experts versus master,184 and185unfiltered controls; all32OFF+6ON raw cells, commands, results, CPU/wall measurements, load observations and exact175initialstates. No ELF/object/dependency payloads.')
(root / 'CAS.json').write_text(json.dumps(index, indent=2) + '\n')
archive = root / 'raw-cpu-cas.tar.gz'
with archive.open('wb') as raw:
    with gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as tar:
            for digest, data in sorted(objects.items()):
                info = tarfile.TarInfo('objects/' + digest)
                info.size, info.mode, info.mtime = len(data), 0o644, 0
                tar.addfile(info, io.BytesIO(data))
with tarfile.open(archive) as tar:
    for digest, expected in objects.items():
        actual = tar.extractfile('objects/' + digest).read()
        if actual != expected or hashlib.sha256(actual).hexdigest() != digest:
            raise ValueError('Archived bytes differ: ' + digest)
receipt = dict(status='PASS', archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
               archive_bytes=archive.stat().st_size, logical_files=len(entries), unique_objects=len(objects),
               index_sha256=hashlib.sha256((root / 'CAS.json').read_bytes()).hexdigest())
(root / 'CAS-VERIFIED.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt))
