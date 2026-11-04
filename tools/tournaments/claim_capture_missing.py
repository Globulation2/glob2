"""Strict additive missing-destination stream; never invent a nearest anchor/XY plane."""
import hashlib,json,struct
from pathlib import Path
DESIGN='independent-minhash-missing-destination-v1'
HEADER=struct.Struct('<8sIIIII');CANDIDATE=struct.Struct('<IIIIQQ')
MAX_RECORD=10*1024**2;MAX_METADATA=65536;NO_TILE=0xffffffff

def count(row,key,bound=2**64-1):
 value=row.get(key)
 if type(value)is not int or not 0<=value<=bound:raise ValueError('invalid missing counter '+key)
 return value

def validate_missing_manifest(m):
 if m.get('version')!=1 or m.get('design')!=DESIGN or m.get('complete')is not True or m.get('censored')is not False:raise ValueError('incomplete/censored missing stream')
 limits={'input_pending_limit':64*1024**2,'pending_limit':2,'encoded_pending_limit':20*1024**2,'record_limit':MAX_RECORD,'final_limit':80*1024**2,'transient_limit':24*1024**2,'free_reserve':8*1024**3,'max_admissions':32768}
 for k,v in limits.items():
  if count(m,k)!=v:raise ValueError('missing declared allowance')
 for k in ('peak_input_reservations','peak_input_payload'):count(m,k,limits['input_pending_limit'])
 if m['peak_input_payload']>m['peak_input_reservations']:raise ValueError('missing input accounting')
 calls=count(m,'calls');sampled=count(m,'sampled',calls);admitted=count(m,'admitted',32768)
 if count(m,'completed')!=admitted or not count(m,'sample_every',2**32-1):raise ValueError('missing completion/sampling')
 obsolete=count(m,'obsolete_completed',admitted)
 count(m,'oracle_elapsed_ns');slots=m.get('strata')
 if not isinstance(slots,list) or len(slots)!=8:raise ValueError('missing eight strata')
 eligible=0;ordinals=set()
 for i,s in enumerate(slots):
  if count(s,'id')!=i or s.get('censored')is not False:raise ValueError('missing stratum identity/censor')
  n=count(s,'eligible',sampled);eligible+=n;r=s.get('reasons')
  if not isinstance(r,list) or len(r)!=3 or any(type(v)is not int or not 0<=v<=n for v in r) or sum(r)!=n:raise ValueError('missing reasons')
  if (i<4 and (r[1] or r[2])) or (i>=4 and r[0]):raise ValueError('missing category mismatch')
  for k in ('excluded_sample_cap','excluded_queue_cap','excluded_input_bytes'):
   if count(s,k):raise ValueError('missing censored admission')
  count(s,'ties',n);count(s,'priority');o=count(s,'ordinal',calls);written=count(s,'captured_ordinal',calls);replacements=count(s,'replacements',n)
  if n:
   if not 0<o==written or o in ordinals or not replacements or s.get('file')!=f'event-0{i}.bin':raise ValueError('missing final minimum')
   if not isinstance(s.get('file_sha256'),str) or len(s['file_sha256'])!=64:raise ValueError('missing record hash')
   ordinals.add(o)
  elif o or written or replacements or s.get('file')is not None or s.get('file_sha256')is not None:raise ValueError('nonempty absent slot')
 if sum(s['replacements'] for s in slots)+obsolete!=admitted:raise ValueError('missing completed/write conservation')
 if eligible+count(m,'excluded_mixed',sampled)+count(m,'valid_destination',sampled)!=sampled:raise ValueError('missing classification conservation')
 return slots

def read_missing_captures(directory,manifest,*,expected_provenance):
 directory=Path(directory)
 if isinstance(manifest,(str,Path)):
  if Path(manifest).stat().st_size>MAX_METADATA:raise ValueError('missing manifest size')
  manifest=json.loads(Path(manifest).read_text())
 slots=validate_missing_manifest(manifest);paths=list(directory.iterdir());expected={'manifest.json'}|{s['file'] for s in slots if s['eligible']}
 if any(p.is_symlink() or not p.is_file() for p in paths) or {p.name for p in paths}!=expected:raise ValueError('missing exact file inventory')
 if sum(p.stat().st_size for p in paths)>80*1024**2+MAX_METADATA:raise ValueError('missing directory bound')
 for i,s in enumerate(slots):
  if not s['eligible']:yield i,s,None;continue
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
  if m.get('design')!=DESIGN or m.get('stratum')!=i or m.get('ordinal')!=s['ordinal'] or m.get('priority')!=s['priority']:raise ValueError('missing minimum identity')
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
  yield i,s,(m,labels,rows)
