#!/usr/bin/env python3
"""Preserve the second recovered archive; omit only AppleDouble filesystem metadata."""
import argparse
import json
import zipfile
from pathlib import Path, PurePosixPath
from import_originals import ART, ROOT, digest

MANIFEST = ART / 'provenance/stephane-2026-09-08-more.json'
GROUPS = {'hive': 'swarm', 'flags': 'flags', 'Constructions': 'construction',
          'direction template': 'direction-templates'}

def verify(archive=None):
    record = json.loads(MANIFEST.read_text())
    for entry in record['files']:
        assert digest((ROOT / entry['path']).read_bytes()) == entry['sha256'], entry['path']
    if archive:
        assert digest(archive.read_bytes()) == record['archive_sha256']
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert {i.filename for i in z.infolist() if not i.is_dir()} == {
                e['archive_path'] for e in record['files'] + record['excluded']}
            for entry in record['files'] + record['excluded']:
                assert digest(z.read(entry['archive_path'])) == entry['sha256']
    print('PASS: %d second-archive artwork files preserved; %d AppleDouble metadata entries documented' %
          (len(record['files']), len(record['excluded'])))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', nargs='?', type=Path)
    archive = parser.parse_args().archive
    if MANIFEST.exists():
        verify(archive)
        return
    if archive is None:
        parser.error('archive required for initial import')
    record = dict(url='https://h.magnenat.net/~steph/glob2-highres-more.zip',
                  comment='https://github.com/Globulation2/glob2/pull/207#issuecomment-5591064207',
                  archive_sha256=digest(archive.read_bytes()), files=[], excluded=[])
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for info in sorted(z.infolist(), key=lambda i: i.filename):
            if info.is_dir(): continue
            parts = PurePosixPath(info.filename).parts
            assert parts[0] == 'glob2-highres-more' and '..' not in parts
            data = z.read(info)
            entry = dict(archive_path=info.filename, sha256=digest(data), bytes=len(data))
            if '.AppleDouble' in parts:
                entry['reason'] = 'Mac filesystem metadata, not artwork; original ZIP hash retained'
                record['excluded'].append(entry)
                continue
            assert len(parts) == 3 and parts[1] in GROUPS
            dest = ART / 'reference-exports/buildings' / GROUPS[parts[1]] / parts[-1]
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.exists(): assert dest.read_bytes() == data
            else: dest.write_bytes(data)
            entry['path'] = str(dest.relative_to(ROOT))
            record['files'].append(entry)
    MANIFEST.write_text(json.dumps(record, indent=2) + '\n')
    verify(archive)

if __name__ == '__main__': main()
