#!/usr/bin/env python3
"""Assemble approved PNG source artwork, then optionally export shared WebP assets.

Approved artwork stays lossless in the repository. Client builds and packaging
use tools/package_assets.py for the single runtime encoding policy.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
PRODUCTION = ROOT / 'datasrc/gfx/production'
RUNTIME = ROOT / 'data/highres/v1'
AI = {'current', 'outline_repair', 'painted_repair', 'crystal_repair',
      'resource constrained', 'world constrained'}
MATERIALS = {'shared-material rugged corner masks v5; quiet flat grass',
             'quiet ripples; periodic material v3'}
ORIGINAL_MATERIALS = {'original-based grass and sand v1; retained rugged corner masks'}
METADATA = ('manifest.json', 'frames.txt', 'README.md')
ATLASES = ('terrain_atlas', 'resource_atlas')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def category(recipe):
    if recipe.startswith('recovered original'):
        return 'original-derived'
    if recipe.startswith('hand-authored SVG'):
        return 'authored'
    if recipe in AI:
        return 'ai-upscaled'
    if recipe in MATERIALS:
        return 'ai-materials'
    if recipe in ORIGINAL_MATERIALS:
        return 'original-materials'
    if recipe.startswith(('procedural terrain', 'procedural resource')):
        return 'procedural-materials'
    if recipe.startswith('generated terrain material'):
        return 'ai-materials'
    if recipe == 'soft mask resampling':
        return 'resampled-masks'
    raise ValueError('Unclassified recipe: ' + recipe)


def inventory(manifest):
    """Map source filenames to approved category paths and expected hashes."""
    entries = {}

    def add(name, folder, expected_hash=None):
        require(isinstance(name, str) and name not in {'', '.', '..'} and
                Path(name).name == name and '\\' not in name, f'Invalid artwork filename: {name}')
        require(name not in entries, f'Duplicate artwork file: {name}')
        entries[name] = (folder + '/' + name, expected_hash)

    require(manifest['version'] == 1 and manifest['frames'], 'Invalid or empty artwork manifest')
    frame_ids = set()
    for frame in manifest['frames']:
        require(frame['id'] not in frame_ids, 'Duplicate artwork frame: ' + frame['id'])
        frame_ids.add(frame['id'])
        roles = set()
        for layer in frame['layers']:
            role = layer['role']
            require(role in {'base', 'team'} and role not in roles, 'Invalid/duplicate layer: ' + frame['id'])
            roles.add(role)
            suffix = 'r.png' if role == 'team' else '.png'
            require(layer['file'] == frame['id'] + suffix, 'Incorrect layer filename: ' + layer['file'])
            add(layer['file'], category(frame['recipe']), layer['sha256'])
        require(roles, 'Missing artwork layers: ' + frame['id'])
    for atlas in ATLASES:
        for level in manifest.get(atlas, {}).get('levels', []):
            add(level['file'], 'atlases', level['sha256'])
    for name in METADATA:
        add(name, 'pack-metadata')
    return entries


def capture(production=PRODUCTION, source=RUNTIME):
    """Explicitly record a complete reviewed source selection; never encode it."""
    manifest = json.loads((source / 'manifest.json').read_text())
    entries = inventory(manifest)
    # Check the complete selection before modifying production inputs.
    records = []
    for name, (relative, expected_hash) in entries.items():
        actual_hash = sha(source / name)
        require(expected_hash is None or actual_hash == expected_hash, 'Source hash differs: ' + name)
        records.append(dict(runtime=name, source=relative, sha256=actual_hash))
    index = production / 'package.json'
    if index.exists():
        previous = json.loads(index.read_text())['files']
        order = {record['runtime']: i for i, record in enumerate(previous)}
        records.sort(key=lambda record: order.get(record['runtime'], len(order)))
    existing = {str(p.relative_to(production)) for p in production.rglob('*') if p.is_file()}
    obsolete = existing - {'package.json', 'README.md'} - {r['source'] for r in records}
    require(not obsolete, f'Remove obsolete approved inputs explicitly before capture: {sorted(obsolete)}')
    for record in records:
        target = production / record['source']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / record['runtime'], target)
    (production / 'package.json').write_text(json.dumps(dict(version=1, files=records), indent=2) + '\n')
    validate(production)
    print('Captured approved source artwork into separate production folders')


def validate(production=PRODUCTION):
    index = json.loads((production / 'package.json').read_text())
    require(index['version'] == 1, 'Invalid production index version')
    records = index['files']
    require(len({r['runtime'] for r in records}) == len(records), 'Duplicate production output paths')
    require(len({r['source'] for r in records}) == len(records), 'Duplicate production source paths')
    for record in records:
        name, relative = record['runtime'], record['source']
        require(name not in {'', '.', '..'} and Path(name).name == name and '\\' not in name,
                'Invalid output filename: ' + name)
        require(not Path(relative).is_absolute() and '..' not in Path(relative).parts and '\\' not in relative,
                'Invalid production path: ' + relative)
        path = production / relative
        require(path.resolve().is_relative_to(production.resolve()), 'Production path escapes source tree: ' + relative)
        require(sha(path) == record['sha256'], 'Production hash differs: ' + relative)
    manifest = json.loads((production / 'pack-metadata/manifest.json').read_text())
    expected = inventory(manifest)
    by_name = {record['runtime']: record for record in records}
    require(by_name.keys() == expected.keys(), 'Production index differs from complete frame/atlas inventory')
    for name, (relative, expected_hash) in expected.items():
        require(by_name[name]['source'] == relative, 'Artwork category differs: ' + name)
        require(expected_hash is None or by_name[name]['sha256'] == expected_hash, 'Manifest hash differs: ' + name)
    actual = {str(p.relative_to(production)) for p in production.rglob('*') if p.is_file()}
    require(actual - {'package.json', 'README.md'} == {r['source'] for r in records}, 'Unindexed production files')
    return records


def assemble(production, output):
    records = validate(production)  # Validate the whole pack before any output writes.
    require(not output.resolve().is_relative_to(production.resolve()) and
            not production.resolve().is_relative_to(output.resolve()), 'Source export overlaps production inputs')
    if output.exists():
        expected = {record['runtime'] for record in records}
        unexpected = [path.name for path in output.iterdir()
                      if not path.name.startswith('.') and path.name not in expected]
        require(not unexpected, f'Unindexed source export files: {sorted(unexpected)}')
        require(not any((output / name).is_symlink() for name in expected),
                'Source export contains symlinked artwork')
    output.mkdir(parents=True, exist_ok=True)
    for record in records:
        target = output / record['runtime']
        # Keep unchanged source timestamps stable so a repeated assembly does
        # not invalidate the shared client's asset-export build inputs.
        if not target.is_file() or sha(target) != record['sha256']:
            shutil.copyfile(production / record['source'], target)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--capture-approved', action='store_true', help='Promote the reviewed source pack to production inputs')
    modes.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, default=RUNTIME, help='Approved PNG source export, not a playable runtime tree')
    parser.add_argument('--runtime-output', type=Path, help='Also export a complete WebP client asset tree with the shared exporter')
    parser.add_argument('--platform', default='generic')
    parser.add_argument('--lossless-images', action='store_true', help='Require exact RGBA in the optional WebP export')
    args = parser.parse_args()
    if args.runtime_output and (args.check or args.capture_approved or args.output.resolve() != RUNTIME):
        parser.error('--runtime-output requires assembly into the standard source pack')
    try:
        if args.capture_approved:
            capture()
        elif args.check:
            validate()
            print('PASS separated categories, complete frame/atlas coverage and all production hashes')
        else:
            records = assemble(PRODUCTION, args.output)
            print(f'Assembled {len(records)} approved source files without model inference')
            if args.runtime_output:
                sys.path.insert(0, str(ROOT))
                from tools.package_assets import export_assets
                export_assets(ROOT, args.runtime_output, platform=args.platform, lossy=not args.lossless_images)
                print('Exported WebP client assets with the shared encoding policy')
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'Artwork packaging failed: {error}\n')


if __name__ == '__main__':
    main()
