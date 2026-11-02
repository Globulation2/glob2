"""Temporary offline scorer wrapper for the portable bounded capture reader."""
import json
from pathlib import Path
from tools.tournaments.claim_capture import (MAGIC, HEADER, CANDIDATE, MAX_RECORD,
    MAX_METADATA, DESIGN, validate_manifest, read_capture)


def read_event(path, manifest, *, expected_provenance, validate_only=False):
    metadata,destinations,rows=read_capture(path,manifest,expected_provenance=expected_provenance)
    if validate_only:return metadata
    from tools.resource_claim_offline import Candidate,FrozenEvent
    event=FrozenEvent(metadata['width'],metadata['height'],metadata['origin'],metadata['nearest'],
        destinations,[Candidate(*row) for row in rows],game_id=metadata['game_id'],event_id=metadata['event_id'],
        provenance=metadata['provenance'],optimum=metadata.get('optimum'))
    return event,metadata


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
