"""Verified raw evidence for threshold-only and selected combined policy panels."""
from pathlib import Path
import gzip
import hashlib
import io
import json
import tarfile

root = Path(__file__).resolve().parent
campaign = root.parents[3]
studies = {
    'threshold-only-min8': campaign / 'final1280/final183-threshold-sweep',
    'combined-configuration': campaign / 'final1280/final184-policy/combined-configuration-study',
}
objects = {}
entries = []


def add(path, logical, expected=None):
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected is not None and digest != expected:
        raise ValueError('Frozen input hash mismatch: ' + str(path))
    objects.setdefault(digest, data)
    entries.append(dict(path=str(logical), sha256=digest, bytes=len(data), original_path=str(path)))


for label, study in studies.items():
    for path in sorted(study.rglob('*')):
        if not path.is_file() or path.name in {'glob2', 'glob2-final184', 'experiment.lock'} or path.suffix in {'.o', '.a', '.dylib'}:
            continue
        if any(part in {'profile', '__pycache__', 'build'} for part in path.relative_to(study).parts):
            continue
        add(path, Path(label) / path.relative_to(study))
    plan = json.loads((study / 'PLAN.json').read_text())
    for item in plan['maps']:
        for key, name in [('map_path', 'initial.map.gz'), ('warm_path', 'warm.game.gz')]:
            add(Path(item[key]), Path('inputs') / item['group'] / name, item[key.replace('_path', '_sha256')])
index = dict(schema=1, entries=entries, logical_files=len(entries), unique_objects=len(objects),
             scope='Both frozen latest-model policy panels:24OFF+6ON threshold16/24/32 atmin8, and24OFF+6ON16/min8,24/min8,24/min32. Commands/results/processmeasurements/load/work/quality/patch/linkinput proofs and exact175worldbytes. ELF/object/profile/dependency payloads excluded.')
(root / 'CAS.json').write_text(json.dumps(index, indent=2) + '\n')
archive = root / 'policy-cas.tar.gz'
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
