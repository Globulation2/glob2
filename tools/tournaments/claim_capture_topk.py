"""Strict V3 bottom-four reader. Existing V1/V2 decoder and envelopes stay unchanged."""
import json
from pathlib import Path
from .claim_capture import DESIGN, STRATA, MAX_RECORD, MAX_METADATA, read_capture, validate_manifest

TOP_K = 4
TOPK_DESIGN = 'independent-bottom-four-per-disjoint-stratum-v3'
U64 = 2**64-1


def _count(row, name, maximum=U64):
    value=row.get(name)
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError('invalid top-k counter: '+name)
    return value


def validate_topk_manifest(manifest):
    if (type(manifest.get('version')) is not int or manifest.get('version') != 3 or
        type(manifest.get('top_k')) is not int or manifest.get('top_k') != TOP_K or
        manifest.get('design') != TOPK_DESIGN or manifest.get('complete') is not True or
        manifest.get('censored') is not False):
        raise ValueError('incomplete, censored or unsupported top-k capture')
    limits={'encoded_pending_limit':80*1024**2,'input_pending_limit':128*1024**2,
            'final_limit':640*1024**2,'transient_limit':24*1024**2}
    for key,value in limits.items():
        if _count(manifest,key)!=value:raise ValueError('top-k allowance mismatch')
    peak=_count(manifest,'peak_input_reservations',limits['input_pending_limit'])
    if _count(manifest,'peak_input_payload',peak)>peak:raise ValueError('top-k input payload exceeds reservation')
    _count(manifest,'input_admission_exclusions');_count(manifest,'input_ineligible_exclusions')
    strata=manifest.get('strata')
    if not isinstance(strata,list) or len(strata)!=16:raise ValueError('invalid top-k strata inventory')
    selected=[];ordinals=set()
    for i,(slot,name) in enumerate(zip(strata,STRATA)):
        if slot.get('id')!=i or type(slot.get('id')) is not int or slot.get('name')!=name or slot.get('censored') is not False:
            raise ValueError('invalid top-k stratum identity/censoring')
        n=_count(slot,'eligible');k=min(TOP_K,n);last=_count(slot,'last_ordinal')
        if _count(slot,'retained',TOP_K)!=k or bool(last)!=bool(n) or n>last:raise ValueError('top-k cardinality mismatch')
        reasons=slot.get('reasons')
        if not isinstance(reasons,list) or len(reasons)!=3 or any(type(x)is not int or not 0<=x<=n for x in reasons) or sum(reasons)!=n:
            raise ValueError('top-k reason denominator mismatch')
        _count(slot,'unknown_pool',n);_count(slot,'current_priority_ties',4*n)
        for key in ('excluded_eligible_admission','excluded_eligible_sample_cap','excluded_eligible_queue_cap','excluded_input_bytes'):
            if _count(slot,key):raise ValueError('top-k censored admission cannot be reweighted')
        if n:
            _count(slot,'weight_numerator');_count(slot,'weight_denominator',TOP_K)
        expected_weight=(n,k) if n else (None,None)
        if (slot.get('weight_numerator'),slot.get('weight_denominator'))!=expected_weight:
            raise ValueError('top-k weight differs from designed Ns/min(4,Ns)')
        records=slot.get('records')
        if not isinstance(records,list) or len(records)!=k or {r.get('slot') for r in records}!=set(range(k)):
            raise ValueError('missing selected top-k record; never reweight partial writes')
        entries=[]
        for record in records:
            physical=_count(record,'slot',TOP_K-1);ordinal=_count(record,'ordinal',last)
            if not ordinal or ordinal in ordinals or record.get('captured_ordinal')!=ordinal:
                raise ValueError('duplicate/missing selected top-k event')
            ordinals.add(ordinal)
            if record.get('file')!=f'event-{i:02d}-{physical}.bin':raise ValueError('invalid top-k filename')
            _count(record,'priority');_count(record,'replacements',n)
            legacy=dict(record,version=1,design=DESIGN,complete=True,censored=False,eligible=n,
                        excluded_eligible_admission=0,excluded_eligible_sample_cap=0,excluded_eligible_queue_cap=0)
            validate_manifest(legacy);entries.append(legacy)
        selected.append(entries)
    if manifest['input_admission_exclusions']!=manifest['input_ineligible_exclusions']+sum(s['excluded_input_bytes'] for s in strata):
        raise ValueError('top-k input exclusion totals mismatch')
    return selected


def read_topk_captures(directory,manifest,*,expected_provenance):
    """Yield (stratum, stratum_metadata, selected_record_metadata, capture).

    Empty strata yield one pair of None values. Weight numerator/denominator are
    on the stratum and usable only after the entire strict iterator validates.
    Priority ties use a stable ordinal tie-break and are reported explicitly;
    inclusion weights use the nominal independent uniform-priority model.
    """
    directory=Path(directory)
    if isinstance(manifest,(str,Path)):
        if Path(manifest).stat().st_size>MAX_METADATA:raise ValueError('oversized top-k manifest')
        manifest=json.loads(Path(manifest).read_text())
    entries=validate_topk_manifest(manifest)
    files={'manifest.json'}|{e['file'] for group in entries for e in group}
    paths=list(directory.iterdir())
    if any(p.is_symlink() or not p.is_file() for p in paths) or {p.name for p in paths}!=files:
        raise ValueError('top-k exact file inventory mismatch')
    if sum(p.stat().st_size for p in paths)>64*MAX_RECORD+MAX_METADATA:raise ValueError('top-k directory allowance')
    for i,group in enumerate(entries):
        slot=manifest['strata'][i]
        if not group:yield i,slot,None,None
        for entry in group:
            record=read_capture(directory/entry['file'],entry,expected_provenance=expected_provenance)
            meta=record[0]
            if meta.get('deep_design')!=TOPK_DESIGN or meta.get('deep_stratum')!=i or meta.get('deep_slot')!=entry['slot']:
                raise ValueError('top-k wire stratum/slot provenance mismatch')
            yield i,slot,entry,record
