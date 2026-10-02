#!/usr/bin/env python3
"""Runtime data packages for the browser client.

The browser cannot read the game's data directories, so the build copies the files
the game reads at run time into content-addressed packages under `assets/`, and
`browser/asset-loader.js` writes them into the virtual file system:

- `core` holds everything the game needs to start, including every simulation data
  file (deploy/sim_version.py), so startup and determinism are unchanged.
- Optional packages hold files the game tolerates being absent and reads again later:
  in-game music (Engine::prepareRun lists `data/zik/` per match) and the
  high-resolution artwork pack (read when a match or the editor starts). The
  browser shell fetches them in the background after the main menu is up.

The build packages the verified runtime export (tools/package_assets.py, which
re-encodes artwork and drops build scripts) when it has one, or the repository's
own data directories. Translation tooling, packaging metadata and documentation
are left out. `python3 scons/web_assets.py --report` prints the sizes per category
and package of the repository's data.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sys
import zlib

ROOTS = ('data', 'maps', 'campaigns', 'scripts')
# Never read by the game: build and packaging files, documentation, store metadata,
# desktop icons, release screenshots and files no code path refers to.
EXCLUDED_NAMES = {'SConscript'}
EXCLUDED_SUFFIXES = ('.py', '.sh', '.perl', '.md')
EXCLUDED_FILES = {
    'data/glob2.desktop',
    'data/org.globulation2.Globulation2.metainfo.xml',
    'data/texts.pending.txt',     # read by data/check_translations.py only
}
# Images the runtime export may re-encode (PNG to WebP), matched without suffix.
EXCLUDED_IMAGES = {
    'data/gfx/IntroMN',           # unreferenced legacy intro art
}
IMAGE_SUFFIXES = ('.png', '.webp')
EXCLUDED_DIRECTORIES = ('data/icons/', 'data/screenshots/')
# (package, path prefixes). Files in no optional package go to `core`.
OPTIONAL = (
    ('music', ('data/zik/original/',)),
    ('hd', ('data/highres/',)),
)
# Optional packages download in parts so the shell can stop between parts while a
# match is running; each part is also cached on its own.
PART_BYTES = 4 * 1024 * 1024
MANIFEST_VERSION = 1


def excluded(path):
    stem, suffix = os.path.splitext(path)
    return (Path(path).name in EXCLUDED_NAMES or path.endswith(EXCLUDED_SUFFIXES) or
            path in EXCLUDED_FILES or path.startswith(EXCLUDED_DIRECTORIES) or
            (suffix in IMAGE_SUFFIXES and stem in EXCLUDED_IMAGES))


def package_of(path):
    for name, prefixes in OPTIONAL:
        if path.startswith(prefixes):
            return name
    return 'core'


def sim_data_files(root):
    spec = importlib.util.spec_from_file_location('glob2_sim_version', Path(root) / 'deploy/sim_version.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.SIM_DATA_FILES


def source_files(root):
    """Every file under the asset roots, as sorted POSIX paths relative to root."""
    root = Path(root)
    return sorted(p.relative_to(root).as_posix() for directory in ROOTS
                  for p in (root / directory).rglob('*') if p.is_file())


def plan(root, source=None):
    """{package: [paths]} with core first, plus the excluded paths.

    `source` is the tree to package (the runtime export), `root` the repository."""
    packages = {'core': []}
    packages.update((name, []) for name, _ in OPTIONAL)
    skipped = []
    for path in source_files(source or root):
        if excluded(path):
            skipped.append(path)
        else:
            packages[package_of(path)].append(path)
    missing = [f for f in sim_data_files(root) if f not in packages['core']]
    if missing:
        raise ValueError('simulation data must be in the core package: ' + ', '.join(missing))
    return packages, skipped


def split(paths, sizes, limit):
    parts, current, used = [], [], 0
    for path in paths:
        if current and used + sizes[path] > limit:
            parts.append(current)
            current, used = [], 0
        current.append(path)
        used += sizes[path]
    if current:
        parts.append(current)
    return parts


def write_packages(root, output, source=None):
    """Write assets/<package>[-<part>].<hash>.data under output; return the manifest."""
    packages, _ = plan(root, source)
    root, output = Path(source or root), Path(output)
    directory = output / 'assets'
    directory.mkdir(parents=True, exist_ok=True)
    sizes = {path: (root / path).stat().st_size for paths in packages.values() for path in paths}
    manifest, written = {'version': MANIFEST_VERSION, 'packages': []}, set()
    for name, paths in packages.items():
        groups = [paths] if name == 'core' else split(paths, sizes, PART_BYTES)
        entry = {'name': name, 'optional': name != 'core', 'size': sum(sizes[p] for p in paths), 'parts': []}
        for index, group in enumerate(groups):
            blob, files = bytearray(), []
            for path in group:
                start = len(blob)
                blob += (root / path).read_bytes()
                files.append(['/' + path, start, len(blob)])
            digest = hashlib.sha256(blob).hexdigest()[:16]
            stem = name if len(groups) == 1 else f'{name}-{index + 1}'
            filename = f'{stem}.{digest}.data'
            target = directory / filename
            if not target.is_file() or target.stat().st_size != len(blob):
                temporary = target.with_name('.' + filename + '.tmp')
                temporary.write_bytes(blob)
                os.replace(temporary, target)
            written.add(filename)
            entry['parts'].append({'url': 'assets/' + filename, 'size': len(blob), 'files': files})
        manifest['packages'].append(entry)
    # Remove packages (and their precompressed copies) from earlier builds.
    for stale in directory.iterdir():
        base = stale.name[:-3] if stale.name.endswith(('.br', '.gz')) else stale.name
        if base.endswith('.data') and base not in written:
            stale.unlink()
    return manifest


def manifest_script(manifest):
    """The --pre-js that hands the package manifest to browser/asset-loader.js."""
    return ('// Generated by scons/web_assets.py.\n'
            'Module["glob2AssetManifest"] ??= ' + json.dumps(manifest, separators=(',', ':')) + ';\n')


def build(root, output, script, source=None):
    manifest = write_packages(root, output, source)
    text = manifest_script(manifest)
    script = Path(script)
    if not script.is_file() or script.read_text() != text:
        script.write_text(text)
    return manifest


def category(path):
    if excluded(path):
        return 'excluded (tooling, docs, icons, screenshots)'
    for prefix, label in (('data/highres/', 'high-resolution art'), ('data/zik/original/', 'in-game music'),
                          ('data/zik/', 'menu music'), ('data/fonts/', 'font'), ('data/gfx/', 'sprites'),
                          ('data/gui/', 'interface'), ('data/menu/', 'interface'), ('data/texts.', 'translations'),
                          ('maps/', 'maps'), ('campaigns/', 'campaigns'), ('scripts/', 'scripts')):
        if path.startswith(prefix):
            return label
    return 'other game data'


def report(root):
    root = Path(root)
    rows = {}
    packages, skipped = plan(root)
    owner = {path: name for name, paths in packages.items() for path in paths}
    for path in source_files(root):
        data = (root / path).read_bytes()
        key = (category(path), owner.get(path, '-'))
        row = rows.setdefault(key, [0, 0, 0])
        row[0] += 1
        row[1] += len(data)
        row[2] += len(zlib.compress(data, 9))
    print(f'{"category":45} {"package":8} {"files":>6} {"MB":>7} {"gzip MB":>8}')
    for (label, package), (count, raw, packed) in sorted(rows.items(), key=lambda item: (item[0][1], -item[1][1])):
        print(f'{label:45} {package:8} {count:6} {raw / 1e6:7.2f} {packed / 1e6:8.2f}')
    for name, paths in packages.items():
        print(f'package {name}: {len(paths)} files, {sum((root / p).stat().st_size for p in paths) / 1e6:.2f} MB')
    print(f'excluded: {len(skipped)} files')


if __name__ == '__main__':
    repository = Path(__file__).resolve().parent.parent
    if sys.argv[1:] == ['--report']:
        report(repository)
    elif len(sys.argv) == 3:
        build(repository, sys.argv[1], sys.argv[2])
    else:
        sys.exit('usage: web_assets.py --report | web_assets.py <output-dir> <manifest.js>')
