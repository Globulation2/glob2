#!/usr/bin/env python3
"""Regenerate the browsable source-art catalog. Requires Pillow for raster dimensions."""
import json
from collections import Counter
from urllib.parse import quote
from import_originals import ROOT, ART, MANIFEST, dimensions, digest, verify

verify()
archive = json.loads(MANIFEST.read_text())
origins = {}
more = ART/'provenance/stephane-2026-09-08-more.json'
if more.exists():
    from import_more_originals import verify as verify_more
    verify_more()
    archive['files'] += json.loads(more.read_text())['files']
for entry in archive['files']:
    origins.setdefault(entry['path'], []).append(entry['archive_path'])
legacy = {r['path']: r['previous_path'] for r in json.loads((ART/'provenance/repository-paths.json').read_text())['files']}
rows = []
for group in ['originals', 'concept-art', 'reference-exports', 'tools']:
    for path in sorted((ART/group).rglob('*')):
        if not path.is_file(): continue
        data = path.read_bytes()
        size, kind = dimensions(data)
        relative = str(path.relative_to(ROOT))
        rows.append(dict(path=relative, category=str(path.relative_to(ART).parent),
            format=kind or path.suffix.lstrip('.'), dimensions=size, bytes=len(data), sha256=digest(data),
            archive_paths=origins.get(relative, []), previous_repository_path=legacy.get(relative)))
(ART/'provenance/catalog.json').write_text(json.dumps(rows, ensure_ascii=False, indent=2)+'\n')
lines = ['# Original artwork catalog', '',
    'Original file bytes are preserved. Dimensions describe the source canvas, not verified runtime frame coverage. '
    'Concept images and reference exports are not automatically production replacements.', '',
    'Regenerate with `python tools/artwork/catalog_originals.py` (Pillow required for PNG/JPEG dimensions).', '',
    '| Category | Files |', '| --- | ---: |']
for category, count in sorted(Counter(r['category'] for r in rows).items()):
    lines.append(f'| {category} | {count} |')
last = None
for row in rows:
    if row['category'] != last:
        last = row['category']
        lines += ['', f'## {last}', '', '| File | Format | Source size | Provenance |', '| --- | --- | --- | --- |']
    relative = row['path'].removeprefix('datasrc/gfx/')
    size = ' × '.join(map(str,row['dimensions'])) if row['dimensions'] else '—'
    origin = 'Repository + recovered archive' if row['archive_paths'] and row['previous_repository_path'] else 'Recovered archive' if row['archive_paths'] else 'Repository'
    lines.append(f"| [{relative.split('/')[-1]}]({quote(relative)}) | {row['format']} | {size} | {origin} |")
(ART/'CATALOG.md').write_text('\n'.join(lines)+'\n')
print(f'Cataloged {len(rows)} unique source/reference/tool files')
