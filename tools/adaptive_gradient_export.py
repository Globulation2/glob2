#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Seal successes and validated complete episodes from explicitly censored games.

Raw coordinator receipts and transported bytes remain immutable. A derived CSV
never claims that its terminated trajectory was a completed game.
"""
import argparse,collections,csv,gzip,hashlib,io,json,shutil,tempfile,os
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:sys.path.insert(0,str(ROOT))
from tools import adaptive_gradient_dataset as d
from tools import adaptive_gradient_collection as collector
from tools.resource_gradient_dataset import seal
from tools.resource_gradient_distributed import export as completed_export
from tools.resource_gradient_tournament import map_identity
from tools.tournaments.results import Results
from tools.tournaments.common import atomic_json
from tools.tournaments.bundles import inspect_bundle

def recover_complete_records(stream,writer):
 """Stream a numeric probeCSV without loading a capped trajectory into RAM."""
 header=None;count=incomplete=discarded=decoded=0;digest=hashlib.sha256()
 for line in stream:
  digest.update(line);decoded+=len(line)
  if not line.endswith(b'\n'):discarded+=len(line);continue
  values=next(csv.reader([line.decode('utf-8')]))
  if header is None:header=values;writer.writerow(header);continue
  if len(values)!=len(header):raise ValueError('Malformed interior partial CSV row')
  parsed={k:(v if k=='overlap_support_reason' else int(v)) for k,v in zip(header,values)}
  if 'overlap_support_reason' in parsed and parsed['overlap_support_reason'] not in ('disabled','natural_ground','clear_ground','guard_weighted_seeds','forbidden_border_seeds','market_supplier_seeds','air_unvalidated'):raise ValueError('Unknown spatial support reason')
  if parsed.get('complete') not in (0,1):raise ValueError('Invalid original completeness flag')
  if not parsed['complete']:incomplete+=1;continue
  writer.writerow(values);count+=1
 return dict(header=header,count=count,incomplete=incomplete,discarded=discarded,decoded_bytes=decoded,decoded_sha256=digest.hexdigest())

def export(directory,output):
 reader=Results(directory);output=Path(output);output.mkdir(parents=True,exist_ok=True)
 if any(not collector.compatible_contract(j['labels']['resource_gradient']['cohort']['contract']) for j in reader.manifest['jobs']):raise ValueError('Only supported all-family collection contracts may be exported here')
 completed=completed_export(directory,output/'completed',allow_partial=True);success={r['job']['id'] for r in reader if r.get('category')=='success' and r.get('result',{}).get('status')=='completed' and not any(a['path']=='runtime-cap-override.json' for a in r.get('artifacts',[]))};expected={j['id']:j for j in reader.manifest['jobs']};attempts=collections.defaultdict(list)
 for r in reader.attempts():
  if r['job']['id'] not in expected or r['job']!=expected[r['job']['id']]:raise ValueError('Attempt changed frozen job')
  attempts[r['job']['id']].append(r)
 cohorts={};accounting=[];sealed=[]
 for key,job in expected.items():
  if key in success:accounting.append(dict(group=key,status='completed'));continue
  candidates=attempts.get(key,[])
  if not candidates:accounting.append(dict(group=key,status='unresolved'));continue
  r=max(candidates,key=lambda r:r['finished']);meta={a['path']:a for a in r.get('artifacts',[])};external=None
  if 'runtime-cap-override.json' in meta:
   with reader.open_artifact(r,'runtime-cap-override.json') as stream:external=json.load(stream)
   if external.get('category')!='censored-user-wall-cap' or external.get('job')!=key or external.get('effective_cap_seconds')!=1800:raise ValueError('Invalid external cap provenance')
  censored=(r['category']=='timeout' and job['limits'].get('timeout_seconds')==1800) or external is not None
  if not censored:accounting.append(dict(group=key,status='other-failure',category=r['category']));continue
  dest=output/'censored'/key;dest.mkdir(parents=True,exist_ok=True);raw=[]
  for a in r.get('artifacts',[]):
   original=reader.root/'artifacts'/a['sha256'];target=dest/'transport'/a['sha256'];target.parent.mkdir(exist_ok=True)
   if d.file_hash(original)!=a['sha256']:raise ValueError('Raw partial artifact hash mismatch')
   if not target.exists():shutil.copyfile(original,target)
   if d.file_hash(target)!=a['sha256']:raise ValueError('Immutable partial export changed')
   raw.append(dict(**a,preserved_transport=str(target.resolve())))
  atomic_json(dest/'attempt.json',r);status=dict(group=key,status='user-censored',raw_category=r['category'],attempt=r['id'],observed_through_tick=(r.get('last_progress') or {}).get('tick'),artifacts=raw,episode_rows_recovered=0,whole_game_totals_complete=False)
  if 'gradient-episodes.csv' not in meta:status['exclusion']='No flushed episode stream';accounting.append(status);continue
  a=meta['gradient-episodes.csv'];derivative=dest/'complete-episodes.csv.gz'
  with tempfile.NamedTemporaryFile(dir=dest,delete=False) as temporary:
   temporary_path=Path(temporary.name)
   try:
    with gzip.GzipFile(filename='',fileobj=temporary,mode='wb',mtime=0) as compressed:
     with io.TextIOWrapper(compressed,encoding='utf-8',newline='') as text,reader.open_artifact(r,'gradient-episodes.csv','rb') as stream:
      recovered=recover_complete_records(stream,csv.writer(text,lineterminator='\n'))
    if recovered['decoded_sha256']!=a.get('raw_sha256') or recovered['decoded_bytes']!=a.get('raw_bytes'):raise ValueError('Decoded partial differs from receipt')
    if not recovered['header']:status['exclusion']='No complete header';accounting.append(status);continue
    temporary.flush()
    if derivative.exists():
     if d.file_hash(derivative)!=d.file_hash(temporary_path):raise ValueError('Derivative export changed')
    else:os.replace(temporary_path,derivative)
   finally:temporary_path.unlink(missing_ok=True)
  count=recovered['count'];discarded=recovered['discarded'];incomplete=recovered['incomplete']
  status.update(episode_rows_recovered=count,discarded_unterminated_bytes=discarded,incomplete_horizon_rows_excluded=incomplete,derivative_sha256=d.file_hash(derivative));labels=job['labels']['resource_gradient'];cohort=labels['cohort'];build=inspect_bundle(reader.root/'builds'/job['build']);binary=next(f['sha256'] for f in build['files'] if f['path']==build['executable']);spec=labels['actual_spec']
  record=dict(group=key,map_group=map_identity(spec),format=spec['format'],spec=spec,binary_sha256=binary,episodes=str(derivative.resolve()),episode_sha256=d.file_hash(derivative),trajectory_status='user-censored',observed_through_tick=status['observed_through_tick'],cohort=cohort,provenance=dict(raw_attempt_sha256=d.file_hash(dest/'attempt.json'),original_episode_transport=a,derivation='only-original-complete1-newline-terminated-numeric-records-v1',discarded_unterminated_bytes=discarded,qualification='complete episodes only; not a completed game'))
  if count:cohorts.setdefault(cohort['id'],dict(metadata=cohort,records=[]))['records'].append(record)
  accounting.append(status)
 for cid,value in cohorts.items():
  name=d.canonical_hash(value)[:24];rawfile=output/(name+'.records.json');atomic_json(rawfile,value['records']);target=output/(name+'.cohort.json');seal(rawfile,target,value['metadata']);rawfile.unlink();collector.load([target]);sealed.append(str(target.resolve()))
 result=dict(expected=len(expected),counts=dict(collections.Counter(r['status'] for r in accounting)),completed_manifests=completed['manifests'],censored_manifests=sealed,slots=accounting,cohort_weighting_unchanged=True,note='Censored raw evidence retained; complete horizon rows validated by unchanged unified loader. No fit or promotion.')
 atomic_json(output/'terminal-accounting.json',result);return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('directory');p.add_argument('--output',required=True);a=p.parse_args();print(json.dumps(export(a.directory,a.output),indent=2))
