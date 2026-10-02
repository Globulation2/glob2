#!/usr/bin/env python3
"""Runtime data packages for the browser client.

The browser cannot read the game's data directories, so the build copies the files
the game reads at run time into content-addressed packages under `assets/`, and
`browser/asset-loader.js` writes them into the virtual file system:

- `core` holds what the main menu, the online hub and rooms need, including every
  simulation data file (deploy/sim_version.py), so startup and determinism are
  unchanged. Three files are smaller browser copies (browser/derive_assets.py):
  the font without its CJK outlines, the menu backdrop as a JPEG and the wordmark
  without the area the menu never shows.
- `game`: the in-game sprites (GlobalContainer::loadGameGraphics and the building
  artwork). The page downloads it first after the main menu is up; matches and the
  editor wait for it ("Loading game graphics").
- `font-cjk`: the full font, replacing the core copy. A startup package for a
  Chinese, Japanese or Korean interface; otherwise it follows in the background so
  CJK player names and language names get their glyphs.
- `translations`: the full catalogs of every language but English. Core has
  each language's name and code only; a startup package for an interface in
  another language, otherwise the game reloads its string table when it arrives.
- `menu-music`, `music` (in-game) and `hd` (high-resolution artwork): the game
  tolerates them being absent and loads them when they arrive or when a match
  starts.

Build scripts, translation tooling, packaging metadata and documentation are left
out. `python3 scons/web_assets.py --report` prints the sizes per category and
package.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
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
    'data/gfx/IntroMN.png',       # unreferenced legacy intro art
}
EXCLUDED_DIRECTORIES = ('data/icons/', 'data/screenshots/')
# (package, path prefixes) in download order after `game`, which game_files()
# selects. Files in no other package go to `core`.
OPTIONAL = (
    ('font-cjk', ('data/fonts/sans.ttf',)),
    ('menu-music', ('data/zik/intro.ogg', 'data/zik/menu.ogg')),
    ('translations', ('data/texts.',)),
    ('music', ('data/zik/original/',)),
    ('hd', ('data/highres/',)),
)
PACKAGES = ('core', 'game') + tuple(name for name, _ in OPTIONAL)
# Also in `game`: the game cursor's frames. The browser always shows the system
# cursor (the page passes -C and the setting is hidden there), so nothing loads
# them before a match.
GAME_EXTRA = ('data/gfx/cursor/',)
# Later packages download in parts so the shell can stop between parts while a
# match is running; each part is also cached on its own.
PART_BYTES = 4 * 1024 * 1024
MANIFEST_VERSION = 1
SPRITE = re.compile(r'"data/gfx/([A-Za-z0-9_-]+)"')
# Core keeps the key list, English and, of every other language, only what the
# language list needs (stub()); `translations` brings the full catalogs.
CORE_TEXTS = ('data/texts.keys.txt', 'data/texts.list.txt', 'data/texts.incomplete.txt', 'data/texts.en.txt')
STUB_KEYS = ('[language]', '[language incomplete]', '[language-code]')


def translation(path):
    return path.startswith('data/texts.') and path.endswith('.txt') and path not in CORE_TEXTS


def stub(data):
    """A catalog with only its language's name and code; StringTable shows English for the rest."""
    lines = data.decode('utf-8').split('\n')
    kept = []
    for index in range(0, len(lines) - 1, 2):
        if lines[index] in STUB_KEYS:
            kept += lines[index:index + 2]
    return ('\n'.join(kept) + '\n').encode('utf-8')


def excluded(path):
    return (Path(path).name in EXCLUDED_NAMES or path.endswith(EXCLUDED_SUFFIXES) or
            path in EXCLUDED_FILES or path.startswith(EXCLUDED_DIRECTORIES))


def load_module(root, name, path):
    spec = importlib.util.spec_from_file_location(name, Path(root) / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sim_data_files(root):
    return load_module(root, 'glob2_sim_version', 'deploy/sim_version.py').SIM_DATA_FILES


def derived_assets(root):
    """{core path: browser copy} for the browser copies that still match their sources.

    A copy whose source changed after browser/derive_assets.py wrote it is not used:
    core then ships the original (and, for the font, there is no font-cjk)."""
    derive = load_module(root, 'glob2_derive_assets', 'browser/derive_assets.py')
    try:
        recorded = json.loads((Path(root) / 'browser/assets/sources.json').read_text())
    except (OSError, ValueError):
        return {}
    names = hashlib.sha256(''.join(sorted(derive.language_names(root))).encode()).hexdigest()
    current = {}
    for source, target in derive.DERIVED.items():
        entry = recorded.get(target, {})
        if ((Path(root) / target).is_file() and entry.get('source') == source and
                entry.get('sha256') == derive.digest(Path(root) / source) and
                entry.get('languageNames', names) == names):
            current[source] = target
    return current


def game_sprites(root):
    """Names of the sprites GlobalContainer::loadGameGraphics loads, buildings included."""
    root = Path(root)
    text = (root / 'src/GlobalContainer.cpp').read_text()
    body = text[text.index('void GlobalContainer::loadGameGraphics'):]
    names = set(SPRITE.findall(body[:body.index('\n}\n')]))
    for path in sorted((root / 'src/game/entities').glob('BuildingTypes*.cpp')):
        names.update(SPRITE.findall(path.read_text()))
    if not {'unit', 'terrain', 'gamegui', 'swarm0b'} <= names:
        raise ValueError('could not find the game sprites in src/GlobalContainer.cpp')
    return names


def game_files(paths, sprites):
    """Frames of these sprites: data/gfx/<name><index>[r].png, as Sprite::load reads them."""
    files = set()
    for path in paths:
        if not path.startswith('data/gfx/') or '/' in path[len('data/gfx/'):]:
            continue
        # Names may end in digits themselves: inn0b's frames are inn0b0.png, inn0b1.png.
        if any(path.startswith('data/gfx/' + name) and
               re.fullmatch(r'\d+r?\.png', path[len('data/gfx/' + name):]) for name in sprites):
            files.add(path)
    return files


def source_files(root):
    """Every file under the asset roots, as sorted POSIX paths relative to root."""
    root = Path(root)
    return sorted(p.relative_to(root).as_posix() for directory in ROOTS
                  for p in (root / directory).rglob('*') if p.is_file())


def plan(root):
    """({package: [paths]} with core first, excluded paths, {core path: browser copy})."""
    packages = {name: [] for name in PACKAGES}
    files, skipped = [], []
    for path in source_files(root):
        (skipped if excluded(path) else files).append(path)
    game = game_files(files, game_sprites(root))
    substitutes = derived_assets(root)
    for path in files:
        name = 'game' if path in game or path.startswith(GAME_EXTRA) else next(
            (name for name, prefixes in OPTIONAL if path.startswith(prefixes)), 'core')
        if name == 'font-cjk':
            # Core has the font; with a current browser copy, core's is the copy and
            # font-cjk carries the full one.
            packages['core'].append(path)
            if path in substitutes:
                packages['font-cjk'].append(path)
            continue
        if name == 'translations':
            # Core ships the stub (contents()); the full catalog replaces it.
            if translation(path):
                packages['core'].append(path)
                packages['translations'].append(path)
            else:
                packages['core'].append(path)
            continue
        packages[name].append(path)
    missing = [f for f in sim_data_files(root) if f not in packages['core']]
    if missing:
        raise ValueError('simulation data must be in the core package: ' + ', '.join(missing))
    packages = {name: paths for name, paths in packages.items() if paths or name == 'core'}
    return packages, skipped, substitutes


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


def contents(root, name, path, substitutes):
    """The bytes package `name` ships as `path`."""
    if name == 'core' and translation(path):
        return stub((Path(root) / path).read_bytes())
    return (Path(root) / (substitutes.get(path, path) if name == 'core' else path)).read_bytes()


def write_packages(root, output):
    """Write assets/<package>[-<part>].<hash>.data under output; return the manifest."""
    root, output = Path(root), Path(output)
    directory = output / 'assets'
    directory.mkdir(parents=True, exist_ok=True)
    packages, _, substitutes = plan(root)
    manifest, written = {'version': MANIFEST_VERSION, 'packages': []}, set()
    for name, paths in packages.items():
        data = {path: contents(root, name, path, substitutes) for path in paths}
        sizes = {path: len(value) for path, value in data.items()}
        groups = [paths] if name == 'core' else split(paths, sizes, PART_BYTES)
        entry = {'name': name, 'optional': name != 'core', 'size': sum(sizes.values()), 'parts': []}
        for index, group in enumerate(groups):
            blob, files = bytearray(), []
            for path in group:
                start = len(blob)
                blob += data[path]
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


def build(root, output, script):
    manifest = write_packages(root, output)
    text = manifest_script(manifest)
    script = Path(script)
    if not script.is_file() or script.read_text() != text:
        script.write_text(text)
    return manifest


def category(path, game=()):
    if excluded(path):
        return 'excluded (tooling, docs, icons, screenshots)'
    if path in game:
        return 'game sprites'
    for prefix, label in (('data/highres/', 'high-resolution art'), ('data/zik/original/', 'in-game music'),
                          ('data/zik/', 'menu music'), ('data/fonts/', 'font'), ('data/gfx/', 'menu sprites'),
                          ('data/gui/', 'interface'), ('data/menu/', 'interface'), ('data/texts.', 'translations'),
                          ('maps/', 'maps'), ('campaigns/', 'campaigns'), ('scripts/', 'scripts')):
        if path.startswith(prefix):
            return label
    return 'other game data'


def report(root):
    root = Path(root)
    packages, skipped, substitutes = plan(root)
    game = set(packages.get('game', ()))
    rows = {}
    entries = [(name, path, contents(root, name, path, substitutes)) for name, paths in packages.items() for path in paths]
    entries += [('-', path, (root / path).read_bytes()) for path in skipped]
    for name, path, data in entries:
        row = rows.setdefault((category(path, game), name), [0, 0, 0])
        row[0] += 1
        row[1] += len(data)
        row[2] += len(zlib.compress(data, 9))
    order = {name: index for index, name in enumerate(PACKAGES + ('-',))}
    print(f'{"category":45} {"package":10} {"files":>6} {"MB":>7} {"gzip MB":>8}')
    for (label, package), (count, raw, packed) in sorted(rows.items(), key=lambda item: (order[item[0][1]], -item[1][1])):
        print(f'{label:45} {package:10} {count:6} {raw / 1e6:7.2f} {packed / 1e6:8.2f}')
    for name, paths in packages.items():
        size = sum(len(contents(root, name, p, substitutes)) for p in paths)
        print(f'package {name}: {len(paths)} files, {size / 1e6:.2f} MB')
    print(f'excluded: {len(skipped)} files')
    print('browser copies in core: ' + (', '.join(sorted(substitutes)) or 'none (run browser/derive_assets.py)'))


if __name__ == '__main__':
    repository = Path(__file__).resolve().parent.parent
    if sys.argv[1:] == ['--report']:
        report(repository)
    elif len(sys.argv) == 3:
        build(repository, sys.argv[1], sys.argv[2])
    else:
        sys.exit('usage: web_assets.py --report | web_assets.py <output-dir> <manifest.js>')
