#!/usr/bin/env python3
"""Extract a final-package cohort without accepting links or unlisted files."""
import argparse
import json
import tarfile
from pathlib import Path


def extract(bundle, destination):
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(bundle, 'r:gz') as archive:
        members = archive.getmembers()
        names = [member.name for member in members]
        if len(names) != len(set(names)):
            raise ValueError('duplicate bundle members')
        for member in members:
            if not member.isfile() or Path(member.name).name != member.name or member.name in ('.', '..'):
                raise ValueError('bundle requires regular files with plain basenames')
        member = archive.getmember('package-inventory.json')
        if member.size > 1024 * 1024:
            raise ValueError('inventory is too large')
        inventory = json.load(archive.extractfile(member))
        if inventory.get('schemaVersion') != 2:
            raise ValueError('staged bundle requires a schema-2 inventory')
        expected = {'package-inventory.json'} | {
            item['filename'] for item in inventory['packages'] + inventory['sources']}
        if set(names) != expected:
            raise ValueError('bundle files must exactly match the selected inventory')
        for member in members:
            target = destination / member.name
            if target.exists():
                raise ValueError('refusing to overwrite extracted files')
            with archive.extractfile(member) as source, target.open('xb') as output:
                import shutil
                shutil.copyfileobj(source, output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    extract(args.bundle, args.destination)
