#!/usr/bin/env python3
"""Copy a built browser client into the directory Caddy serves at /play/.

    deploy/install-web-client.py <release-dir> <served-dir>

Content-addressed data packages (assets/*.data) are added first; then each entry
file is replaced by rename together with its precompressed copies, and index.html
goes last, after the files it loads. Caddy bind-mounts the served directory
itself, so files are swapped in place rather than swapping the directory. Data
packages of the previous installation stay for pages that are still loading it;
older ones are removed.
"""
import json
import os
from pathlib import Path
import shutil
import sys

# Runtimes before the loader that picks one, and the page last.
ENTRY_FILES = ('threaded/index.wasm', 'threaded/index.js', 'index.wasm', 'index.js', 'loader.js')
ENCODINGS = ('', '.br', '.gz')
RECORD = '.installed-assets.json'


def localization_files(release):
    """Check localized launchers before changing any files in the served release."""
    if 'src="i18n.js"' not in (release / 'index.html').read_text():
        return ()
    catalogs = Path(__file__).resolve().parents[1] / 'platform/packages/i18n/locales'
    expected = {'locales/' + path.name for path in catalogs.glob('*.json')}
    actual = {'locales/' + path.name for path in (release / 'locales').glob('*.json')}
    if not expected or actual != expected:
        raise ValueError('Browser translation catalog inventory mismatch')
    names = ('i18n.js', *sorted(expected))
    for name in names:
        if not (release / name).is_file():
            raise FileNotFoundError(f'install-web-client: {release / name} is missing')
    return names


def copy(source, target):
    temporary = target.with_name('.' + target.name + '.new')
    shutil.copyfile(source, temporary)
    os.replace(temporary, target)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[2].strip())
    release, served = Path(sys.argv[1]), Path(sys.argv[2])
    localization = localization_files(release)
    (served / 'assets').mkdir(parents=True, exist_ok=True)
    current = sorted(p.name for p in (release / 'assets').iterdir()
                     if p.is_file() and not p.name.startswith('.'))
    for name in current:
        target = served / 'assets' / name
        if not target.is_file() or target.stat().st_size != (release / 'assets' / name).stat().st_size:
            copy(release / 'assets' / name, target)
    (served / 'threaded').mkdir(exist_ok=True)
    for name in ENTRY_FILES + localization + ('studio.html',) + (('generator-studio.html',) if (release / 'generator-studio.html').is_file() else ()) + (('set-preview.html',) if (release / 'set-preview.html').is_file() else ()) + ('index.html',):
        if not (release / name).is_file():
            sys.exit(f'install-web-client: {release / name} is missing')
        for suffix in ENCODINGS[1:]:
            if not (release / (name + suffix)).is_file():
                (served / (name + suffix)).unlink(missing_ok=True)
        # Precompressed copies first: Caddy prefers them, so a stale one would win.
        for suffix in sorted(ENCODINGS, reverse=True):
            if (release / (name + suffix)).is_file():
                (served / name).parent.mkdir(parents=True, exist_ok=True)
                copy(release / (name + suffix), served / (name + suffix))
    # Earlier builds shipped one data file without compression.
    for suffix in ENCODINGS:
        (served / ('index.data' + suffix)).unlink(missing_ok=True)
        (served / ('threaded/index.data' + suffix)).unlink(missing_ok=True)
    record = served / RECORD
    try:
        previous = set(json.loads(record.read_text()))
    except (OSError, ValueError):
        previous = set()
    keep = set(current) | previous
    for stale in (served / 'assets').iterdir():
        if stale.is_file() and stale.name not in keep:
            stale.unlink()
    record.write_text(json.dumps(current) + '\n')
    print(f'web client: {served} ({len(current)} asset files)')


if __name__ == '__main__':
    main()
