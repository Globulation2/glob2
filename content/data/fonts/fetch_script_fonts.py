#!/usr/bin/env python3
"""Verify the pinned script fonts; --download restores their exact source bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'scripts/manifest.json').read_text(encoding='utf-8'))
    files = [(ROOT / 'scripts' / (font['name'] + '-Regular.ttf'), font)
             for font in manifest['fonts']]
    files.append((ROOT / 'LICENSE-Noto.txt', manifest['license_file']))
    for path, spec in files:
        if args.download:
            with urllib.request.urlopen(spec['source'], timeout=60) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() != spec['sha256']:
                parser.exit(1, 'Source checksum mismatch: ' + path.name + '\n')
            path.write_bytes(data)
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != spec['sha256']:
            parser.exit(1, 'Missing or changed pinned asset: ' + path.name + '\n')
    print(f'{len(files) - 1} script fonts and SIL OFL licence match pinned source checksums')


if __name__ == '__main__':
    main()
