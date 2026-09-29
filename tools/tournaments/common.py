"""Durable records and bounded, content-addressed storage primitives."""
import contextlib
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import tempfile
import time

CHUNK_BYTES = 1024 * 1024


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def file_hash(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(CHUNK_BYTES), b''):
            h.update(block)
    return h.hexdigest()


def fsync_directory(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic_bytes(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix='.' + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(tmp, path)
        fsync_directory(path.parent)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(tmp)


def atomic_json(path, value):
    atomic_bytes(path, canonical(value) + b'\n')


def read_json(path):
    with Path(path).open() as stream:
        return json.load(stream)


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}', value):
        raise ValueError(f'invalid identifier: {value!r}')
    return value


def hash_id(value):
    if not isinstance(value, str) or not re.fullmatch(r'[a-f0-9]{64}', value):
        raise ValueError('expected SHA-256 identity')
    return value


def inside(root, relative):
    """Never allow absolute paths, traversal, or symlink escapes in protocol paths."""
    root = Path(root).resolve()
    path = Path(relative)
    if path.is_absolute() or '..' in path.parts or not path.parts:
        raise ValueError(f'unsafe relative path: {relative}')
    target = (root / path).resolve()
    if not target.is_relative_to(root) or target == root:
        raise ValueError(f'path escapes storage: {relative}')
    return target


def database(path):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path, timeout=30, isolation_level=None)
    db.row_factory = sqlite3.Row
    db.execute('PRAGMA journal_mode=WAL')
    db.execute('PRAGMA synchronous=FULL')
    db.execute('PRAGMA foreign_keys=ON')
    return db


@contextlib.contextmanager
def transaction(db):
    db.execute('BEGIN IMMEDIATE')
    try:
        yield db
        db.execute('COMMIT')
    except BaseException:
        db.execute('ROLLBACK')
        raise


@contextlib.contextmanager
def lock(path, blocking=False):
    import fcntl
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'a+b') as stream:
        fcntl.flock(stream, fcntl.LOCK_EX | (0 if blocking else fcntl.LOCK_NB))
        yield stream


def locked(path):
    try:
        with lock(path):
            return False
    except BlockingIOError:
        return True


def store_artifact(source, directory, compress=True):
    """gzip is deterministic; identities describe the transported bytes."""
    source, directory = Path(source), Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix='.artifact-', dir=directory)
    try:
        with os.fdopen(fd, 'wb') as out, source.open('rb') as inp:
            if compress:
                with gzip.GzipFile(fileobj=out, mode='wb', filename='', mtime=0) as zipper:
                    for block in iter(lambda: inp.read(CHUNK_BYTES), b''):
                        zipper.write(block)
            else:
                for block in iter(lambda: inp.read(CHUNK_BYTES), b''):
                    out.write(block)
            out.flush()
            os.fsync(out.fileno())
        checksum = file_hash(tmp)
        destination = directory / checksum
        os.replace(tmp, destination)
        fsync_directory(directory)
        return {'sha256': checksum, 'bytes': destination.stat().st_size,
                'encoding': 'gzip' if compress else 'identity',
                'raw_sha256': file_hash(source), 'raw_bytes': source.stat().st_size}
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(tmp)


def copy_decoded(source, target, artifact):
    opener = gzip.open if artifact['encoding'] == 'gzip' else open
    h = hashlib.sha256()
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    with opener(source, 'rb') as inp, open(str(target) + '.part', 'wb') as out:
        total = 0
        for block in iter(lambda: inp.read(CHUNK_BYTES), b''):
            total += len(block)
            if total > artifact['raw_bytes']:
                raise ValueError('decompressed artifact exceeds declared size')
            h.update(block)
            out.write(block)
        out.flush()
        os.fsync(out.fileno())
    if total != artifact['raw_bytes'] or h.hexdigest() != artifact['raw_sha256']:
        raise ValueError('decompressed artifact checksum mismatch')
    os.replace(str(target) + '.part', target)
    fsync_directory(target.parent)
