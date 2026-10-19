"""Artifact-only conservative terminal recovery. Never modifies frozen tools/results."""
import collections,csv,datetime,gzip,hashlib,io,json,os,pathlib,shutil,sys,tarfile
BASE=pathlib.Path('/home/bradley/glob2/artifacts/gradient-contradictions-20261018');ROOT=BASE/'postprocess-cumulative-round002';sys.path.insert(0,str(ROOT/'source'))
from tools.resource_gradient_distributed import export
from tools.resource_gradient_dataset import canonical_hash,file_hash,load,seal,support_audit,manifest_records
from tools.resource_gradient_tournament import map_identity
from tools.tournaments.results import Results
from tools.tournaments.bundles import inspect_bundle

def write(p,a):
 p.parent.mkdir(parents=True,exist_ok=True);s=json.dumps(a,indent=2)+'\n'
 if p.exists():assert p.read_text()==s,('immutable output changed',str(p))
 else:p.write_text(s)

def verify_artifacts(reader,record):
 out={}
 for a in record['artifacts']:
  digest=hashlib.sha256();n=0
  with reader.open_artifact(record,a['path'],'rb') as f:
   for b in iter(lambda:f.read(1024*1024),b''):digest.update(b);n+=len(b)
  assert digest.hexdigest()==a['raw_sha256'] and n==a['raw_bytes'],a['path']
  out[a['path']]={'transport_path':str(reader.root/'artifacts'/a['sha256']),'transport':a,'decoded_sha256':digest.hexdigest(),'decoded_bytes':n}
 return out

def recover(record,labels,binary,raw,proof,folder):
 data=raw('gradient-episodes.csv');end=data.rfind(b'\n')+1;valid=data[:end];suffix=data[end:]
 lines=valid.splitlines(keepends=True);assert lines,'missing terminated header'
 assert all(line.endswith(b'\n') for line in lines),'interior record lacks newline terminator'
 header=next(csv.reader([lines[0].decode()],strict=True));assert len(header)==len(set(header))
 required={'tick','complete','defer_extra','refresh_extra','movement_observed','contradiction_capture_known','defer_stalls','refresh_stalls'}
 assert required<=set(header),header
 selected=[lines[0]];counts=collections.Counter();ticks=[]
 for b in lines[1:]:
  fields=next(csv.reader([b.decode()],strict=True));assert len(fields)==len(header),'malformed interior row'
  row={k:int(v) for k,v in zip(header,fields)};assert row['complete'] in [0,1]
  counts['rows']+=1
  if row['complete']!=1:counts['incomplete_excluded']+=1;continue
  selected.append(b);counts['complete_retained']+=1;ticks.append(row['tick'])
 if not ticks:raise ValueError('No complete recoverable episodes: '+record['job']['id'])
 dest=folder/'partial-episodes'/record['job']['id'];dest.mkdir(parents=True,exist_ok=True);ep=dest/'complete-original-episodes.csv.gz'
 if not ep.exists():
  with ep.open('xb') as f:
   with gzip.GzipFile(filename='',fileobj=f,mode='wb',mtime=0) as g:g.write(b''.join(selected))
 with gzip.open(ep,'rb') as f:assert f.read()==b''.join(selected)
 spec=labels['actual_spec'];derivation=dict(protocol='original-complete1-newline-terminated-strict-numeric-csv-v1',counts=dict(counts),discarded_unterminated_suffix_bytes=len(suffix),discarded_unterminated_suffix_sha256=hashlib.sha256(suffix).hexdigest(),raw_episode_sha256=hashlib.sha256(data).hexdigest(),raw_episode_bytes=len(data),retained_tick_min=min(ticks),retained_tick_max=max(ticks),complete_future_offsets=labels['cohort']['contract']['future_offsets'],last_progress=record.get('last_progress'),censoring=proof)
 write(dest/'derivation.json',derivation);write(dest/'raw-attempt-receipt.json',record)
 value=dict(group=record['job']['id'],map_group=map_identity(spec),format=spec['format'],spec=spec,episodes=str(ep),episode_sha256=file_hash(ep),binary_sha256=binary,map_sha256=labels['map_sha256'],ticks=record.get('last_progress',{}).get('tick'),termination='censored-partial-original-complete-episodes-only',cohort=labels['cohort'],provenance=dict(host=record['host'],attempt=record['id'],bundle=record['job']['build'],source_revision=proof['source_revision'],model_sha256=labels['model_sha256'],header_sha256=labels['header_sha256'],collection_seed=labels['collection_seed'],partial=True,censoring=proof,derivation=str(dest/'derivation.json'),raw_artifacts=proof['raw_artifacts'],artifacts={},note='Diagnostic totals are unknown/incomplete for censored trajectory; raw files preserved but deliberately excluded from whole-game totals.'))
 return value,derivation

def run(name,root,capped_archive=False):
 folder=ROOT/name;folder.mkdir(exist_ok=True)
 if (folder/'TERMINAL-SEAL.json').exists():return json.loads((folder/'TERMINAL-SEAL.json').read_text())['manifests']
 reader=Results(root);expected={j['id']:j for j in reader.manifest['jobs']};success=list(reader);assert len({r['job']['id'] for r in success})==len(success)
 assert all(r['category']=='success' and r.get('accepted') is True for r in success)
 failure=[json.loads(p.read_text()) for p in (root/'failures').glob('*.json')];archive_records={};retirement={}
 if capped_archive:
  arc=BASE/'capped-round001';meta=json.loads((arc/'partial-manifest.json').read_text());assert file_hash(arc/'partial-attempts.tar.gz')==meta['archive_sha256'];target=folder/'retired-raw';target.mkdir(exist_ok=True)
  with tarfile.open(arc/'partial-attempts.tar.gz') as tar:
   for member in tar:
    path=pathlib.PurePosixPath(member.name);assert not path.is_absolute() and '..' not in path.parts
    if not member.isfile():continue
    b=tar.extractfile(member).read();identity=meta['files'][member.name];assert len(b)==identity['bytes'] and hashlib.sha256(b).hexdigest()==identity['sha256'];p=target/member.name;p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():assert p.read_bytes()==b
    else:p.write_bytes(b)
  assert set(meta['files'])=={str(p.relative_to(target)) for p in target.rglob('*') if p.is_file()}
  retirement=json.loads((arc/'retirement.json').read_text());active={x['attempt']:x for x in retirement['active']}
  for p in target.glob('*/record.json'):
   r=json.loads(p.read_text());assert r['id'] in active and active[r['id']]['job']==r['job']['id'];failure.append(r);archive_records[r['id']]=p.parent
 full={r['job']['id'] for r in success};partial={r['job']['id'] for r in failure};assert len(partial)==len(failure) and not full&partial
 assert full|partial==set(expected),('terminal account missing',set(expected)-full-partial)
 status=export(root,folder/'completed-only',allow_partial=True);assert status['completed']==len(success)
 manifests=list(status['manifests']);recovered=collections.defaultdict(list);declarations={};audit=[]
 for r in success:
  assert r['job']==expected[r['job']['id']]
  assert not any(a['path']=='runtime-cap-override.json' for a in r['artifacts']),'Cap marker inside nominal success: must classify censored first'
 for r in failure:
  assert r['job']==expected[r['job']['id']];labels=r['job']['labels']['resource_gradient'];build=inspect_bundle(root/'builds'/r['job']['build']);binary=next(x['sha256'] for x in build['files'] if x['path']==build['executable'])
  if r['id'] in archive_records:
   directory=archive_records[r['id']];raw=lambda name,d=directory:(d/'output'/name).read_bytes();artifacts={}
   for x in r['artifacts']:
    p=directory/'output'/x['path']
    if p.exists():assert file_hash(p)==x['raw_sha256'] and p.stat().st_size==x['raw_bytes'];artifacts[x['path']]={'path':str(p),'decoded_sha256':file_hash(p),'decoded_bytes':p.stat().st_size,'transport':x}
   proof={'category':'censored-explicit-user-retirement','raw_worker_category':r['category'],'retirement_path':str(BASE/'capped-round001/retirement.json'),'retirement_sha256':file_hash(BASE/'capped-round001/retirement.json'),'raw_artifacts':artifacts}
  else:
   artifacts=verify_artifacts(reader,r);raw=lambda name,r=r:reader.open_artifact(r,name,'rb').read()
   if 'runtime-cap-override.json' in artifacts:
    marker=json.loads(raw('runtime-cap-override.json'));assert marker['category']=='censored-user-wall-cap' and marker['experiment']==r['experiment'] and marker['attempt']==r['id'] and marker['job']==r['job']['id'];assert marker['job_sha256']==canonical_hash(r['job']);assert marker['effective_cap_seconds']==1800 and marker['elapsed_seconds']>=1800
    proof={'category':'censored-user-wall-cap','raw_worker_category':r['category'],'marker':marker,'raw_artifacts':artifacts}
   else:
    if r['category']=='timeout' and r['job']['limits']['timeout_seconds']==1800 and r['seconds']>=1800:
     proof={'category':'censored-frozen-worker-timeout','raw_worker_category':r['category'],'timeout_seconds':1800,'observed_seconds':r['seconds'],'raw_artifacts':artifacts}
    else:
     proof={'category':'other-failure-no-verified-cap','raw_worker_category':r['category'],'exit_code':r.get('exit_code'),'observed_seconds':r['seconds'],'raw_artifacts':artifacts,'note':'Termination cause unknown; do not label as cap. Only original complete validated episodes retained.'}
  proof['source_revision']=build['source_revision']
  value,derivation=recover(r,labels,binary,raw,proof,folder);key=(labels['cohort']['id'],binary);recovered[key].append(value);declarations[key]=labels['cohort'];audit.append(dict(job=r['job']['id'],attempt=r['id'],format=value['format'],spec=value['spec'],censor_category=proof['category'],raw_category=r['category'],**derivation['counts']))
 for key,records in recovered.items():
  identifier=canonical_hash(dict(cohort=key[0],binary=key[1],groups=sorted(r['group'] for r in records)))[:24];tmp=folder/(identifier+'.records.json');write(tmp,records);dest=folder/(identifier+'.partial.cohort.json');seal(tmp,dest,declarations[key]);manifests.append(str(dest))
 rows=load(manifests);report={'planned':len(expected),'full_successes':len(success),'partial_trajectories':len(failure),'verified_censored_partials':sum(x['censor_category'].startswith('censored-') for x in audit),'other_failure_partials':sum(not x['censor_category'].startswith('censored-') for x in audit),'unresolved':0,'by_format_planned':dict(collections.Counter(j['labels']['resource_gradient']['actual_spec']['format'] for j in expected.values())),'by_format_full':dict(collections.Counter(r['job']['labels']['resource_gradient']['actual_spec']['format'] for r in success)),'partials':audit,'manifests':manifests,'episodes':len(rows),'support':support_audit(rows),'complete_episode_games':len({r['collection'] for r in rows}),'note':'Partial records preserve the original full-game cohort metadata; no extra cohort or role weight. No completed-game or whole-game totals claim for censored trajectories.'};write(folder/'TERMINAL-SEAL.json',report);print(name,len(success),len(failure),len(rows),flush=True);return manifests

if __name__=='__main__':
 output=[]
 for name,directory,archive in [('round001',BASE/'round-001-readonly',True),('round002-exploration',BASE/'round-002-exploration-readonly',False)]:output+=run(name,directory,archive)
 if (BASE/'round-002-candidate-readonly/experiment.json').exists():output+=run('round002-candidate',BASE/'round-002-candidate-readonly',False)
 write(ROOT/('cumulative-manifests-terminal256.json' if (BASE/'round-002-candidate-readonly/experiment.json').exists() else 'cumulative-manifests.json'),output)
 print('SEALED',len(output),'manifests',flush=True)
