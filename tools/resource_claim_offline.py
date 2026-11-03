"""Temporary bounded full-grid claim scoring. No engine capture or policy selection.

Catalog arrays remain exact packed int16/int16/uint8 coordinates; only shortlisted
arrays need C++ headers. Event fixtures are frozen observations, never live maps.
"""
import argparse
from collections import OrderedDict
from dataclasses import asdict, dataclass, replace
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3

from tools.resource_claim_patterns import Pattern, PROBE, encode, specifications

NO_TILE = 0xffffffff
UNREACHABLE = 0xffffffffffffffff


def file_hash(path):
    with Path(path).open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def write_catalog(directory, specs, *, shard_bytes=64*1024**2, max_bytes=2*1024**3,
                  max_aliases=250000, reserve_bytes=8*1024**3):
    """Disk-backed exact dedup; one encoded array and one shard writer in memory."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=False)
    if min(shard_bytes, max_bytes, max_aliases) <= 0 or reserve_bytes < 0:
        raise ValueError('invalid catalog budget')
    db = sqlite3.connect(directory/'index.sqlite')
    db.execute('PRAGMA cache_size=-16384')
    db.execute('CREATE TABLE arrays(hash TEXT PRIMARY KEY, shard INTEGER, offset INTEGER, bytes INTEGER)')
    db.execute('CREATE TABLE aliases(id INTEGER PRIMARY KEY, spec TEXT UNIQUE, hash TEXT)')
    summary = {'version': 1, 'complete': False, 'aliases': 0, 'arrays': 0,
               'packed_bytes': 0, 'shard_bytes': shard_bytes, 'max_bytes': max_bytes,
               'generator_sha256': hashlib.sha256(Path(__file__).with_name('resource_claim_patterns.py').read_bytes()).hexdigest()}
    shard, offset, handle = -1, 0, None
    try:
        for spec in specs:
            if summary['aliases'] >= max_aliases:
                raise ValueError('catalog alias limit reached')
            data = encode(spec)
            if len(data) > shard_bytes:
                raise ValueError('single pattern exceeds shard budget')
            key = hashlib.sha256(data).hexdigest()
            existing = db.execute('SELECT shard,offset,bytes FROM arrays WHERE hash=?', (key,)).fetchone()
            # Reserve metadata space before committing a row, including SQLite indexes.
            projected = summary['packed_bytes'] + (0 if existing else len(data)) + (summary['aliases']+1)*1024
            if projected > max_bytes or shutil.disk_usage(directory).free < reserve_bytes+len(data)+4096:
                raise ValueError('catalog storage allowance or free-space reserve exhausted')
            if existing is None:
                if handle is None or offset+len(data) > shard_bytes:
                    if handle: handle.close()
                    shard += 1; offset = 0
                    handle = (directory/f'patterns-{shard:05d}.bin').open('xb')
                handle.write(data)
                db.execute('INSERT INTO arrays VALUES(?,?,?,?)', (key,shard,offset,len(data)))
                offset += len(data); summary['arrays'] += 1; summary['packed_bytes'] += len(data)
            else:
                if handle: handle.flush()
                with (directory/f'patterns-{existing[0]:05d}.bin').open('rb') as source:
                    source.seek(existing[1])
                    if source.read(existing[2]) != data:
                        raise ValueError('catalog content hash collision')
            db.execute('INSERT INTO aliases VALUES(?,?,?)', (summary['aliases'],json.dumps(asdict(spec),sort_keys=True),key))
            summary['aliases'] += 1
            if summary['aliases'] % 1024 == 0: db.commit()
        db.commit()
        if handle: handle.flush()
        summary['complete'] = True
    finally:
        if handle: handle.close()
        db.commit(); db.close()
        summary['files'] = {p.name: {'bytes': p.stat().st_size, 'sha256': file_hash(p)}
                            for p in sorted(directory.iterdir()) if p.is_file()}
        summary['identity'] = identity(summary)
        (directory/'catalog.json').write_text(json.dumps(summary,indent=2)+'\n')
    return summary


def catalog_arrays(directory):
    directory = Path(directory)
    summary = json.loads((directory/'catalog.json').read_text())
    if not summary.get('complete'):
        raise ValueError('incomplete catalog')
    expected = summary.pop('identity')
    if identity(summary) != expected:
        raise ValueError('catalog manifest identity mismatch')
    for name, record in summary['files'].items():
        path = directory/name
        if Path(name).name != name or path.stat().st_size != record['bytes'] or file_hash(path) != record['sha256']:
            raise ValueError('catalog file mismatch')
    db = sqlite3.connect(f'file:{directory.resolve() / "index.sqlite"}?mode=ro', uri=True)
    handle=None;current=-1
    try:
        for key, shard, offset, size in db.execute('SELECT * FROM arrays ORDER BY shard,offset'):
            if shard!=current:
                if handle:handle.close()
                handle=(directory/f'patterns-{shard:05d}.bin').open('rb');current=shard
            handle.seek(offset); data = handle.read(size)
            if len(data) != size or hashlib.sha256(data).hexdigest() != key:
                raise ValueError('catalog array mismatch')
            yield key, data
    finally:
        if handle:handle.close()
        db.close()


def logical_patterns(directory):
    """Alias views rotate relative to nearest octant, sharing physical arrays.

    Catalog must contain the full directional views for requested aliases.
    Iterate catalog_arrays first to validate file identities before scoring.
    """
    directory=Path(directory)
    # Full validation is streaming and bounded; do not retain the arrays.
    for _ in catalog_arrays(directory):pass
    db=sqlite3.connect(f'file:{directory.resolve() / "index.sqlite"}?mode=ro',uri=True)
    try:
        for alias,spec_json,key in db.execute('SELECT * FROM aliases ORDER BY id'):
            spec=Pattern(**json.loads(spec_json));views=[]
            directional=spec.radius and (spec.family.startswith('cone_') or spec.family=='rays_toward')
            for octant in range(8) if directional else range(1):
                physical=replace(spec,direction=(spec.direction+octant)%8) if directional else spec
                row=db.execute('SELECT a.shard,a.offset,a.bytes FROM aliases s JOIN arrays a ON a.hash=s.hash WHERE s.spec=?',
                    (json.dumps(asdict(physical),sort_keys=True),)).fetchone()
                if row is None:raise ValueError('catalog missing relative directional view')
                with (directory/f'patterns-{row[0]:05d}.bin').open('rb') as handle:
                    handle.seek(row[1]);views.append(handle.read(row[2]))
            yield identity(asdict(spec)),tuple(views) if directional else views[0]
    finally:db.close()


@dataclass(frozen=True)
class Candidate:
    source: int
    component: int = NO_TILE
    available: bool = True
    access_eligible: bool = True
    distance: int = UNREACHABLE
    access_distance: int = UNREACHABLE


@dataclass(frozen=True)
class Outcome:
    source: int = NO_TILE
    access: int = NO_TILE
    probe: int = NO_TILE
    distance: int = UNREACHABLE
    access_distance: int = UNREACHABLE
    probes: int = 0
    fallback: bool = True


class FrozenEvent:
    """Pure fixture contract; native binary capture is intentionally a later stage."""
    def __init__(self, width, height, unit, nearest, destinations, candidates, *, game_id,
                 event_id, provenance, optimum=None):
        import numpy as np
        if width <= 0 or height <= 0 or width & (width-1) or height & (height-1) or width*height > 512*512:
            raise ValueError('invalid or oversized frozen event')
        size = width*height
        if not 0 <= unit < size or nearest != NO_TILE and not 0 <= nearest < size:
            raise ValueError('invalid event origin')
        if len(destinations) != size or any(v != NO_TILE and not 0 <= v < size for v in destinations):
            raise ValueError('invalid destination plane')
        required = ('source_revision', 'bundle_id', 'cohort_id', 'metadata_mode', 'censored')
        if any(key not in provenance for key in required) or provenance['metadata_mode'] != 'published-xy':
            raise ValueError('frozen published-XY provenance required')
        if provenance['censored'] is not False:
            raise ValueError('censored events are calibration-only')
        if not game_id or not event_id: raise ValueError('stable game/event identity required')
        if optimum is not None and (type(optimum) is not int or not 0<=optimum<UNREACHABLE):
            raise ValueError('invalid oracle optimum')
        if any(type(value) is not int or not 0<=value<=UNREACHABLE for c in candidates for value in (c.distance,c.access_distance)):
            raise ValueError('invalid candidate distance')
        self.width,self.height,self.unit,self.nearest = width,height,unit,nearest
        self.game_id,self.event_id,self.provenance,self.optimum = game_id,event_id,json.loads(json.dumps(provenance)),optimum
        self.destinations = np.array(destinations,dtype=np.uint32,copy=True)
        self.destinations.setflags(write=False)
        self.candidates = {c.source:c for c in candidates}
        if len(self.candidates) != len(candidates) or any(not 0 <= c.source < size or c.component != NO_TILE and not 0 <= c.component < size for c in candidates):
            raise ValueError('invalid or duplicate candidate')
        components = {}
        for c in candidates:
            if c.available and c.component != NO_TILE:
                components[c.component] = min(c.source,components.get(c.component,c.source))
        choices = np.full(size,NO_TILE,dtype=np.uint32)
        for c in candidates:
            if c.access_eligible:
                choices[c.source] = components.get(c.component,NO_TILE) if c.component != NO_TILE else c.source if c.available else NO_TILE
        # Resolve canonical XY exactly once per probe tile, shared by every policy.
        self.sources = np.full(size,NO_TILE,dtype=np.uint32)
        valid = self.destinations != NO_TILE
        self.sources[valid] = choices[self.destinations[valid]]
        self.sources.setflags(write=False)
        self.origins = (unit,nearest,self.midpoint())

    @staticmethod
    def delta(a,b,size): return ((b-a+size//2)&(size-1))-size//2

    def midpoint(self):
        if self.nearest == NO_TILE:return NO_TILE
        dx=self.delta(self.unit%self.width,self.nearest%self.width,self.width)
        dy=self.delta(self.unit//self.width,self.nearest//self.width,self.height)
        # C++ integer division truncates toward zero, including negative odd deltas.
        return (((self.unit//self.width+int(dy/2))&(self.height-1))*self.width+
                ((self.unit%self.width+int(dx/2))&(self.width-1)))

    def octant(self):
        if self.nearest == NO_TILE:return 0
        dx=self.delta(self.unit%self.width,self.nearest%self.width,self.width)
        dy=self.delta(self.unit//self.width,self.nearest//self.width,self.height)
        if abs(dx)>=2*abs(dy):return 0 if dx>=0 else 4
        if abs(dy)>=2*abs(dx):return 2 if dy>=0 else 6
        return (1 if dy>=0 else 7) if dx>=0 else (3 if dy>=0 else 5)

    def geometry(self,access):
        dx=max(0,abs(self.delta(self.unit%self.width,access%self.width,self.width))-1)
        dy=max(0,abs(self.delta(self.unit//self.width,access//self.width,self.height))-1)
        return 10*max(dx,dy)+4*min(dx,dy)


class Scorer:
    """Vectorized tails plus bounded shared prefixes; both policies in one pass.

    Prefix hits and vector operations describe offline scorer work, not native
    policy runtime. Oracle reachability/cost never filters or ranks candidates.
    """
    def __init__(self,event,*,prefix_entries=256,cache_bytes=16*1024**2):
        import numpy as np
        if prefix_entries < 0 or cache_bytes < 0:raise ValueError('invalid scorer budget')
        self.event,self.prefix_entries,self.cache_bytes=event,prefix_entries,cache_bytes
        self.cache=OrderedDict();self.used=0;self.prefix_hits=0;self.vector_entries=0
        # A published destination and unit origin are immutable for this event.
        # Share their ranking across every pattern; oracle labels never enter it.
        accesses=event.destinations.astype(np.int64)
        w,h=event.width,event.height
        dx=np.maximum(0,np.abs(((accesses%w-event.unit%w+w//2)&(w-1))-w//2)-1)
        dy=np.maximum(0,np.abs(((accesses//w-event.unit//w+h//2)&(h-1))-h//2)-1)
        self.geometry=(10*np.maximum(dx,dy)+4*np.minimum(dx,dy)).astype(np.uint32)
        self.geometry.setflags(write=False)

    def segment(self,data):
        import numpy as np
        if len(data)%PROBE.size:raise ValueError('invalid packed pattern')
        fields=np.frombuffer(data,dtype=[('x','<i2'),('y','<i2'),('origin','u1')])
        if np.any(fields['origin']>2):raise ValueError('invalid pattern origin')
        origins=np.asarray(self.event.origins,dtype=np.int64)[fields['origin']]
        fields=fields[origins!=NO_TILE];origins=origins[origins!=NO_TILE]
        n=len(fields);self.vector_entries+=n
        if not n:return Outcome(),Outcome(),None
        w,h=self.event.width,self.event.height
        probes=(((origins//w+fields['y'])&(h-1))*w+((origins%w+fields['x'])&(w-1)))
        sources=self.event.sources[probes];hits=np.flatnonzero(sources!=NO_TILE)
        if not len(hits):return Outcome(probes=n),Outcome(probes=n),None
        scores=self.geometry[probes[hits]]
        best=int(np.argmin(scores));first=int(hits[0]);closest=int(hits[best])
        def outcome(index,count):
            source=int(sources[index]);access=int(self.event.destinations[probes[index]])
            return Outcome(source,access,int(probes[index]),self.event.candidates[source].distance,
                           self.event.candidates[access].access_distance,count,False)
        return outcome(first,first+1),outcome(closest,n),int(scores[best])

    def evaluate_many(self,arrays,*,max_entries=1024**2):
        """Segmented reductions amortize Python dispatch across short patterns."""
        import numpy as np
        lengths=np.asarray([len(data)//PROBE.size for data in arrays],dtype=np.int64)
        if any(len(data)%PROBE.size for data in arrays) or lengths.sum()>max_entries:
            raise ValueError('invalid or oversized pattern batch')
        n=len(arrays)
        if not n:return []
        fields=np.frombuffer(b''.join(arrays),dtype=[('x','<i2'),('y','<i2'),('origin','u1')])
        if np.any(fields['origin']>2):raise ValueError('invalid pattern origin')
        origins=np.asarray(self.event.origins,dtype=np.int64)[fields['origin']]
        groups=np.repeat(np.arange(n),lengths);valid=origins!=NO_TILE
        fields=fields[valid];origins=origins[valid];groups=groups[valid]
        counts=np.bincount(groups,minlength=n);starts=np.cumsum(counts)-counts
        self.vector_entries+=len(fields)
        w,h=self.event.width,self.event.height
        probes=(((origins//w+fields['y'])&(h-1))*w+((origins%w+fields['x'])&(w-1)))
        sources=self.event.sources[probes];hits=np.flatnonzero(sources!=NO_TILE)
        outcomes=[(Outcome(probes=int(count)),Outcome(probes=int(count))) for count in counts]
        if not len(hits):return outcomes
        hitgroups=groups[hits];unique,first=np.unique(hitgroups,return_index=True)
        scores=self.geometry[probes[hits]]
        minimum=np.minimum.reduceat(scores,first)
        besthits=hits[scores==np.repeat(minimum,np.diff(np.append(first,len(hits))))]
        bestgroups=groups[besthits];_,bestfirst=np.unique(bestgroups,return_index=True)
        def result(index,probes_count):
            source=int(sources[index]);access=int(self.event.destinations[probes[index]])
            return Outcome(source,access,int(probes[index]),self.event.candidates[source].distance,
                self.event.candidates[access].access_distance,int(probes_count),False)
        for i,group in enumerate(unique):
            at=int(hits[first[i]]);best=int(besthits[bestfirst[i]])
            outcomes[int(group)]=(result(at,at-starts[group]+1),result(best,counts[group]))
        return outcomes

    def evaluate(self,data):
        from dataclasses import replace
        cut=min(len(data),self.prefix_entries*PROBE.size);key=data[:cut]
        prefix=self.cache.pop(key,None)
        if prefix is None:
            prefix=self.segment(key);size=len(key)+256
            if size<=self.cache_bytes:
                while self.cache and self.used+size>self.cache_bytes:
                    old,_=self.cache.popitem(last=False);self.used-=len(old)+256
                self.cache[key]=prefix;self.used+=size
        else:self.cache[key]=prefix;self.prefix_hits+=1
        if cut==len(data):return prefix[:2]
        tail=self.segment(data[cut:]);count=prefix[1].probes
        first=prefix[0] if not prefix[0].fallback else replace(tail[0],probes=count+tail[0].probes)
        best=prefix[1] if tail[1].fallback or not prefix[1].fallback and prefix[2]<=tail[2] else tail[1]
        return first,replace(best,probes=count+tail[1].probes)


def freeze_split(games, *, source_identity, cohort_identity, salt='claims-discovery-v1',holdout_percent=20,strata=None):
    if not 0<holdout_percent<100 or len(set(games))!=len(games):raise ValueError('invalid game split')
    assignments={g:'holdout' if int(identity([salt,g])[:16],16)%100<holdout_percent else 'discovery' for g in sorted(games)}
    if strata is not None:
        if set(strata)!=set(games):raise ValueError('split strata must cover every game')
        for group in sorted(set(strata.values())):
            members=sorted((g for g in games if strata[g]==group),key=lambda g:identity([salt,g]))
            count=min(len(members)-1,(len(members)*holdout_percent+99)//100)
            for index,g in enumerate(members):assignments[g]='holdout' if index<count else 'discovery'
    manifest={'version':1,'source_identity':source_identity,'cohort_identity':cohort_identity,
              'salt':salt,'holdout_percent':holdout_percent,'games':assignments,'strata':strata,'bootstrap_unit':'game',
              'policy_selection_allowed':False}
    return manifest|{'identity':identity(manifest)}


def score_game(events, arrays, *, max_patterns=250000, max_event_bytes=128*1024**2):
    """Bounded game/event and array batches; never emit per-event full-grid rows."""
    import itertools
    events=list(itertools.islice(events,8193))
    if not events or len(events)>8192 or len({e.game_id for e in events})!=1 or len({e.event_id for e in events})!=len(events):
        raise ValueError('bounded unique events from one game required')
    if len({identity(e.provenance) for e in events})!=1:raise ValueError('mixed event provenance')
    if sum(2*e.destinations.nbytes+e.sources.nbytes+len(e.candidates)*256 for e in events)>max_event_bytes:
        raise ValueError('event batch exceeds memory budget; stream smaller game batches')
    scorers=[Scorer(e,cache_bytes=0) for e in events]
    def score(batch):
        totals=[[dict(events=0,fallback=0,unreachable=0,access_unreachable=0,oracle_missing=0,compared=0,regret=0,ratio_sum=0.0,ratio_count=0,zero_optimum_positive_cost=0,access_compared=0,access_regret=0,access_ratio_sum=0.0,access_ratio_count=0,zero_optimum_positive_access_cost=0,probes=0) for _ in range(2)] for _ in batch]
        for event,scorer in zip(events,scorers):
            views=[data[event.octant()] if isinstance(data,tuple) else data for _,data in batch]
            for sums,outcomes in zip(totals,scorer.evaluate_many(views)):
                for total,outcome in zip(sums,outcomes):
                    total['events']+=1;total['fallback']+=outcome.fallback;total['probes']+=outcome.probes
                    total['unreachable']+=not outcome.fallback and outcome.distance==UNREACHABLE
                    total['access_unreachable']+=not outcome.fallback and outcome.access_distance==UNREACHABLE
                    total['oracle_missing']+=event.optimum is None
                    if not outcome.fallback and outcome.distance!=UNREACHABLE:
                        if event.optimum is None or outcome.distance<event.optimum:raise ValueError('policy label contradicts exact optimum')
                        total['compared']+=1;total['regret']+=outcome.distance-event.optimum
                        if event.optimum>0 or outcome.distance==0:
                            total['ratio_count']+=1;total['ratio_sum']+=outcome.distance/event.optimum if event.optimum else 1
                        else:total['zero_optimum_positive_cost']+=1
                    if not outcome.fallback and outcome.access_distance!=UNREACHABLE:
                        if event.optimum is None or outcome.access_distance<event.optimum:raise ValueError('invalid access optimum')
                        total['access_compared']+=1;total['access_regret']+=outcome.access_distance-event.optimum
                        if event.optimum>0 or outcome.access_distance==0:
                            total['access_ratio_count']+=1;total['access_ratio_sum']+=outcome.access_distance/event.optimum if event.optimum else 1
                        else:total['zero_optimum_positive_access_cost']+=1
        for (key,_),sums in zip(batch,totals):
            yield {'game_id':events[0].game_id,'array':key,'policies':sums}
    batch=[];entries=packed=0
    for ordinal,(key,data) in enumerate(arrays):
        if ordinal>=max_patterns:raise ValueError('pattern limit reached')
        views=data if isinstance(data,tuple) else (data,)
        maximum=max(len(v) for v in views)//PROBE.size;size=sum(map(len,views))
        if maximum>1024**2 or size>64*1024**2:raise ValueError('single pattern exceeds batch budget')
        if batch and (entries+maximum>1024**2 or packed+size>64*1024**2 or len(batch)>=1024):
            yield from score(batch);batch=[];entries=packed=0
        batch.append((key,data));entries+=maximum;packed+=size
    if batch:yield from score(batch)


METRIC_FIELDS=('events','fallback','unreachable','access_unreachable','oracle_missing','compared',
               'regret','ratio_sum','ratio_count','zero_optimum_positive_cost','access_compared','access_regret','access_ratio_sum','access_ratio_count','zero_optimum_positive_access_cost','probes')


def alias_views(directory):
    """Small logical-alias to physical-array index matrix for all eight bearings."""
    import numpy as np
    directory=Path(directory);summary=json.loads((directory/'catalog.json').read_text())
    if not summary.get('complete') or summary['aliases']>250000:raise ValueError('invalid alias catalog')
    db=sqlite3.connect(f'file:{directory.resolve() / "index.sqlite"}?mode=ro',uri=True)
    groups={};physical=[];alias_group=np.empty(summary['aliases'],dtype=np.uint32)
    directions=np.empty(summary['aliases'],dtype=np.uint8)
    try:
        for ordinal,encoded,index in db.execute('SELECT s.id,s.spec,a.rowid-1 FROM aliases s JOIN arrays a ON a.hash=s.hash ORDER BY s.id'):
            spec=json.loads(encoded);direction=spec.pop('direction')
            directional=spec['radius'] and (spec['family'].startswith('cone_') or spec['family']=='rays_toward')
            key=tuple(sorted(spec.items()));group=groups.get(key)
            if group is None:group=len(physical);groups[key]=group;physical.append([NO_TILE]*8)
            if directional:physical[group][direction]=index
            else:physical[group]=[index]*8
            alias_group[ordinal]=group;directions[ordinal]=direction if directional else 0
    finally:db.close()
    table=np.asarray(physical,dtype=np.uint32)
    if np.any(table==NO_TILE):raise ValueError('catalog lacks complete directional views')
    return table[alias_group[:,None],(directions[:,None]+np.arange(8))%8]


def score_logical_game(events,directory,views=None):
    """Score unique arrays once per bearing group; accumulate compact alias rows.

    No per-event full-grid rows are retained. Event batches are bounded by
    score_game; larger games must be streamed in explicitly bounded batches and
    merged before they enter game-level statistics.
    """
    import numpy as np
    import itertools
    events=list(itertools.islice(events,8193))
    if not events or len(events)>8192 or len({e.game_id for e in events})!=1:raise ValueError('one bounded game required')
    if sum(2*e.destinations.nbytes+e.sources.nbytes+len(e.candidates)*256 for e in events)>128*1024**2:
        raise ValueError('logical game event batch exceeds128MiB')
    if len({e.event_id for e in events})!=len(events):raise ValueError('duplicate event identity')
    if len({identity(e.provenance) for e in events})!=1:raise ValueError('mixed event provenance')
    summary=json.loads((Path(directory)/'catalog.json').read_text())
    if views is None:views=alias_views(directory)
    if views.shape!=(summary['aliases'],8) or np.any(views>=summary['arrays']):raise ValueError('invalid alias projection')
    dtype=np.dtype([(name,'<f8' if name in ('ratio_sum','access_ratio_sum') else '<u8') for name in METRIC_FIELDS])
    totals=np.zeros((summary['aliases'],2),dtype=dtype)
    physical=np.zeros((summary['arrays'],2),dtype=dtype)
    for octant in sorted({e.octant() for e in events}):
        group=[e for e in events if e.octant()==octant];physical.fill(0);count=0
        for index,row in enumerate(score_game(group,catalog_arrays(directory))):
            for policy,values in enumerate(row['policies']):
                physical[index,policy]=tuple(values[name] for name in METRIC_FIELDS)
            count+=1
        if count!=summary['arrays']:raise ValueError('incomplete physical catalog scores')
        for start in range(0,len(views),4096):
            end=start+4096;selected=views[start:end,octant]
            for name in METRIC_FIELDS:
                addition=physical[name][selected]
                if name not in ('ratio_sum','access_ratio_sum') and np.any(np.iinfo(np.uint64).max-totals[name][start:end]<addition):
                    raise ValueError('aggregate integer overflow')
                totals[name][start:end]+=addition
    return totals


def deep_budget(*, catalog_bytes, events=1000, width=512, height=512,
                freeze_bytes=9*1024**3, aggregate_bytes=256*1024**2,
                total_bytes=16*1024**3, free_bytes, reserve_bytes=8*1024**3):
    values=(catalog_bytes,events,width,height,freeze_bytes,aggregate_bytes,total_bytes,free_bytes,reserve_bytes)
    if any(type(v) is not int or v<0 for v in values) or not events or not width or not height:
        raise ValueError('invalid deep-capture budget')
    # Two uint16 XY planes plus a worst-case32-byte candidate row per tile;
    # reserve64KiB/event for immutable metadata and framing. Compression is not assumed.
    event_bytes=36*width*height+65536
    if events*event_bytes>freeze_bytes:raise ValueError('deep event allowance exceeds frozen-input budget')
    required=catalog_bytes+freeze_bytes+aggregate_bytes
    if required>total_bytes or required+reserve_bytes>free_bytes:
        raise ValueError('catalog/freeze/aggregate total allowance or reserve exceeded')
    return {'catalog_bytes':catalog_bytes,'max_events':events,'max_event_bytes':event_bytes,
        'freeze_bytes':freeze_bytes,'aggregate_bytes':aggregate_bytes,'reserved_bytes':required,
        'total_bytes':total_bytes,'free_space_reserve_bytes':reserve_bytes,
        'policy':'No silent caps: capture exclusions are censored; storage failures are failures.'}


class GameAggregates:
    """Stream compact game matrices into global means; no fake bootstrap CI.

    Whole-game splits are frozen before capture. A shortlist must be re-scored
    on retained held-out games for game-clustered intervals and feature analysis.
    """
    METRICS=(('access_regret','access_compared'),('access_ratio_sum','access_ratio_count'),('regret','compared'),('ratio_sum','ratio_count'),
             ('fallback','events'),('unreachable','events'),('access_unreachable','events'),
             ('oracle_missing','events'),('probes','events'))
    def __init__(self,aliases,split,*,catalog_identity,provenance_identity,max_bytes=128*1024**2):
        import numpy as np
        if type(aliases) is not int or aliases<1 or aliases>250000:raise ValueError('invalid aggregate shape')
        if identity({k:v for k,v in split.items() if k!='identity'})!=split.get('identity') or any(v not in ('discovery','holdout') for v in split['games'].values()):
            raise ValueError('invalid frozen game split')
        required=2*aliases*2*len(self.METRICS)*12
        if required>max_bytes:raise ValueError('global aggregate memory budget exceeded')
        self.sums=np.zeros((2,aliases,2,len(self.METRICS)),dtype='<f8')
        self.counts=np.zeros(self.sums.shape,dtype='<u4')
        self.split=split;self.catalog_identity=catalog_identity;self.provenance_identity=provenance_identity
        self.games=set()

    def add(self,game_id,values,*,provenance_identity):
        import numpy as np
        if game_id in self.games or game_id not in self.split['games']:raise ValueError('duplicate or unknown game')
        if provenance_identity!=self.provenance_identity:raise ValueError('mixed aggregate provenance')
        if values.shape!=self.sums.shape[1:3] or tuple(values.dtype.names or ())!=METRIC_FIELDS:
            raise ValueError('invalid compact game matrix')
        if any(values.dtype[name]!=np.dtype('<f8' if name in ('ratio_sum','access_ratio_sum') else '<u8') for name in METRIC_FIELDS) or any(np.any(~np.isfinite(values[field])) for field in ('ratio_sum','access_ratio_sum')):
            raise ValueError('invalid compact metric types or nonfinite ratio')
        cohort=0 if self.split['games'][game_id]=='discovery' else 1
        for metric,(numerator,denominator) in enumerate(self.METRICS):
            valid=values[denominator]>0
            ratio=np.divide(values[numerator],values[denominator],out=np.zeros(values.shape),where=valid)
            self.sums[cohort,:,:,metric]+=ratio
            self.counts[cohort,:,:,metric]+=valid
        self.games.add(game_id)

    def write(self,directory,*,max_bytes=256*1024**2,reserve_bytes=8*1024**3):
        import numpy as np
        directory=Path(directory);directory.mkdir(parents=True,exist_ok=False)
        # Reserve the uncompressed bound before writing compressed arrays.
        required=self.sums.nbytes+self.counts.nbytes+65536
        if required>max_bytes or shutil.disk_usage(directory).free<required+reserve_bytes:
            raise ValueError('global aggregate output allowance/reserve exceeded')
        partial=directory/'aggregates.npz.partial'
        with partial.open('xb') as handle:np.savez_compressed(handle,sums=self.sums,games=self.counts)
        partial.rename(directory/'aggregates.npz')
        report={'version':1,'complete':True,'catalog_identity':self.catalog_identity,
            'provenance_identity':self.provenance_identity,'split_identity':self.split['identity'],
            'observed_games':sorted(self.games),'missing_games':sorted(set(self.split['games'])-self.games),
            'cohorts':['discovery','holdout'],'metrics':self.METRICS,'shape':self.sums.shape,
            'means':'sums/games where games>0; absent cells are unavailable, not zero',
            'confidence_intervals':None,'next_step':'Re-score a frozen shortlist per held-out game and features; bootstrap games.',
            'primary_travel_metric':'access_regret/access_compared; source regret is component-best lower bound',
            'pool_source_identity':'Smallest available stock index in diagnostic evaluator; live picker is ripest-first, so this is not exact physical stock-choice parity.',
            'policy_selection_allowed':False,'arrays_sha256':file_hash(directory/'aggregates.npz')}
        (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n')
        return report


def write_scores(path, rows, *, max_raw_bytes=256*1024**2, reserve_bytes=8*1024**3):
    """Compressed streaming output with a conservative uncompressed allowance.

    Failure leaves an explicitly partial file, never a complete-looking result.
    """
    path=Path(path);partial=path.with_name(path.name+'.partial')
    if path.exists() or partial.exists():raise ValueError('score output already exists')
    if max_raw_bytes<=0 or reserve_bytes<0:raise ValueError('invalid score allowance')
    count=total=0
    with partial.open('xb') as raw:
        with gzip.GzipFile(fileobj=raw,mode='wb',mtime=0) as compressed:
            for row in rows:
                data=(json.dumps(row,sort_keys=True,separators=(',',':'))+'\n').encode()
                if total+len(data)>max_raw_bytes or shutil.disk_usage(path.parent).free<reserve_bytes+len(data):
                    raise ValueError('score output budget/reserve exhausted; partial output retained')
                compressed.write(data);total+=len(data);count+=1
    partial.rename(path)
    return {'rows':count,'raw_bytes':total,'compressed_bytes':path.stat().st_size,'sha256':file_hash(path)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--max-bytes',type=int,default=2*1024**3)
    parser.add_argument('--max-aliases',type=int,default=250000)
    args=parser.parse_args()
    print(json.dumps(write_catalog(args.directory,specifications(),max_bytes=args.max_bytes,max_aliases=args.max_aliases),indent=2))


if __name__=='__main__':main()
