"""Shared source inventory for tutorial authoring and release validation."""
import json
from pathlib import Path

CATALOG = 'scripts/tutorials/catalog.json'
RUNTIME = 'scripts/tutorials/runtime.js'


def assets(root):
    """The runtime tree these paths name: a checkout's content/, or root itself
    for installed and staged trees."""
    content = Path(root) / 'content'
    return content if content.is_dir() else Path(root)


def catalog_lessons(root):
    root = assets(root)
    entries = json.loads((root / CATALOG).read_text())
    if not isinstance(entries, list) or not 1 <= len(entries) <= 64:
        raise ValueError('Expected 1–64 tutorial catalog entries')
    return [(entry, json.loads((root / entry['definition']).read_text())) for entry in entries]


def packaged_assets(root):
    paths = [CATALOG, RUNTIME]
    for entry, definition in catalog_lessons(root):
        paths.extend((entry['definition'], entry['script'], definition['map']))
    return tuple(dict.fromkeys(paths))


def reject_retired_assets(root):
    root = assets(root)
    candidates = [root / 'campaigns/Tutorial_Campaign.txt',
                  *root.glob('campaigns/tutorial-part*.map*'),
                  *root.glob('scripts/tutorial_part*.sgsl')]
    retired = sorted(str(path.relative_to(root)) for path in candidates if path.is_file())
    if retired:
        raise ValueError('retired tutorial assets must not ship: ' + ', '.join(retired))
