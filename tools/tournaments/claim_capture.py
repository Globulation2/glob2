"""Temporary, bounded reader for native claim min-hash reservoir captures."""
import hashlib
import json
from pathlib import Path
import struct

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


def read_capture(path, manifest, *, expected_provenance):
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
        rows.append((source, component, bool(flags & 1), bool(flags & 2), distance, access_distance))
    return metadata, destinations, rows


STRATIFIED_DESIGN = 'independent-minhash-per-disjoint-stratum-v2'
STRATA = tuple(
    f'{prefix}-{phase}'
    for prefix in ('early-food', 'early-fruit', 'early-other', 'late-fruit',
                   'late-other', 'late-food-pooled',
                   'late-food-protected-growing-proxy', 'late-food-other-or-unverified')
    for phase in ('movement', 'pickup'))


def validate_stratified_manifest(manifest):
    """Validate every slot, including empty/censored slots; never silently drop one.

    Returns V1-shaped slot attestations for the unchanged binary decoder. The
    envelope remains V2 and must not be passed off as a legacy one-per-game draw.
    """
    if (manifest.get('version') != 2 or manifest.get('design') != STRATIFIED_DESIGN
            or manifest.get('complete') is not True):
        raise ValueError('incomplete or unsupported stratified deep capture')
    limits = {'encoded_pending_limit': 80*1024**2, 'input_pending_limit': 128*1024**2,
              'final_limit': 160*1024**2, 'transient_limit': 24*1024**2}
    if any(type(manifest.get(k)) is not int or manifest[k] != v for k,v in limits.items()):
        raise ValueError('stratified declared allowance mismatch')
    for key, bound in (('peak_input_reservations', limits['input_pending_limit']),
                       ('peak_input_payload', limits['input_pending_limit']),
                       ('input_admission_exclusions', 2**64-1),
                       ('input_ineligible_exclusions', 2**64-1)):
        if type(manifest.get(key)) is not int or not 0 <= manifest[key] <= bound:
            raise ValueError('invalid stratified memory or admission accounting')
    if manifest['peak_input_payload'] > manifest['peak_input_reservations']:
        raise ValueError('stratified payload exceeds reservation')
    slots = manifest.get('strata')
    if not isinstance(slots, list) or len(slots) != len(STRATA):
        raise ValueError('invalid deep stratum inventory')
    result = []
    for index, (slot, name) in enumerate(zip(slots, STRATA)):
        if not isinstance(slot, dict) or type(slot.get('id')) is not int or slot['id'] != index or slot.get('name') != name:
            raise ValueError('deep stratum identity mismatch')
        reasons = slot.get('reasons')
        if (not isinstance(reasons, list) or len(reasons) != 3
                or any(type(n) is not int or not 0 <= n <= 0xffffffffffffffff for n in reasons)
                or sum(reasons) != slot.get('eligible')):
            raise ValueError('deep stratum reason counts mismatch')
        for key in ('unknown_pool', 'minimum_priority_ties', 'excluded_input_bytes'):
            n = slot.get(key)
            if type(n) is not int or not 0 <= n <= slot['eligible']:
                raise ValueError('invalid deep stratum coverage counter')
        if slot['excluded_input_bytes']:
            raise ValueError('input-budget censored deep stratum')
        legacy = dict(slot, version=1, design=DESIGN, complete=True,
                      context=manifest.get('context'))
        validate_manifest(legacy)
        if slot.get('file') != f'event-{index:02d}.bin':
            raise ValueError('invalid deep stratum filename')
        result.append(legacy)
    if manifest['input_admission_exclusions'] != sum(s['excluded_input_bytes'] for s in slots)+manifest['input_ineligible_exclusions']:
        raise ValueError('stratified input exclusion totals mismatch')
    if manifest.get('censored') is not False:
        raise ValueError('censored stratified capture')
    return result


def read_stratified_captures(directory, manifest, *, expected_provenance):
    """Yield bounded records; preserve slot denominators and absent strata.

    Consumers must retain the full manifest, not treat these policy-conditioned
    rows as independent games. A censored slot rejects this strict reader.
    """
    directory = Path(directory)
    if isinstance(manifest, (str, Path)):
        if Path(manifest).stat().st_size > MAX_METADATA:
            raise ValueError('oversized deep manifest')
        manifest = json.loads(Path(manifest).read_text())
    slots = validate_stratified_manifest(manifest)
    expected_files = {'manifest.json'} | {s['file'] for s in slots if s['eligible']}
    if any(p.is_symlink() or not p.is_file() for p in directory.iterdir()):
        raise ValueError('unexpected stratified deep entry type')
    if {p.name for p in directory.iterdir()} != expected_files:
        raise ValueError('stratified deep file inventory mismatch')
    if sum(p.stat().st_size for p in directory.iterdir()) > 16*MAX_RECORD+MAX_METADATA:
        raise ValueError('stratified deep directory allowance exceeded')
    for index, slot in enumerate(slots):
        if not slot['eligible']:
            yield index, slot, None
            continue
        record = read_capture(directory/slot['file'], slot, expected_provenance=expected_provenance)
        metadata = record[0]
        if metadata.get('deep_design') != STRATIFIED_DESIGN or metadata.get('deep_stratum') != index:
            raise ValueError('deep record stratum provenance mismatch')
        yield index, slot, record
