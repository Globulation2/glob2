"""Light read-only completed-prefix audit. Never imports frozen executable modules."""
from pathlib import Path
import json,hashlib,re,statistics,datetime,collections,os
O=Path(__file__).resolve().parent;R=O.parents[1];P=R/'coordination-redesign';C=P/'confirmation/final-cohort';S=C/'serial'
def read(p):return json.loads(Path(p).read_text())
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def proc(pid):
 q=Path('/proc')/str(pid)
 try:
  f=(q/'stat').read_text().split(') ',1)[1].split();args=[x.decode(errors='replace') for x in (q/'cmdline').read_bytes().split(b'\0') if x];status=(q/'status').read_text();mask=next(x.split(':',1)[1].strip() for x in status.splitlines() if x.startswith('Cpus_allowed_list:'));return {'pid':pid,'state':f[0],'parent_pid':int(f[1]),'starttime':f[19],'argv':args,'cpus_allowed_list':mask}
 except (OSError,IndexError,StopIteration):return None
plan=read(C/'plan.json');approval=read(C/'root-approved.json');resolved=read(plan['latest_resolve']);variants={v['role']:v for v in resolved['variants']};assert sha(plan['latest_resolve'])==approval['latest_resolve_sha256'];pairs=read(S/'pairs.json');progress=read(S/'progress.json');paths=list(S.glob('*/*/receipt.json'));rows=[];errors=[]
for q in paths:
 r=read(q);log=q.parent/'stdout.log';assert sha(log)==r['stdout_sha256'];text=log.read_text(errors='replace');assert r['fixture_sha256']==plan['fixtures'] and r['base_revision']==plan['base_revision'];v=variants[r['role']];assert r['binary_sha256']==v['sha256']==approval['binary_hashes'][r['role']]
 declared=next(x for x in plan['declared_commands']['serial'] if (x['map'],x['seed'],x['role'])==(r['map'],r['seed'],r['role']));assert r['command']==declared['command'] and r['allocation']==declared['allocation'] and r['compute_requested']==declared['compute'] and r['cwd']==declared['cwd'];assert r['env_overrides']['GLOB2_TEST_SEED']==str(r['seed']) and r['env_overrides']['GLOB2_ASSET_DIR']==r['cwd']
 if r['status']=='winner':
  markers=re.findall(r'^HISTORY_RESULT (.*)$',text,re.M);assert len(markers)==1;d=dict(re.findall(r'(\w+)=([^\s]+)',markers[0]));assert d==r['reported'];expected=(128,128,8,9) if r['map']=='Playground' else (256,256,11,12)
  assert tuple(int(d[k]) for k in ['width','height','teams','controllers'])==expected and int(d['compute_participants'])==32 and int(d['eligible_cpus'])==32 and d['gradient_backend']=='cpu';assert int(d['ticks'])==r['ticks'] and int(d['winner'])==r['winner'] and int(d['ended'])==1 and r['returncode']==0
  clocks=re.findall(r'^HISTORY_TIMING run_wall_seconds=([0-9.eE+-]+)$',text,re.M);assert len(clocks)==1 and float(clocks[0])==r['inner_seconds'];roster=re.findall(r'Player: (Nicowar[^\n]*) for team (\d+)',text);assert len(roster)==expected[2] and sorted(int(t) for _,t in roster)==list(range(expected[2]))
 assert r['finished_monotonic']>=r['started_monotonic'];rows.append(r)
lookup={(r['map'],r['seed'],r['role']):r for r in rows};verified=[]
for pair in pairs:
 b=lookup[pair['map'],pair['seed'],'baseline'];w=lookup[pair['map'],pair['seed'],'wake-only'];both=b['status']==w['status']=='winner';same=both and (b['ticks'],b['winner'])==(w['ticks'],w['winner'])
 if both and b['observed_checksum'] is not None and w['observed_checksum'] is not None:same=same and b['observed_checksum']==w['observed_checksum']
 assert pair['both_winners']==both and pair['endpoints_match']==(same if both else None);assert not both or same
 index=plan['seeds'].index(pair['seed']);expected=['baseline','wake-only'] if index%2==0 else ['wake-only','baseline'];ordered=sorted([b,w],key=lambda r:r['started_monotonic']);assert [r['role'] for r in ordered]==expected and ordered[0]['finished_monotonic']<=ordered[1]['started_monotonic'];verified.append(dict(pair,actual_first_role=ordered[0]['role']))
metrics=[]
for mapname in ['Playground','Oazis']:
 good=[p for p in verified if p['map']==mapname and p['eligible_winner_timing_pair']];record={'map':mapname,'complete_pairs':sum(p['map']==mapname for p in verified),'eligible_winner_pairs':len(good),'first_role_counts':dict(collections.Counter(p['actual_first_role'] for p in verified if p['map']==mapname)),'metrics':{}}
 for metric in ['e2e_seconds','inner_seconds','total_cpu_seconds','system_cpu_seconds','peak_rss_kib','voluntary_context_switches','involuntary_context_switches','inner_tps','cpu_per_tick']:
  eligible=[p for p in good if all(lookup[mapname,p['seed'],role].get(metric) is not None for role in ['baseline','wake-only'])]
  if not eligible:continue
  b=[lookup[mapname,p['seed'],'baseline'][metric] for p in eligible];w=[lookup[mapname,p['seed'],'wake-only'][metric] for p in eligible];record['metrics'][metric]={'paired_prefix_n':len(eligible),'baseline_mean':statistics.mean(b),'wake_mean':statistics.mean(w),'ratio_of_paired_prefix_means_change_percent':100*(sum(w)/sum(b)-1) if sum(b) else None}
 metrics.append(record)
live=[]
for q in Path('/proc').glob('[0-9]*/cmdline'):
 item=proc(int(q.parent.name))
 if item and item['state']!='Z' and item['argv'] and Path(item['argv'][0]).name=='glob2':
  env=(q.parent/'environ').read_bytes().split(b'\0');item['requested_game_seed']=next((x.split(b'=',1)[1].decode() for x in env if x.startswith(b'GLOB2_TEST_SEED=')),None);live.append(item)
claims=[]
for q in S.glob('*/*/active-claim.json'):
 claim=read(q);actual=proc(claim['pid']);claims.append({'path':str(q),'pid':claim['pid'],'claimed_starttime':claim['starttime'],'live_same_identity':bool(actual and actual['state']!='Z' and actual['starttime']==claim['starttime']),'role':claim['role'],'seed':claim['seed']})
stale=[]
for key,claim in progress.get('active',{}).items():
 actual=proc(int(key));stale.append({'pid':int(key),'claimed_starttime':claim['starttime'],'live_same_identity':bool(actual and actual['state']!='Z' and actual['starttime']==claim['starttime'])})
out={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'scope':'Read-only incomplete serialprefix; completedpairsonly forcomparisons; no bootstrap,adoption,stoporreorderingdecision.','retained_receipts_snapshot':len(rows),'reported_progress':progress['completed'],'receipt_status_counts':dict(collections.Counter(r['status'] for r in rows)),'completed_pairs_snapshot':len(verified),'verified_pairs':verified,'metrics':metrics,'actual_live_glob2_processes':live,'progress_active_identity_checks':stale,'live_claims_only':[c for c in claims if c['live_same_identity']],'historical_claim_files':len(claims),'outer_identity':proc(109088),'old_campaign_identity':proc(4072783),'errors':errors,'provenance':{'plan':str(C/'plan.json'),'plan_sha256':sha(C/'plan.json'),'launcher_sha256':sha(C/'launcher.py'),'analyzer_sha256':sha(C/'analyze.py'),'serial_claim':read(S/'claim.json')},'limits':['Progressactive map canretainjust-finishedPID; actual/proc PID+starttime andactiveclaim required.','Prefixisdeterministicseedorder andmostly/onlyPG; cannotrepresentOA,all32seedsorconcurrentwaves.','No confidenceinterval/sequentialsignificancetesting here. Effects descriptiveonly; repeatedlooksnotadoption/stopcriteria.','Matchingwinner/ticks/observedendpoint isnotper-ticksimulationproof; latest40parityseparate.','WholeprocessCPU/sys/RSS/contextdistinctfrominnergamewall/TPS.','Snapshotfiles mayadvancebetweenreads; countsareexplicitlyasread,notatomicglobalstatus.']};(O/'latest.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({'utc':out['utc'],'receipts':len(rows),'pairs':len(verified),'metrics':metrics,'live':live,'progress_active':stale,'outer':out['outer_identity'],'campaign_state':out['old_campaign_identity']['state']},indent=2))
