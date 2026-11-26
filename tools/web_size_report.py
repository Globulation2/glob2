#!/usr/bin/env python3
"""Measure what a packaged browser client costs players to download.

Run it on the output of platforms/web/package-static.py, the directory that is
published. It reports every shipped file raw, as gzip -9 and as Brotli (the
encoding /play/ serves), groups them into categories, and derives two totals
from the asset manifest built into the runtime:

  startup     the page, the serial runtime and the required asset packages,
              everything fetched before the game starts
  per player  startup plus every optional asset package and worker an
              English-speaking player can fetch (one runtime, one locale)

--label appends the measurement to a JSONL ledger, and --ledger prints the
ledger as a step table, so a series of builds becomes a before/after record.
Compressed sizes are cached by content hash beside the ledger.
"""

import argparse
import gzip
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from buildsys.web_assets import BROTLI_WINDOW, DATA_BROTLI_QUALITY  # noqa: E402

try:
    import brotli
except ImportError:  # pragma: no cover - reported at run time
    brotli = None

DEFAULT_LEDGER = ROOT / 'artifacts/lite-size/steps.jsonl'
TEXT_BROTLI_QUALITY = 11
ASSET = re.compile(r'^assets/(?P<package>[a-z0-9_-]+?)(?:-\d+)?\.[0-9a-f]{16}\.data$')
# The runtime's JavaScript is minified, so the manifest's keys may lose their quotes.
PACKAGE = re.compile(r'"?name"?:"(?P<name>[a-z0-9_-]+)","?optional"?:(?P<optional>true|false|!0|!1)')
# Duplicates of index.html served under other names, and files no player fetches.
NOT_FETCHED = {'generator-studio.html', 'studio.html', 'set-preview.html', 'package.json', 'SHA256SUMS'}


def category(name):
    """Return (category, fetched by an English player) for a shipped file."""
    match = ASSET.match(name)
    if match:
        # Terrain families load one by one on demand; report them as one group.
        package = 'hd-terrain-*' if match['package'].startswith('hd-terrain-') else match['package']
        return 'asset:' + package, True
    if name.startswith('threaded/'):
        return 'runtime:threaded', False
    if re.match(r'^index-[0-9a-f]{16}\.(js|wasm|data)$', name):
        return 'runtime:serial', True
    if name.startswith('locales/'):
        return 'locales', name == 'locales/en.json'
    if name.startswith('fonts/') or name == 'script-fonts.css':
        # script-fonts.css loads each face by unicode-range: an English page fetches none.
        return 'launcher-fonts', name == 'script-fonts.css'
    if name.startswith('licenses/'):
        return 'licenses', False
    for worker in ('music', 'hive', 'recording', 'asset'):
        if Path(name).name.startswith(worker + '-'):
            return 'worker:' + ('asset-video' if worker == 'asset' else worker), True
    return 'shell', True


class Compressor:
    def __init__(self, cache_path):
        self.path = cache_path
        try:
            self.cache = json.loads(cache_path.read_text())
        except (OSError, ValueError):
            self.cache = {}

    def sizes(self, data, name):
        key = hashlib.sha256(data).hexdigest()
        if key not in self.cache:
            quality = DATA_BROTLI_QUALITY if name.endswith('.data') else TEXT_BROTLI_QUALITY
            self.cache[key] = [len(gzip.compress(data, 9, mtime=0)),
                               len(brotli.compress(data, quality=quality, lgwin=BROTLI_WINDOW))]
        return self.cache[key]

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(self.cache))


def required_packages(directory):
    runtime = next(directory.glob('index-*.js'), None)
    if runtime is None:
        raise SystemExit(f'{directory} is not a package-static.py output (no index-<version>.js)')
    packages = {m['name']: m['optional'] in ('false', '!1') for m in PACKAGE.finditer(runtime.read_text(errors='replace'))}
    if not any(packages.values()):
        raise SystemExit(f'{runtime} lists no required asset packages')
    return packages


def measure(directory, compressor):
    packages = required_packages(directory)
    files = []
    for path in sorted(p for p in directory.rglob('*') if p.is_file()):
        name = path.relative_to(directory).as_posix()
        if name.endswith('.gz') or name.endswith('.br') or name in NOT_FETCHED:
            continue
        data = path.read_bytes()
        group, fetched = category(name)
        gz, br = compressor.sizes(data, name)
        startup = fetched and (group in ('shell', 'runtime:serial', 'locales', 'launcher-fonts')
                               or (group.startswith('asset:') and packages.get(group[6:], False)))
        if group == 'shell' and not startup:
            raise SystemExit(f'unclassified file {name}')
        files.append(dict(name=name, category=group, raw=len(data), gzip=gz, brotli=br,
                          per_player=fetched, startup=startup))
    return files


def summarize(files):
    categories = {}
    for f in files:
        row = categories.setdefault(f['category'], dict(raw=0, gzip=0, brotli=0, files=0))
        for key in ('raw', 'gzip', 'brotli'):
            row[key] += f[key]
        row['files'] += 1
    totals = {}
    for scope in ('startup', 'per_player'):
        chosen = [f for f in files if f[scope]]
        totals[scope] = {key: sum(f[key] for f in chosen) for key in ('raw', 'gzip', 'brotli')}
    totals['shipped'] = {key: sum(f[key] for f in files) for key in ('raw', 'gzip', 'brotli')}
    return categories, totals


def mb(value):
    return f'{value / 1e6:8.2f}'


def print_report(categories, totals):
    print(f'{"category":28} {"files":>5} {"raw MB":>8} {"gzip MB":>8} {"br MB":>8}')
    for name, row in sorted(categories.items(), key=lambda item: -item[1]['brotli']):
        print(f'{name:28} {row["files"]:5} {mb(row["raw"])} {mb(row["gzip"])} {mb(row["brotli"])}')
    print()
    for scope, label in (('startup', 'startup'), ('per_player', 'per player (one runtime)'),
                         ('shipped', 'everything shipped')):
        t = totals[scope]
        print(f'{label:34} {mb(t["raw"])} {mb(t["gzip"])} {mb(t["brotli"])}')


def print_ledger(path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    print(f'{"step":44} {"startup br":>10} {"player br":>10} {"Δ player":>9} {"wasm br":>8}')
    previous = None
    for row in rows:
        player = row['totals']['per_player']['brotli']
        wasm = row['categories'].get('runtime:serial', {}).get('brotli', 0)
        delta = '' if previous is None else f'{(player - previous) / 1e6:+9.2f}'
        print(f'{row["label"][:44]:44} {mb(row["totals"]["startup"]["brotli"]):>10} {mb(player):>10} {delta:>9} {mb(wasm)}')
        previous = player


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('directory', nargs='?', type=Path, help='package-static.py output directory')
    parser.add_argument('--label', help='append this measurement to the ledger under this step name')
    parser.add_argument('--ledger', type=Path, default=DEFAULT_LEDGER)
    parser.add_argument('--show-ledger', action='store_true', help='print the ledger as a step table')
    parser.add_argument('--files', type=int, default=0, help='also list the N largest files by Brotli size')
    args = parser.parse_args()
    if args.show_ledger:
        print_ledger(args.ledger)
        return
    if args.directory is None:
        parser.error('a package directory is required')
    if brotli is None:
        raise SystemExit('brotli is required: pip install -r requirements-dev.txt')
    compressor = Compressor(args.ledger.parent / 'compressed-sizes.json')
    files = measure(args.directory, compressor)
    compressor.save()
    categories, totals = summarize(files)
    print_report(categories, totals)
    if args.files:
        print()
        for f in sorted(files, key=lambda f: -f['brotli'])[:args.files]:
            print(f'{f["name"][:60]:60} {mb(f["brotli"])}')
    if args.label:
        args.ledger.parent.mkdir(parents=True, exist_ok=True)
        with args.ledger.open('a') as out:
            out.write(json.dumps(dict(label=args.label, directory=str(args.directory),
                                      categories=categories, totals=totals)) + '\n')


if __name__ == '__main__':
    main()
