#!/usr/bin/env python3
"""Read-only bounded AIT1-tail analysis of retained frozen latest legacy replays.
Writes derived JSON/HTML only. Never calls engine, compiler or profiler.
"""
from pathlib import Path
import collections,datetime,hashlib,html,json,os,struct
ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'engine-history/snapshots/2026-10-11-9a08b91f4/source'
CASES=['check-t4-gpuoff','oazis-window-s1056243202-r1-t4-gpuoff']
LIMIT=16*1024*1024;MAX_TEAM=16;MAX_FIELDS=4096+132;MAX_FILE=256*1024*1024;MAX_CANDIDATES=128
class Invalid(ValueError):pass
class Reader:
 def __init__(self,data,start):self.data=data;self.pos=start;self.start=start;self.budget=LIMIT
 def take(self,n):
  if n<0 or self.pos+n>len(self.data):raise Invalid('Truncated field')
  x=self.data[self.pos:self.pos+n];self.pos+=n;return x
 def u32(self):return struct.unpack('>I',self.take(4))[0]
 def boolean(self):
  v=self.take(1)[0]
  if v not in (0,1):raise Invalid('Not writer-canonical bool')
  return bool(v)
 def spend(self,n):
  if n>self.budget:raise Invalid('16MiB logical budget exceeded')
  self.budget-=n
 def text(self):
  n=self.u32()
  if n>4096:raise Invalid('Text exceeds source4096 limit')
  self.spend(n);return self.take(n)
def signature(meta):return b''.join(struct.pack('>I',len(s))+s for s in meta)
def sha(data):return hashlib.sha256(data).hexdigest()
def parse(data,start):
 r=Reader(data,start)
 if r.u32()!=0x41495431 or r.u32()!=1:raise Invalid('Unsupported magic/version')
 until=r.u32();truncated=r.boolean();count=r.u32()
 if count>65536:raise Invalid('Frame count exceeds source bound')
 # Minimum17byte trailer,8byte frames; fail impossible candidates before loops.
 if 8*count>len(data)-r.pos:raise Invalid('Impossible frame length')
 frames=[];last=-1
 for _ in range(count):
  r.spend(8);step=r.u32();rows=r.u32()
  if step<=last or step>until or rows>MAX_TEAM*2:raise Invalid('Invalid step or row count')
  if rows*17>len(data)-r.pos:raise Invalid('Impossible row length')
  last=step;changes=[]
  for _ in range(rows):
   r.spend(32);team=r.u32();player=r.u32();name=r.text()
   if team>=MAX_TEAM or player>=MAX_TEAM:raise Invalid('Invalid team/player')
   available=r.boolean();fields=r.u32()
   if fields>MAX_FIELDS:raise Invalid('Too many values')
   r.spend(fields*24)
   if fields*20>len(data)-r.pos:raise Invalid('Impossible value length')
   values=[]
   for _ in range(fields):
    n=r.text();v=r.text();u=r.text();m=r.text();updated=r.u32();values.append((n,v,u,m,updated))
   changes.append((team,player,name,available,values))
  frames.append((step,changes))
 if r.pos!=len(data):raise Invalid('Tail does not terminate at EOF')
 return dict(start=start,until=until,truncated=truncated,frames=frames,logical_budget_used=LIMIT-r.budget,wire_bytes=r.pos-start)
def summarize(tail):
 counter=collections.Counter();metadata=collections.Counter();names=collections.Counter();schemas=collections.Counter();prev={};players=collections.defaultdict(collections.Counter)
 for step,rows in tail['frames']:
  counter['frames']+=1;counter['frame_rows_total']+=len(rows)
  counter['duplicate_player_rows_within_frame']+=len(rows)-len({row[1] for row in rows})
  for team,player,name,available,values in rows:
   pc=players[player];pc['row_records']+=1;counter['rows_available' if available else 'rows_unavailable']+=1
   names[name]+=1;counter['row_name_bytes']+=len(name)
   metas=[(v[0],v[2],v[3]) for v in values];ms=collections.Counter(metas);ns=collections.Counter(v[0] for v in values)
   counter['duplicate_exact_metadata_occurrences_within_rows']+=sum(n-1 for n in ms.values());counter['duplicate_name_occurrences_within_rows']+=sum(n-1 for n in ns.values())
   schema=b''.join(struct.pack('>I',len(signature(m)))+signature(m) for m in metas);schemas[(player,schema)]+=1
   counter['values_total']+=len(values);pc['value_records']+=len(values)
   for n,v,u,m,updated in values:
    metadata[(n,u,m)]+=1;counter['metadata_text_bytes_repeated']+=len(n)+len(u)+len(m);counter['value_text_bytes']+=len(v)
    if updated>step:counter['updated_after_recorded_frame_step']+=1
   if player not in prev:
    counter['initial_values']+=len(values);pc['first_step']=step
   else:
    pteam,pname,pavailable,pvalues=prev[player];pc['row_transitions']+=1;counter['row_transitions']+=1
    if team!=pteam:counter['team_changes']+=1
    if name!=pname:counter['player_name_changes']+=1
    if available!=pavailable:counter['availability_changes']+=1
    oldmetas=[(v[0],v[2],v[3]) for v in pvalues]
    if oldmetas!=metas:counter['ordered_schema_changes']+=1;pc['ordered_schema_changes']+=1
    if values==pvalues:counter['identical_values_vectors']+=1
    if (team,name,available,values)==(pteam,pname,pavailable,pvalues):counter['identical_complete_rows']+=1
    counter['tail_positions_added']+=max(0,len(values)-len(pvalues));counter['tail_positions_removed']+=max(0,len(pvalues)-len(values))
    for position,(old,new) in enumerate(zip(pvalues,values)):
     if (old[0],old[2],old[3])!=(new[0],new[2],new[3]):counter['overlapping_positions_metadata_changed']+=1;continue
     counter['comparable_same_metadata_positions']+=1;pc['comparable_positions']+=1
     value_changed=old[1]!=new[1];updated_changed=old[4]!=new[4]
     k=('value_and_updated_changed' if value_changed and updated_changed else 'value_only_changed' if value_changed else 'updated_only_changed' if updated_changed else 'value_and_updated_unchanged')
     counter[k]+=1;pc[k]+=1
   pc['last_step']=step;prev[player]=(team,name,available,values)
 # Counts preserve exactbytes/order/duplicates, never collapse records by name.
 meta_rows=[]
 for m,n in metadata.most_common():
  sig=signature(m);meta_rows.append(dict(signature_sha256=sha(sig),occurrences=n,text_bytes=sum(map(len,m)),canonical_length_prefixed_bytes=len(sig),name_hex=m[0].hex(),unit_hex=m[1].hex(),meaning_hex=m[2].hex(),name_display=m[0].decode('utf-8','backslashreplace'),unit_display=m[1].decode('utf-8','backslashreplace'),meaning_display=m[2].decode('utf-8','backslashreplace')))
 unique_bytes=sum(sum(map(len,m)) for m in metadata);unique_wire=sum(len(signature(m)) for m in metadata)
 F=counter['frames'];R=counter['frame_rows_total'];V=counter['values_total']
 breakdown=dict(tail_header=17,frame_headers=8*F,row_fixed_fields=17*R,row_name_text=counter['row_name_bytes'],value_fixed_lengths_and_updated=20*V,metadata_text=counter['metadata_text_bytes_repeated'],value_text=counter['value_text_bytes'])
 assert sum(breakdown.values())==tail['wire_bytes']
 assert counter['comparable_same_metadata_positions']==sum(counter[k] for k in ['value_and_updated_changed','value_only_changed','updated_only_changed','value_and_updated_unchanged'])
 for key in ['value_only_changed','value_and_updated_changed','value_and_updated_unchanged','updated_only_changed','team_changes','player_name_changes','availability_changes','duplicate_player_rows_within_frame','duplicate_exact_metadata_occurrences_within_rows','duplicate_name_occurrences_within_rows','updated_after_recorded_frame_step']:counter.setdefault(key,0)
 return dict(tail_offset=tail['start'],tail_wire_bytes=tail['wire_bytes'],telemetry_until=tail['until'],truncated=tail['truncated'],logical_budget_used=tail['logical_budget_used'],first_frame_step=tail['frames'][0][0] if F else None,last_frame_step=tail['frames'][-1][0] if F else None,counts=dict(counter),player_counts={str(k):dict(v) for k,v in sorted(players.items())},wire_breakdown=breakdown,metadata=dict(unique_exact_triples=len(metadata),unique_text_bytes=unique_bytes,repeated_text_bytes=counter['metadata_text_bytes_repeated'],repeated_length_prefixed_bytes=counter['metadata_text_bytes_repeated']+12*V,unique_length_prefixed_bytes=unique_wire,triples=meta_rows),row_names=dict(unique_exact_names=len(names),unique_text_bytes=sum(map(len,names)),repeated_text_bytes=counter['row_name_bytes']),ordered_schemas=[dict(player=k[0],signature_sha256=sha(k[1]),row_occurrences=n) for k,n in schemas.items()])
def analyze(case):
 folder=ROOT/'runs'/case;path=folder/'profile/replays/last_game.replay';recordpath=folder/'result.json';record=json.loads(recordpath.read_text())
 if path.stat().st_size>MAX_FILE:raise Invalid('Replay file bound exceeded')
 data=path.read_bytes();offsets=[];cursor=0
 while True:
  at=data.find(b'AIT1',cursor)
  if at<0:break
  offsets.append(at);cursor=at+1
  if len(offsets)>MAX_CANDIDATES:raise Invalid('Candidate count bound exceeded')
 good=[];bad=[]
 for at in offsets:
  try:good.append(parse(data,at))
  except Invalid as e:bad.append(dict(offset=at,reason=str(e)))
 if len(good)!=1:raise Invalid('Require exactlyone structurally valid EOF AIT1 tail; found '+str(len(good)))
 result=summarize(good[0]);result.update(case=case,replay_path=str(path.resolve()),replay_sha256=sha(data),replay_file_bytes=len(data),tail_sha256=sha(data[result['tail_offset']:]),result_path=str(recordpath.resolve()),result_sha256=sha(recordpath.read_bytes()),source_command=record['command'],binary_sha256=record['binary_sha256'],reported=record['reported'],ticks_actual=record['ticks_actual'],seed=record['seed'],candidate_magic_offsets=offsets,rejected_candidates=bad)
 return result

def validation_checks():
 def u(n):return struct.pack('>I',n)
 def text(v):return u(len(v))+v
 def row(updated):
  value=lambda v: text(b'N\0')+text(v)+text(b'U')+text(b'M')+u(updated)
  return u(0)+u(0)+text(b'P')+b'\1'+u(2)+value(b'1')+value(b'2')
 sample=b'AIT1'+u(1)+u(32)+b'\0'+u(2)+u(0)+u(1)+row(0)+u(32)+u(1)+row(32)
 parsed=summarize(parse(sample,0));assert parsed['counts']['duplicate_exact_metadata_occurrences_within_rows']==2
 assert parsed['metadata']['unique_exact_triples']==1 and parsed['counts']['updated_only_changed']==2
 tests={'embedded_NUL_exact_metadata_duplicates_and_update_only':True}
 cases={'truncated_tail':sample[:-1],'trailing_byte':sample+b'x','bad_version':sample[:4]+u(2)+sample[8:],
        'frame_count_over_limit':sample[:13]+u(65537)+sample[17:],
        'row_count_over_limit':sample[:21]+u(33)+sample[25:],
        'text_length_over_limit':sample[:33]+u(4097)+sample[37:]}
 for name,data in cases.items():
  try:parse(data,0)
  except Invalid:tests[name]=True
  else:raise AssertionError('Malformed parser input accepted: '+name)
 return tests

def main():
 os.sched_setaffinity(0,{15,31})
 paths=['src/replay/ReplayTelemetry.cpp','src/replay/ReplayTelemetry.h','src/replay/ReplayWriter.cpp','src/replay/ReplayReader.cpp','libgag/src/BinaryStream.cpp','libgag/include/BinaryStream.h','src/ai/telemetry/AITelemetry.h','src/team/Team.h']
 result=dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),frozen_commit='9a08b91f4350649862e788135e99bb1f5f469e3a',parser_sha256=sha(Path(__file__).read_bytes()),cpu_affinity=[15,31],source_provenance=[dict(path=str((SOURCE/n).resolve()),sha256=sha((SOURCE/n).read_bytes())) for n in paths],parser_validation_checks=validation_checks(),format=dict(magic_hex='41495431',version=1,endian='big/network: BinaryOutputStream htonl, BinaryInputStream ntohl',frame_limit=65536,row_limit=MAX_TEAM*2,team_player_limit=MAX_TEAM,value_limit=MAX_FIELDS,text_limit=4096,logical_budget=LIMIT),cases=[analyze(c) for c in CASES],limitations=['No engine/build/profiler run; existing replay files read unchanged.','All candidate magic locations require full structural/count/length/budget validation and exact EOF; exactlyonevalidtail required. Prefix game/save/order stream not fully decoded, so this is not complete replay load/acceptance validation.','Byte-preserving metadata triples are ordered and duplicates retained. Comparisons use player and vector position; metadata changes are separated from value changes.','Only captured changed rows are retained: row transitions compare the last retained row per player, not every game tick. Truncation means later telemetry is absent even if the game continues.','Logical16MiB accounting and wirebytes are not live heap/RSS. Unique repeated bytes quantify representation redundancy but imply no resident-memory or CPU savings; dictionaries/deltas have additional overhead.','Interning or compact/delta storage must reconstruct exact metadata/value/updated/order/duplicates/availability/frame history and preserve existing AIT1 persistence payload, not merely rendered values. Reserve prototype tests remain pending.'])
 (ROOT/'replay-wire-analysis.json').write_text(json.dumps(result,indent=2)+'\n')
 sections=[]
 for c in result['cases']:
  n=c['counts'];m=c['metadata'];rows=[('Frames / row records / value records',f"{n['frames']:,} / {n['frame_rows_total']:,} / {n['values_total']:,}"),('AIT1 wire / logical budget used',f"{c['tail_wire_bytes']:,} / {c['logical_budget_used']:,} bytes"),('Truncated / final retained step',f"{c['truncated']} / {c['last_frame_step']} (game ticks {c['ticks_actual']})"),('Unique metadata triples / exact text bytes',f"{m['unique_exact_triples']:,} / {m['unique_text_bytes']:,}"),('Repeated metadata text / length-prefixed bytes',f"{m['repeated_text_bytes']:,} / {m['repeated_length_prefixed_bytes']:,}"),('Same-metadata comparable positions',f"{n.get('comparable_same_metadata_positions',0):,}"),('Value and updated unchanged / updated only changed',f"{n.get('value_and_updated_unchanged',0):,} / {n.get('updated_only_changed',0):,}"),('Value only changed / both changed',f"{n.get('value_only_changed',0):,} / {n.get('value_and_updated_changed',0):,}"),('Ordered schema changes / duplicate metadata occurrences',f"{n.get('ordered_schema_changes',0):,} / {n.get('duplicate_exact_metadata_occurrences_within_rows',0):,}")]
  sections.append('<section><h2>'+html.escape(c['case'])+'</h2><table>'+''.join('<tr><th>'+html.escape(k)+'</th><td>'+html.escape(v)+'</td></tr>' for k,v in rows)+'</table><p>Replay SHA256 <code>'+c['replay_sha256']+'</code>; validated EOF tail offset '+str(c['tail_offset'])+'.</p></section>')
 page='<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Replay telemetry wire analysis</title><style>body{font:16px/1.55 system-ui;max-width:1000px;margin:36px auto;padding:0 24px;color:#233044}section{border:1px solid #ccd6e3;border-radius:8px;padding:20px;margin:22px 0}h2{font-size:20px}th,td{text-align:left;padding:7px;border-bottom:1px solid #ddd}code{word-break:break-all}.note{background:#fff7dd;padding:16px}</style><h1>Retained replay telemetry: exact wire redundancy</h1><p>Two existing frozen latest legacy8k replays use the same four-participant CPU-off binary and seed: Playground128 with8AIs/9controllers, Oazis256 with11AIs/12controllers. This analysis validates the bounded AIT1 tail and counts exact metadata bytes and position-specific value/update transitions.</p><p class="note">These are wire and logical-budget counts, not heap/RSS savings. Telemetry truncation and schema changes matter. Preserve complete history, duplicate fields, row order, timestamps and the existing AIT1 payload. The reserve experiment has no demonstrated benefit yet.</p>'+''.join(sections)+'<h2>Representation opportunities</h2><p>Exact metadata triples can potentially share immutable storage; stable positional schemas can potentially refer to a schema identifier while retaining explicit changes and duplicates. Unchanged value/update pairs could use internal deltas, while update-only changes still need their timestamp. All designs require exact reconstruction and measured memory/CPU controls; the byte counts do not establish savings.</p><h2>Validation limits</h2><ul>'+''.join('<li>'+html.escape(v)+'</li>' for v in result['limitations'])+'</ul><p><a href="replay-wire-analysis.json">Full counts, metadata signatures, source/file provenance</a> · <a href="replay-wire-analysis.py">Bounded parser</a></p></html>'
 (ROOT/'replay-wire-analysis.html').write_text(page)
 for c in result['cases']:print(c['case'],json.dumps({k:c[k] for k in ['tail_wire_bytes','logical_budget_used','telemetry_until','truncated','counts']},indent=2));print('metadata',{k:v for k,v in c['metadata'].items() if k!='triples'})
if __name__=='__main__':main()
