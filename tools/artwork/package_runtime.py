#!/usr/bin/env python3
"""Assemble the runtime pack from approved, physically separated artwork.

The explicit capture operation promotes a reviewed runtime export into production
inputs. Normal packaging uses only those inputs and Python's standard library.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = ROOT / 'datasrc/gfx/production'
RUNTIME = ROOT / 'data/highres/v1'
AI = {'current', 'outline_repair', 'painted_repair', 'crystal_repair',
      'resource constrained', 'world constrained'}
MATERIALS = {'shared-material rugged corner masks v5; quiet flat grass',
             'quiet ripples; periodic material v3'}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def category(recipe):
    if recipe.startswith('recovered original'):
        return 'original-derived'
    if recipe in AI:
        return 'ai-upscaled'
    if recipe in MATERIALS:
        return 'ai-materials'
    if recipe == 'soft mask resampling':
        return 'resampled-masks'
    raise ValueError('Unclassified recipe: ' + recipe)

def capture():
    manifest = json.loads((RUNTIME / 'manifest.json').read_text())
    entries = []
    for frame in manifest['frames']:
        for layer in frame['layers']:
            entries.append((layer['file'], category(frame['recipe']) + '/' + layer['file']))
    for atlas in ('terrain_atlas', 'resource_atlas'):
        for level in manifest.get(atlas, {}).get('levels', []):
            entries.append((level['file'], 'atlases/' + level['file']))
    entries += [(name, 'pack-metadata/' + name) for name in ('manifest.json', 'frames.txt', 'README.md')]
    records = []
    for name, relative in entries:
        source = RUNTIME / name
        target = PRODUCTION / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        records.append(dict(runtime=name, source=relative, sha256=sha(source)))
    index = PRODUCTION / 'package.json'
    index.write_text(json.dumps(dict(version=1, files=records), indent=2) + '\n')
    # Detect obsolete category files instead of silently shipping them.
    validate()
    print('Captured approved artwork into separate production folders')

def validate():
    index = json.loads((PRODUCTION / 'package.json').read_text())
    assert index['version'] == 1
    records = index['files']
    assert len({r['runtime'] for r in records}) == len(records)
    assert len({r['source'] for r in records}) == len(records)
    for r in records:
        assert Path(r['runtime']).name == r['runtime']
        assert not Path(r['source']).is_absolute() and '..' not in Path(r['source']).parts
        assert sha(PRODUCTION / r['source']) == r['sha256'], r['source']
    manifest = json.loads((PRODUCTION / 'pack-metadata/manifest.json').read_text())
    by_name = {r['runtime']: r for r in records}
    expected = {'manifest.json', 'frames.txt', 'README.md'}
    for frame in manifest['frames']:
        for layer in frame['layers']:
            name = layer['file']; expected.add(name)
            assert by_name[name]['source'] == category(frame['recipe']) + '/' + name
            assert by_name[name]['sha256'] == layer['sha256']
    for atlas in ('terrain_atlas', 'resource_atlas'):
        for level in manifest.get(atlas, {}).get('levels', []):
            name = level['file']; expected.add(name)
            assert by_name[name]['source'] == 'atlases/' + name
            assert by_name[name]['sha256'] == level['sha256']
    assert set(by_name) == expected
    actual = {str(p.relative_to(PRODUCTION)) for p in PRODUCTION.rglob('*') if p.is_file()}
    assert actual - {'package.json', 'README.md'} == {r['source'] for r in records}
    return records

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--capture-approved', action='store_true',
                        help='Explicitly promote the reviewed current runtime pack to production inputs')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, default=RUNTIME)
    args = parser.parse_args()
    if args.capture_approved:
        capture()
        return
    records = validate()  # Validate the complete pack before writing any output.
    if args.check:
        print('PASS separated categories, complete frame/atlas coverage and all production hashes')
        return
    args.output.mkdir(parents=True, exist_ok=True)
    for r in records:
        shutil.copyfile(PRODUCTION / r['source'], args.output / r['runtime'])
    print('Packaged %d files without experiment, model or image-processing dependencies' % len(records))

if __name__ == '__main__':
    main()
