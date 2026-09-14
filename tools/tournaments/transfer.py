"""Bounded checksummed chunks with idempotent, resumable offsets."""
import base64
import hashlib
import os
from pathlib import Path
from .common import CHUNK_BYTES, file_hash, fsync_directory, hash_id, lock


def offset(directory, identity, size):
    hash_id(identity)
    root = Path(directory)
    final, partial = root / identity, root / (identity + '.part')
    if final.exists():
        if final.stat().st_size != size or file_hash(final) != identity:
            raise ValueError('corrupted committed transfer')
        return size
    return partial.stat().st_size if partial.exists() else 0


def put_chunk(directory, identity, size, start, encoded, checksum):
    hash_id(identity)
    if type(size) is not int or size < 0 or type(start) is not int or start < 0:
        raise ValueError('invalid transfer size/offset')
    if len(encoded) > 4 * ((CHUNK_BYTES + 2) // 3):
        raise ValueError('chunk exceeds transfer bound')
    block = base64.b64decode(encoded, validate=True)
    if len(block) > CHUNK_BYTES or hashlib.sha256(block).hexdigest() != checksum or start + len(block) > size:
        raise ValueError('chunk checksum or length mismatch')
    root = Path(directory)
    root.mkdir(parents=True, exist_ok=True)
    with lock(root / (identity + '.lock'), blocking=True):
        final, partial = root / identity, root / (identity + '.part')
        if final.exists():
            return {'offset': offset(root, identity, size), 'complete': True}
        current = partial.stat().st_size if partial.exists() else 0
        if start < current:
            with partial.open('rb') as stream:
                stream.seek(start)
                if stream.read(len(block)) != block:
                    raise ValueError('conflicting retransmission')
            return {'offset': current, 'complete': False}
        if start != current:
            raise ValueError('noncontiguous transfer')
        with partial.open('ab') as stream:
            stream.write(block)
            stream.flush()
            os.fsync(stream.fileno())
        current += len(block)
        if current == size:
            if file_hash(partial) != identity:
                partial.unlink()  # Only corrupt partials, never unacknowledged results.
                raise ValueError('whole-file checksum mismatch; restart transfer')
            os.replace(partial, final)
            fsync_directory(root)
        return {'offset': current, 'complete': current == size}


def get_chunk(directory, identity, start):
    path = Path(directory) / hash_id(identity)
    if type(start) is not int or start < 0 or start > path.stat().st_size:
        raise ValueError('invalid read offset')
    with path.open('rb') as stream:
        stream.seek(start)
        block = stream.read(CHUNK_BYTES)
    return {'offset': start, 'data': base64.b64encode(block).decode(),
            'sha256': hashlib.sha256(block).hexdigest(), 'bytes': path.stat().st_size}


def send_file(transport, path, identity, budget=8):
    path = Path(path)
    size = path.stat().st_size
    start = transport.rpc('offset', identity=identity, size=size)['offset']
    for _ in range(budget):
        if start == size:
            # Empty objects still need a commit operation.
            if size:
                return True
        with path.open('rb') as stream:
            stream.seek(start)
            block = stream.read(CHUNK_BYTES)
        reply = transport.rpc('put', identity=identity, size=size, start=start,
                              data=base64.b64encode(block).decode(), checksum=hashlib.sha256(block).hexdigest())
        start = reply['offset']
        if reply['complete']:
            return True
    return False


def receive_file(transport, directory, identity, size, budget=8):
    start = offset(directory, identity, size)
    if start == size and (Path(directory) / identity).exists():
        return True
    for _ in range(budget):
        block = transport.rpc('get', identity=identity, start=start)
        reply = put_chunk(directory, identity, size, start, block['data'], block['sha256'])
        start = reply['offset']
        if reply['complete']:
            return True
    return False
