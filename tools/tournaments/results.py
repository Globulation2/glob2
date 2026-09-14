"""Offline, SQLite-independent committed-result and artifact readers."""
import gzip
from pathlib import Path
from .common import file_hash, hash_id, read_json


class Results:
    def __init__(self, directory):
        self.root = Path(directory)
        self.manifest = read_json(self.root / 'experiment.json')

    def __iter__(self):
        # Manifest order, never completion or filesystem order.
        for job in self.manifest['jobs']:
            path = self.root / 'results' / (job['id'] + '.json')
            if path.exists():
                yield read_json(path)

    def attempts(self):
        for path in sorted((self.root / 'attempts').glob('*.json')):
            yield read_json(path)

    def open_artifact(self, record, name, mode='rt', verify=True):
        meta = next(a for a in record['artifacts'] if a['path'] == name)
        path = self.root / 'artifacts' / hash_id(meta['sha256'])
        if verify and (path.stat().st_size != meta['bytes'] or file_hash(path) != meta['sha256']):
            raise ValueError('artifact checksum mismatch')
        if meta['encoding'] == 'gzip':
            return gzip.open(path, mode)
        if meta['encoding'] != 'identity':
            raise ValueError('unknown artifact encoding')
        return path.open(mode)
