"""Strict independent missing-destination V2 bottom-k stream; V1 stays unchanged."""
import hashlib,json,struct
from pathlib import Path
from .claim_capture_missing import count,HEADER,CANDIDATE,MAX_RECORD,MAX_METADATA,NO_TILE
DESIGN='independent-bottom-k-missing-destination-v2'
MAX_MANIFEST=1024**2
U64=2**64-1

def validate_missing_topk_manifest(m):
 k=count(m,'top_k',64)
 if not k or type(m.get('version'))is not int or m['version']!=2 or m.get('design')!=DESIGN or m.get('complete')is not True or m.get('censored')is not False:raise ValueError('incomplete/censored missing top-k stream')
 limits={'input_pending_limit':64*1024**2,'pending_limit':4,'encoded_pending_limit':40*1024**2,'record_limit':MAX_RECORD,'final_limit':8*k*MAX_RECORD,'transient_limit':24*1024**2,'free_reserve':8*1024**3,'max_admissions':32768}
 for key,value in limits.items():
  if count(m,key)!=value:raise ValueError('missing top-k allowance')
 peak=count(m,'peak_input_reservations',limits['input_pending_limit']);count(m,'peak_input_payload',peak)
 calls=count(m,'calls');sampled=count(m,'sampled',calls);admitted=count(m,'admitted',32768)
 if count(m,'completed')!=admitted or not count(m,'sample_every',2**32-1):raise ValueError('missing top-k completion/sampling')
 obsolete=count(m,'obsolete_completed',admitted);count(m,'oracle_elapsed_ns');strata=m.get('strata')
 if not isinstance(strata,list)or len(strata)!=8:raise ValueError('missing eight strata')
 eligible=0;written=0;ordinals=set();groups=[]
 for i,s in enumerate(strata):
  if count(s,'id')!=i or s.get('censored')is not False:raise ValueError('missing stratum identity/censor')
  n=count(s,'eligible',sampled);eligible+=n;last=count(s,'last_ordinal',calls);retained=min(k,n)
  if count(s,'retained',k)!=retained or bool(last)!=bool(n)or n>last:raise ValueError('missing top-k cardinality')
  reasons=s.get('reasons')
  if not isinstance(reasons,list)or len(reasons)!=3 or any(type(v)is not int or not 0<=v<=n for v in reasons)or sum(reasons)!=n:raise ValueError('missing reason denominator')
  if (i<4 and(reasons[1]or reasons[2]))or(i>=4 and reasons[0]):raise ValueError('missing category mismatch')
  for key in ('excluded_sample_cap','excluded_queue_cap','excluded_input_bytes'):
   if count(s,key):raise ValueError('missing censored admission cannot be reweighted')
  count(s,'ties',min(U64,k*n))
  if n:count(s,'weight_numerator');count(s,'weight_denominator',k)
  if (s.get('weight_numerator'),s.get('weight_denominator'))!=((n,retained)if n else(None,None)):raise ValueError('missing Ns/min(k,Ns) weight')
  records=s.get('records')
  if not isinstance(records,list)or len(records)!=retained or any(not isinstance(r,dict) for r in records)or {count(r,'slot',k-1)for r in records}!=set(range(retained)):raise ValueError('missing exact selected set')
  for r in records:
   ordinal=count(r,'ordinal',last)
   if not ordinal or ordinal in ordinals or count(r,'captured_ordinal',last)!=ordinal:raise ValueError('missing duplicate/unwritten selected event')
   ordinals.add(ordinal);count(r,'priority');replacement=count(r,'replacements',n)
   if not replacement:raise ValueError('missing write count')
   written+=replacement
   if r.get('file')!=f"event-{i:02d}-{r['slot']}.bin":raise ValueError('missing canonical filename')
   digest=r.get('file_sha256')
   if not isinstance(digest,str)or len(digest)!=64 or any(c not in '0123456789abcdef'for c in digest):raise ValueError('missing record SHA256')
  groups.append(records)
 if written+obsolete!=admitted:raise ValueError('missing completion/write conservation')
 if eligible+count(m,'excluded_mixed',sampled)+count(m,'valid_destination',sampled)!=sampled:raise ValueError('missing classification conservation')
 return groups

def _read_record(directory,manifest,i,s,expected_provenance):
 p=directory/s['file']
 if not HEADER.size+32<=p.stat().st_size<=MAX_RECORD:raise ValueError('missing record length')
 data=p.read_bytes()
 if hashlib.sha256(data).hexdigest()!=s['file_sha256'] or hashlib.sha256(data[:-32]).digest()!=data[-32:]:raise ValueError('missing digest')
 magic,version,meta_bytes,cells,label_count,candidates=HEADER.unpack_from(data)
 if magic!=b'G2CLMG01' or version!=1 or meta_bytes>MAX_METADATA or not 0<cells<=512*512 or label_count not in (0,cells) or candidates>cells:raise ValueError('missing header')
 if len(data)!=HEADER.size+meta_bytes+label_count*4+candidates*CANDIDATE.size+32:raise ValueError('missing length')
 m=json.loads(data[HEADER.size:HEADER.size+meta_bytes]);w,h=m.get('width'),m.get('height')
 if any(type(v)is not int or v<=0 or v&(v-1) for v in (w,h)) or w*h!=cells:raise ValueError('missing dimensions')
 for key,bound in [('owner',2**64-1),('pre_task',2**64-1),('tick',2**32-1),('ordinal',2**64-1),('material',11)]:count(m,key,bound)
 if 'nearest' not in m:raise ValueError('missing explicit nearest')
 if type(m.get('origin'))is not int or not 0<=m['origin']<cells or m.get('nearest')is not None:raise ValueError('missing anchor contract')
 count(m,'priority');count(m,'physical_slot',manifest['top_k']-1);count(m,'top_k',64);count(m,'stratum',7)
 if m.get('design')!=DESIGN or m.get('stratum')!=i or m.get('ordinal')!=s['ordinal'] or m.get('priority')!=s['priority'] or m.get('physical_slot')!=s['slot'] or m.get('top_k')!=manifest['top_k']:raise ValueError('missing minimum identity')
 if any(m.get('provenance',{}).get(k)!=v for k,v in expected_provenance.items()) or m.get('game_id')!=manifest.get('context',{}).get('game_id'):raise ValueError('missing provenance')
 if type(m.get('field_present'))is not bool or type(m.get('label_present'))is not bool or m['label_present']!=bool(label_count):raise ValueError('missing field flags')
 reason=m.get('reason');flags={'invalid_xy':(True,True),'missing_scalar':(False,False),'missing_xy_plane':(True,False)}
 if flags.get(reason)!=(m['field_present'],m['label_present']) or (i<4)!=(reason=='invalid_xy'):raise ValueError('missing reason flags')
 if m.get('phase') not in ('movement','pickup') or (i%2==1)!=(m['phase']=='pickup') or ((i%4)<2)!=(m.get('material')==1):raise ValueError('missing phase/material stratum')
 if 'optimum'not in m or (m['optimum']is not None and (type(m['optimum'])is not int or not 0<=m['optimum']<2**64-1)):raise ValueError('missing oracle label')
 offset=HEADER.size+meta_bytes;labels=struct.unpack_from(f'<{label_count}I',data,offset) if label_count else None;offset+=label_count*4
 if labels is not None and (labels[m['origin']]!=NO_TILE or any(v!=NO_TILE and v>=cells for v in labels)):raise ValueError('missing invalid-origin labels')
 rows=[];seen=set()
 for source,component,flags,reserved,distance,access_distance in CANDIDATE.iter_unpack(data[offset:-32]):
  if flags&~3 or reserved or source>=cells or source in seen or (component!=NO_TILE and component>=cells):raise ValueError('missing candidate')
  seen.add(source);rows.append((source,component,bool(flags&1),bool(flags&2),distance,access_distance))
 finite=[r[4] for r in rows if r[2] and r[4]!=2**64-1]
 if m['optimum']!=(min(finite) if finite else None):raise ValueError('missing optimum/candidate mismatch')
 return m,labels,rows

def read_missing_topk_captures(directory,manifest,*,expected_provenance):
 """Consume the complete iterator before using any Ns/min(K,Ns) weights."""
 directory=Path(directory)
 if isinstance(manifest,(str,Path)):
  if Path(manifest).stat().st_size>MAX_MANIFEST:raise ValueError('missing top-k manifest size')
  manifest=json.loads(Path(manifest).read_text())
 if (directory/'manifest.json').stat().st_size>MAX_MANIFEST:raise ValueError('missing top-k manifest size')
 context=manifest.get('context')
 if not isinstance(context,dict)or not isinstance(context.get('game_id'),str)or not context['game_id']or any(not isinstance(v,str)or not v for v in context.values())or any(context.get(key)!=value for key,value in expected_provenance.items()):raise ValueError('missing top-k manifest provenance')
 groups=validate_missing_topk_manifest(manifest);paths=list(directory.iterdir());expected={'manifest.json'}|{r['file']for group in groups for r in group}
 if any(p.is_symlink()or not p.is_file()for p in paths)or {p.name for p in paths}!=expected:raise ValueError('missing top-k exact inventory')
 if sum(p.stat().st_size for p in paths)>8*manifest['top_k']*MAX_RECORD+MAX_MANIFEST:raise ValueError('missing top-k directory allowance')
 for i,group in enumerate(groups):
  s=manifest['strata'][i]
  if not group:yield i,s,None,None
  for r in group:yield i,s,r,_read_record(directory,manifest,i,r,expected_provenance)
