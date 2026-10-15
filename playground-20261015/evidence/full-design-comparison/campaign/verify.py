"""Shared fail-closed artifact validators, no workload launches."""
from pathlib import Path
import json,hashlib,importlib.util,re,xml.etree.ElementTree as ET
read=lambda p:json.loads(Path(p).read_text())
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
_validated={}
def bindings(approval):
 for item in approval['bound_files']:
  q=Path(item['path']);st=q.stat();token=(st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns,item['sha256'])
  if _validated.get(str(q))!=token:assert sha(q)==item['sha256'],str(q);_validated[str(q)]=token
def helpers(plan):
 assert sha(plan['shared_launcher'])==plan['shared_launcher_sha256'];s=importlib.util.spec_from_file_location('canonicalhelpers',plan['shared_launcher']);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def receipt(plan,v,regime,target):
 r=read(target/'receipt.json');d=next(x for x in plan['declared_commands'][regime]if(x['map'],x['seed'],x['role'])==(r['map'],r['seed'],r['role']))
 assert r['command']==d['command']and r['allocation']==d['allocation']and r['compute_requested']==d['compute']and r['cwd']==v['cwd']and r['binary_sha256']==v['sha256']and r['fixture_sha256']==plan['fixtures']and r['base_revision']==plan['base_revision']
 assert r['command_sha256']==hashlib.sha256(json.dumps(r['command'],separators=(',',':')).encode()).hexdigest()
 e=dict(v.get('env',{}));e.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(target/'profile'),GLOB2_USER_DATA_DIR=str(target/'profile'),GLOB2_TEST_SEED=str(r['seed']),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd']);assert r['env_overrides']==e
 assert sha(target/'stdout.log')==r['stdout_sha256'];m=helpers(plan);n=m.parse_log((target/'stdout.log').read_text(errors='replace'),r['map'],32 if regime=='serial'else 6,len(r['allocation']),r['returncode'],r['status']=='cap')
 for k in ['status','ticks','winner','reported','inner_seconds','observed_checksum']:assert n[k]==r[k]
 assert r['cap_seconds']==plan['cap_seconds']
 for field,denom in [('cpu_per_tick',r['ticks']),('e2e_tps',r['e2e_seconds']),('inner_tps',r['inner_seconds'])]:
  expected=(r['total_cpu_seconds']/denom if field=='cpu_per_tick'else r['ticks']/denom)if denom and r['ticks']else None
  assert expected==r[field]or(expected is not None and r[field]is not None and abs(expected-r[field])<1e-8)
 assert r['status']=='cap' or r['e2e_seconds']<plan['cap_seconds'];assert m.checksum_evidence(target)==r['checksum_files']
 assert abs(r['finished_monotonic']-r['started_monotonic']-r['e2e_seconds'])<1e-6;assert abs(r['total_cpu_seconds']-r['user_cpu_seconds']-r['system_cpu_seconds'])<1e-8
 return r
def endpoints(b,w):
 same=(b['ticks'],b['winner'])==(w['ticks'],w['winner'])
 if b['checksum_files']and w['checksum_files']:
  sides=[[ (x.get('rows'),x.get('first'),x.get('last'))for x in r['checksum_files']if x['complete']]for r in [b,w]];same=same and bool(sides[0])and sides[0]==sides[1]
 if b['observed_checksum']is not None and w['observed_checksum']is not None:same=same and b['observed_checksum']==w['observed_checksum']
 return same
def wave(w,rows,indices):
 assert w['seed_indices']==indices and len(rows)==len(indices)and w['makespan_seconds']>0
 assert abs(w['finished_monotonic']-w['started_monotonic']-w['makespan_seconds'])<1e-6
 assert all(w['started_monotonic']<=r['started_monotonic']<=r['finished_monotonic']<=w['finished_monotonic']for r in rows)
 assert w['complete_tick_coverage']==all(r['ticks']is not None for r in rows)
 assert w['winners']==sum(r['status']=='winner'for r in rows)and w['caps']==sum(r['status']=='cap'for r in rows)
 assert abs(w['sum_game_seconds']-sum(r['e2e_seconds']for r in rows))<1e-6
 assert abs(w['games_per_hour']-w['winners']*3600/w['makespan_seconds'])<1e-6
 assert abs(w['observed_ticks_per_second']-sum(r['ticks']or 0 for r in rows)/w['makespan_seconds'])<1e-6

# Experimental qualification may proceed only with this exact matched baseline
# exception. Raw engine failures and rc1 are preserved, never labelled a pass.
KNOWN_BASELINE_STATE='qualified_with_known_baseline_failures'
def baseline_failure_review(reference,roles):
 assert sha(reference['path'])==reference['sha256']
 review=read(reference['path'])
 assert review['state']=='matched_failures_independently_confirmed_not_test_pass'
 entries={x['role']:x for x in review['roles']}
 assert len(entries)==14 and set(entries)==set(roles)
 assert all(x['cases']==162 and x['passed']==160 and x['matched_failed_cases']==2 and x['error_or_skip_cases']==0 for x in entries.values())
 expected=review['baseline_failures']
 assert [(x['suite'],x['name'],len(x['events']))for x in expected]==[
  ('ResourceGrowth','historical growth payloads are rejected after header preflight',3),
  ('TurretTargeting','overlapping publication preserves other sources through upgrade and incarnation changes',1)]
 return review,entries

def focused_with_baseline_exception(record,variant,review,entries):
 q=Path(record['command_receipt']);command=read(q)
 assert sha(q.parent/'stdout.log')==command['stdout_sha256']
 kind=record['kind'];assert kind in ['unit','engine','unit_gradient']
 binary=variant['engine'] if kind=='engine' else variant['unit']
 assert sha(binary)==record['tested_binary_sha256']
 xml=q.parent/'junit.xml';assert sha(xml)==record['junit_sha256']
 cases=list(ET.parse(xml).getroot().iter('testcase'));assert cases and len(cases)==record['cases']
 assert not any(c.find('error')is not None or c.find('skipped')is not None for c in cases)
 failed=[]
 for c in cases:
  events=c.findall('failure')
  if events:
   failed.append({'suite':c.get('classname'),'name':c.get('name'),'events':[{'type':f.get('type'),'message':f.get('message'),'body':re.sub(r'(?m)^  logged: name := 0x[0-9a-fA-F]+\s*$','  logged: name := <volatile-address>',f.text or '')}for f in events]})
 if kind=='engine':
  assert command['returncode']==1 and len(cases)==162 and len(failed)==2
  assert sha(xml)==entries[variant['role']]['junit_sha256']
  assert failed==review['baseline_failures']
  assert record['failures']==[{'class':x['suite'],'name':x['name']}for x in failed]
 else:
  assert command['returncode']==0 and not failed and not record['failures']
  if kind=='unit_gradient':
   ref=record['fresh_supplemental_metadata'];assert sha(ref['path'])==ref['sha256'];metadata=read(ref['path'])
   assert metadata['state']=='passed'and metadata['cases']==len(cases)==len(set(metadata['names']))==22
   assert metadata['junit_sha256']==record['junit_sha256']and record['names']==metadata['names']==[c.get('name')for c in cases]
   assert record['junit_classname']=='src/map/gradient/GradientPipelineHarness.cpp'and all(c.get('classname')==record['junit_classname']for c in cases)
 return [q,xml,q.parent/'stdout.log',Path(binary)]
