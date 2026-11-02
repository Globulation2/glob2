"""Temporary, bounded reader for native claim min-hash reservoir captures."""
import hashlib
import json
from pathlib import Path
import struct

from tools.resource_claim_offline import Candidate, FrozenEvent

MAGIC = b'G2CLMD01'
HEADER = struct.Struct('<8sIIII')  # magic, version, metadata bytes, cells, candidates
CANDIDATE = struct.Struct('<IIIIQQ')
MAX_RECORD = 10 * 1024**2
MAX_METADATA = 64 * 1024
DESIGN = 'independent-minhash-one-per-game-over-sampled-contested-published-xy'


def validate_manifest(manifest):
    if manifest.get('version') != 1 or manifest.get('complete') is not True or manifest.get('censored') is not False:
        raise ValueError('incomplete or censored deep capture')
    if manifest.get('design') != DESIGN:
        raise ValueError('invalid deep inclusion design')
    counters=('eligible','ordinal','captured_ordinal','replacements','excluded_eligible_admission',
              'excluded_eligible_sample_cap','excluded_eligible_queue_cap')
    if any(type(manifest.get(k)) is not int or not 0<=manifest[k]<=0xffffffffffffffff for k in counters):
        raise ValueError('invalid deep reservoir counters')
    if manifest['excluded_eligible_admission'] != manifest['excluded_eligible_sample_cap']+manifest['excluded_eligible_queue_cap'] or manifest['excluded_eligible_admission']:
        raise ValueError('deep admission exclusions contradict uncensored claim')
    if manifest['eligible']:
        if not 0<manifest['ordinal']==manifest['captured_ordinal'] or not 1<=manifest['replacements']<=manifest['eligible']:
            raise ValueError('deep captured minimum/counter mismatch')
    elif any(manifest[k] for k in ('ordinal','captured_ordinal','replacements')) or manifest.get('file_sha256')!='':
        raise ValueError('zero-eligible deep manifest has captured event')


def read_event(path, manifest, *, expected_provenance, validate_only=False):
    """Reject partial/censored captures before allocating numeric scoring planes.

    The completed manifest is a sidecar, bound to the binary digest and final
    owner-thread minimum. A record alone is never evidence of a complete game.
    """
    path = Path(path)
    if isinstance(manifest, (str, Path)):
        if Path(manifest).stat().st_size > MAX_METADATA:
            raise ValueError('oversized deep manifest')
        manifest = json.loads(Path(manifest).read_text())
    validate_manifest(manifest)
    if not manifest['eligible']:raise ValueError('zero-eligible deep manifest has no event')
    if not HEADER.size+32 <= path.stat().st_size <= MAX_RECORD:
        raise ValueError('invalid deep record size')
    data = path.read_bytes()
    digest = hashlib.sha256(data[:-32]).digest()
    if digest != data[-32:] or hashlib.sha256(data).hexdigest() != manifest.get('file_sha256'):
        raise ValueError('deep digest mismatch')
    magic, version, metadata_bytes, cells, candidates = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1 or metadata_bytes > MAX_METADATA or cells > 512*512 or candidates > cells:
        raise ValueError('invalid deep header')
    if len(data) != HEADER.size+metadata_bytes+cells*4+candidates*CANDIDATE.size+32:
        raise ValueError('deep length mismatch')
    metadata = json.loads(data[HEADER.size:HEADER.size+metadata_bytes])
    provenance = metadata.get('provenance', {})
    if any(provenance.get(key) != value for key, value in expected_provenance.items()):
        raise ValueError('deep provenance mismatch')
    if any(type(manifest.get(key)) is not int or not 0<=manifest[key]<=0xffffffffffffffff for key in ('priority','ordinal')):
        raise ValueError('invalid deep reservoir priority')
    if metadata.get('priority') != manifest.get('priority') or metadata.get('ordinal') != manifest.get('ordinal'):
        raise ValueError('deep final reservoir mismatch')
    width,height=metadata.get('width'),metadata.get('height')
    if any(type(v) is not int or v<=0 or v & (v-1) for v in (width,height)) or width*height != cells:
        raise ValueError('deep shape mismatch')
    if type(metadata.get('origin')) is not int or not 0<=metadata['origin']<cells or type(metadata.get('nearest')) is not int or not 0<=metadata['nearest']<cells:
        raise ValueError('deep origin mismatch')
    offset = HEADER.size+metadata_bytes
    destinations = struct.unpack_from(f'<{cells}I', data, offset)
    offset += cells*4
    if any(value!=0xffffffff and value>=cells for value in destinations):
        raise ValueError('invalid deep destination')
    rows = [];sources=set()
    for source, component, flags, reserved, distance, access_distance in CANDIDATE.iter_unpack(data[offset:-32]):
        if flags & ~3 or reserved:
            raise ValueError('unknown deep candidate flags')
        if source>=cells or source in sources or component!=0xffffffff and component>=cells:
            raise ValueError('invalid deep candidate index')
        sources.add(source)
        rows.append(Candidate(source, component, bool(flags & 1), bool(flags & 2), distance, access_distance))
    if validate_only:
        return metadata
    event = FrozenEvent(metadata['width'], metadata['height'], metadata['origin'], metadata['nearest'],
                        destinations, rows, game_id=metadata['game_id'], event_id=metadata['event_id'],
                        provenance=provenance, optimum=metadata.get('optimum'))
    return event, metadata


def verify_pilot(event, metadata):
    """Compare real captured native decisions to both independent offline kernels."""
    from tools.resource_claim_patterns import FAMILIES, Pattern, encode
    from tools.resource_claim_offline import Scorer, UNREACHABLE
    profiles = (('unit',1,'every','forward',0),('resource',2,'every','reverse',1),
                ('midpoint',4,'random_block','shuffle',2),('interleaved',1,'every','forward',4),
                ('unit',4,'every','forward',6),('resource',1,'every','forward',7))
    arrays = []
    for family in FAMILIES:
        for radius in (1,2,4,8,16):
            for origin,stride,subset,order,direction in profiles:
                bearing=(direction+event.octant())%8 if family.startswith('cone_') or family=='rays_toward' else 0
                arrays.append(encode(Pattern(family,radius,origin,stride,subset,order,bearing)))
    native=metadata.get('patterns')
    if not isinstance(native,list) or len(native)!=2*len(arrays):
        raise ValueError('deep native pilot inventory mismatch')
    scorer=Scorer(event)
    batched=scorer.evaluate_many(arrays)
    checked=0
    for index,data in enumerate(arrays):
        scalar=scorer.evaluate(data)
        if scalar!=batched[index]:raise ValueError('offline kernel disagreement')
        for policy,outcome in enumerate(scalar):
            row=native[index*2+policy]
            distance=None if outcome.distance==UNREACHABLE else outcome.distance
            access_distance=None if outcome.access_distance==UNREACHABLE else outcome.access_distance
            expected=[index,policy,outcome.source,outcome.access,outcome.probe,distance,outcome.probes,outcome.fallback,access_distance]
            if not isinstance(row,list) or len(row)!=10 or row[:7]+row[8:]!=expected:
                raise ValueError(f'native/offline pilot mismatch pattern={index} policy={policy}')
            checked+=1
    return {'native_outcomes':checked,'offline_kernel_comparisons':2*checked,'exact':True,
            'scope':'diagnostic evaluator stock-index choice; not live ripest-first stock identity'}


if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--bundle-id',required=True)
    parser.add_argument('--source-revision',required=True)
    parser.add_argument('--cohort-id',required=True)
    args=parser.parse_args()
    event,metadata=read_event(args.directory/'event.bin',args.directory/'manifest.json',
        expected_provenance={'bundle_id':args.bundle_id,'source_revision':args.source_revision,'cohort_id':args.cohort_id})
    print(json.dumps(verify_pilot(event,metadata),indent=2))
