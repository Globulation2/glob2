#!/usr/bin/env python3
"""Import Stéphane's recovered archive without altering or overwriting source bytes."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'datasrc/gfx'
MANIFEST = ART / 'provenance/stephane-2026-09-08.json'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def destination(name):
    p = PurePosixPath(name)
    if p.parts[0] != 'glob2-highres' or '..' in p.parts:
        raise ValueError(f'Unexpected archive path: {name}')
    parts = p.parts[1:]
    group = parts[0]
    if group == 'buildings':
        return ART / 'originals/buildings' / parts[-1]
    if group == 'buildings-concept':
        return ART / 'concept-art/buildings' / parts[-1]
    if group == 'terrain':
        families = {'Arbres': 'tree', 'Blé smarties': 'wheat', 'Blés': 'wheat-classic'}
        if len(parts) > 2 and parts[1] in families:
            folder = ART / ('reference-exports/resources' if p.suffix.lower() == '.png' else 'originals/resources') / families[parts[1]]
            return folder / parts[-1]
        if len(parts) > 2 and parts[1] == 'water':
            return ART / ('reference-exports/terrain/water' if p.suffix.lower() == '.png' else 'originals/terrain') / parts[-1]
        return ART / 'originals/terrain' / parts[-1]
    if group == 'ui':
        return ART / ('reference-exports/ui/controls' if p.suffix.lower() == '.png' else 'originals/ui/controls') / parts[-1]
    if group == 'cursor':
        return ART / 'originals/cursors' / parts[-1]
    if group == 'areas':
        return ART / ('reference-exports/overlays' if p.suffix.lower() == '.png' else 'originals/overlays') / parts[-1]
    raise ValueError(f'Unclassified archive path: {name}')

def dimensions(data):
    if data.startswith(b'gimp xcf '):
        return list(struct.unpack('>II', data[14:22])), 'xcf'
    try:
        from PIL import Image
        with Image.open(io.BytesIO(data)) as image:
            return list(image.size), image.format.lower()
    except (ImportError, OSError):
        return None, None

def verify():
    manifest = json.loads(MANIFEST.read_text())
    for item in manifest['files']:
        path = ROOT / item['path']
        assert path.is_relative_to(ART)
        assert digest(path.read_bytes()) == item['sha256'], path
    print(f"PASS: {len(manifest['files'])} archive entries preserved byte-for-byte")
    legacy = ART / 'provenance/repository-paths.json'
    if legacy.exists():
        entries = json.loads(legacy.read_text())['files']
        for item in entries:
            assert digest((ROOT / item['path']).read_bytes()) == item['sha256'], item['path']
        print(f'PASS: {len(entries)} pre-existing repository sources preserved byte-for-byte')

def import_archive(archive):
    if MANIFEST.exists():
        manifest = json.loads(MANIFEST.read_text())
        assert digest(archive.read_bytes()) == manifest['archive_sha256'], 'Use a new provenance record for a different archive'
        with zipfile.ZipFile(archive) as bundle:
            assert bundle.testzip() is None
            assert {i.filename for i in bundle.infolist() if not i.is_dir()} == {i['archive_path'] for i in manifest['files']}
            for item in manifest['files']:
                assert digest(bundle.read(item['archive_path'])) == item['sha256']
        verify()
        return
    existing = {}
    for p in sorted(ART.rglob('*')):
        if p.is_file():
            existing.setdefault(digest(p.read_bytes()), p)
    records = []
    with zipfile.ZipFile(archive) as bundle:
        assert bundle.testzip() is None, 'Archive CRC failure'
        for item in sorted(bundle.infolist(), key=lambda i: i.filename):
            if item.is_dir():
                continue
            target = destination(item.filename)
            data = bundle.read(item)
            sha = digest(data)
            reused = sha in existing
            if reused:
                target = existing[sha]
            else:
                if target.exists() and target.read_bytes() != data:
                    target = target.with_name(target.stem + '-archive-2026' + target.suffix)
                if target.exists() and target.read_bytes() != data:
                    raise ValueError(f'Conflicting source: {target}')
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                existing[sha] = target
            size, kind = dimensions(data)
            candidate = ROOT / 'data/gfx' / PurePosixPath(item.filename).name
            runtime_size, _ = dimensions(candidate.read_bytes()) if candidate.is_file() else (None, None)
            records.append(dict(archive_path=item.filename, path=str(target.relative_to(ROOT)),
                bytes=len(data), sha256=sha, archive_modified=list(item.date_time),
                reused_identical_bytes=reused, dimensions=size, format=kind or target.suffix.lstrip('.'),
                same_name_runtime_candidate=str(candidate.relative_to(ROOT)) if candidate.is_file() else None,
                runtime_dimensions=runtime_size,
                mapping_status='unreviewed; filename matches are candidates, not approved mappings'))
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(dict(
        archive='glob2-highres.zip', archive_sha256=digest(archive.read_bytes()),
        source_url='https://h.magnenat.net/~steph/glob2-highres.zip',
        source_comment='https://github.com/Globulation2/glob2/pull/207#issuecomment-5590914680',
        supplied_by='Stéphane Magnenat', received='2026-09-08',
        policy='Original bytes preserved. No AI, resizing, conversion, or runtime replacement.',
        files=records), ensure_ascii=False, indent=2) + '\n')
    verify()
    print(f"{sum(r['reused_identical_bytes'] for r in records)} entries reused identical repository/source bytes")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', nargs='?', type=Path, help='ZIP to import; omit to verify committed originals')
    args = parser.parse_args()
    import_archive(args.archive) if args.archive else verify()
