"""Audit exact native shard ownership, independently of the shard scheduler."""
import argparse
from collections import Counter
import json
from pathlib import Path


def audit(rows):
    if not rows:
        raise ValueError('No case inventories were retained')
    eligible = rows[0]['eligible']
    if not eligible:
        raise ValueError('Selected native inventory is empty')
    if any(row['eligible'] != eligible or row['profile'] != rows[0]['profile'] for row in rows):
        raise ValueError('Shard inventories disagree about selected coverage')
    counts = Counter(case for row in rows for case in row['assigned'])
    missing = set(eligible) - counts.keys()
    extra = counts.keys() - set(eligible)
    duplicates = {case for case, count in counts.items() if count != 1}
    if missing or extra or duplicates:
        raise ValueError(f'missing={sorted(missing)}, extra={sorted(extra)}, repeated={sorted(duplicates)}')
    return len(counts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--shards', type=int, default=4)
    args = parser.parse_args()
    rows = [json.loads(path.read_text()) for path in args.directory.rglob('engine.inventory.json')]
    if len(rows) != args.shards:
        raise SystemExit(f'Expected {args.shards} engine shard inventories, found {len(rows)}')
    print(f'{audit(rows)} selected engine cases assigned exactly once')


if __name__ == '__main__':
    main()
