"""Bounded immutable per-game matrices for provisional discovery scoring only."""
import json
import hashlib
from dataclasses import asdict
import os
from pathlib import Path
import shutil
import tempfile

from tools.resource_claim_offline import METRIC_FIELDS, file_hash, identity, score_logical_game

MAX_CACHE_BYTES = 2*1024**3
RESERVE_BYTES = 8*1024**3


def describe_inputs(event, catalog, views, wire_path):
    import numpy as np
    catalog=Path(catalog)
    info=json.loads((catalog/'catalog.json').read_text())
    if not info.get('complete') or identity({k:v for k,v in info.items() if k!='identity'})!=info.get('identity'):
        raise ValueError('invalid score cache catalog identity')
    index=info['files']['index.sqlite']
    if (catalog/'index.sqlite').stat().st_size!=index['bytes'] or file_hash(catalog/'index.sqlite')!=index['sha256'] or len(views)!=info['aliases']:
        raise ValueError('score cache alias index differs from attested catalog')
    if not isinstance(views,np.ndarray) or views.shape!=(info['aliases'],8) or views.dtype.kind!='u' or views.dtype.itemsize!=4 or np.any(views>=info['arrays']):
        raise ValueError('invalid score cache alias view matrix')
    effective={'width':event.width,'height':event.height,'unit':event.unit,'nearest':event.nearest,'optimum':event.optimum,
        'destinations_sha256':hashlib.sha256(np.asarray(event.destinations,dtype='<u4').tobytes(order='C')).hexdigest(),
        'candidates':[asdict(c) for _,c in sorted(event.candidates.items())]}
    binding={'decoded_event_sha256':identity(effective),
        'alias_views_sha256':hashlib.sha256(np.asarray(views,dtype='<u4').tobytes(order='C')).hexdigest(),
        'decoder_sha256':file_hash(Path(__file__).with_name('resource_claim_deep.py')),
        'wire_decoder_sha256':file_hash(Path(__file__).parent/'tournaments/claim_capture.py')}
    dtype=np.dtype([(name,'<f8' if name in ('ratio_sum','access_ratio_sum') else '<u8') for name in METRIC_FIELDS])
    shape=(len(views),2);size=shape[0]*shape[1]*dtype.itemsize
    contract={'version':2,**binding,'catalog_identity':info['identity'],'catalog_manifest_sha256':file_hash(catalog/'catalog.json'),
        'scorer_sha256':file_hash(Path(__file__).with_name('resource_claim_offline.py')),
        'wire_sha256':file_hash(wire_path),'game_id':event.game_id,'event_id':event.event_id,
        'provenance':event.provenance,'shape':list(shape),'columns':list(METRIC_FIELDS),'bytes':size}
    return contract,dtype


def score_cached(event, catalog, views, wire_path, cache, *, max_bytes=MAX_CACHE_BYTES):
    import numpy as np
    cache=Path(cache);cache.mkdir(parents=True,exist_ok=True)
    contract,dtype=describe_inputs(event,catalog,views,wire_path)
    shape=tuple(contract['shape']);size=contract['bytes']
    key=identity(contract);binary=cache/(key+'.bin');sidecar=cache/(key+'.json')
    if sidecar.exists():
        metadata=json.loads(sidecar.read_text())
        if metadata.get('contract')!=contract or binary.stat().st_size!=size or file_hash(binary)!=metadata.get('sha256'):
            raise ValueError('score cache attestation mismatch')
        return np.fromfile(binary,dtype=dtype).reshape(shape),True,key
    used=sum(p.stat().st_size for p in cache.iterdir() if p.is_file())
    if binary.exists():raise ValueError('incomplete score cache entry requires explicit recovery')
    if used+size+65536>max_bytes or shutil.disk_usage(cache).free-size-65536<RESERVE_BYTES:
        raise ValueError('score cache byte allowance or free-space reserve exceeded')
    matrix=score_logical_game([event],catalog,views)
    if matrix.shape!=shape or matrix.dtype!=dtype:raise ValueError('unexpected score matrix contract')
    descriptor,temp=tempfile.mkstemp(prefix='.matrix-',dir=cache)
    try:
        with os.fdopen(descriptor,'wb') as stream:
            matrix.tofile(stream);stream.flush();os.fsync(stream.fileno())
        os.replace(temp,binary)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    metadata={'contract':contract,'sha256':file_hash(binary),'scope':'provisional per-game discovery matrix; no policy elimination'}
    descriptor,temp=tempfile.mkstemp(prefix='.metadata-',dir=cache)
    try:
        with os.fdopen(descriptor,'w') as stream:
            json.dump(metadata,stream,sort_keys=True);stream.write('\n');stream.flush();os.fsync(stream.fileno())
        os.replace(temp,sidecar)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return matrix,False,key
