#!/usr/bin/env python3
"""Root-gated PG448-only finish controller/export; no games, builds or other campaign stages."""
import argparse,csv,datetime,fcntl,hashlib,html,importlib.util,json,math,os,random,statistics,sys,time,uuid
from pathlib import Path
if not __debug__:raise SystemExit('Use Python without -O')
F=Path(__file__).resolve().parents[1];C=F/'campaign';O=Path(__file__).resolve().parent
FIELDS=('e2e_seconds','inner_seconds','total_cpu_seconds','cpu_per_tick','inner_tps','e2e_tps','peak_rss_kib','user_cpu_seconds','system_cpu_seconds')
def read(p):return json.loads(Path(p).read_text())
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,x):
 p=Path(p);tmp=p.with_name(p.name+'.tmp-'+str(os.getpid()));tmp.write_text(json.dumps(x,indent=2)+'\n');tmp.replace(p)
def identity(pid):
 try:s=(Path('/proc')/str(pid)/'stat').read_text().split(') ',1)[1].split();return s[0],s[19]
 except(OSError,IndexError):return None
def retired(x):
 v=identity(x['pid']);return not v or v[0]=='Z'or v[1]!=str(x['starttime'])
def census():
 busy=[];scripts={(C/n).resolve()for n in ('controller.py','runner.py','analyze.py')}|{(F/'export-when-complete.py').resolve(),(F/'publication-plan/postcampaign-controller.py').resolve()}
 for p in Path('/proc').glob('[0-9]*'):
  if p.name==str(os.getpid()):continue
  try:state=identity(int(p.name));comm=(p/'comm').read_text().strip();args=[os.fsdecode(x)for x in(p/'cmdline').read_bytes().split(b'\0')if x]
  except OSError:continue
  if not state or state[0]=='Z':continue
  if comm.startswith(('glob2','cc1','gcc','g++','clang','lto1','c++')):busy.append(p.name)
  elif args and Path(args[0]).name.startswith('python')and len(args)>1:
   q=Path(args[1]);q=q if q.is_absolute()else Path(os.readlink(p/'cwd'))/q
   if q.resolve()in scripts:busy.append(p.name)
 return busy

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--gate',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--wait',action='store_true');a=ap.parse_args();g=read(a.gate);lock=(O/'analysis.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 assert g['approved']is True and g['script_sha256']==sha(Path(__file__))and g['conditional_stop_proof_derivation_authorized']is True
 required=[C/'plan.json',C/'resolved-variants.json',C/'runner.py',F/'independent-audit/audit-whole-games.py',F/'matched-baseline-failures-review.json',F/'weighted-preparation-bare-diagnostic/summary.json',O/'watch.py',O/'independent-review.json',O/'root-approved.json',O/'sentinel-installed.json',C/'serial/Oazis']
 assert {str(p.resolve())for p in required}<=set(g['static_bindings'])
 def static_check():
  for name,h in g['static_bindings'].items():assert sha(name)==h,name
 static_check();proofpath=Path(g['stop_proof_path']);assert proofpath==O/'complete.json'and g['stop_proof_required_state']=='stopped_after_complete_playground'
 guard_approval=read(O/'root-approved.json');guard_review=read(O/'independent-review.json');assert guard_approval['approved']and guard_review['approved']and guard_approval['source_sha256']==guard_review['source_sha256']==sha(O/'watch.py')and guard_approval['review_sha256']==sha(O/'independent-review.json')
 while True:
  static_check();proof=None
  try:
   if proofpath.exists():proof=read(proofpath)
  except(json.JSONDecodeError,FileNotFoundError):pass
  if proof and proof.get('state')==g['stop_proof_required_state']and not census()and all(retired(x)for x in g['additional_retired_processes']):break
  if proof and ('failed'in proof.get('state','')or proof.get('error')):raise RuntimeError('Boundary guard failed; requires root review')
  if not a.wait:raise RuntimeError('PG stop proof/retirement/clean census not yet ready')
  save(O/'analysis-progress.json',{'state':'passive_wait_no_heavy_work','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'native_or_controller_busy':census()});time.sleep(30)
 # Proof schema is bound to the independently reviewed boundary guard. Never
 # manufacture fullserial896/campaign1792 completion from this PG-only subset.
 assert proof['runs']==448 and proof['no_oazis_or_waves_started']is True and proof['native_games_interrupted']is False and proof['unexpected_native_abort']is False and proof['signals_sent']is False
 sentinel=Path(proof['scope_sentinel']['path']);assert sentinel==(C/'serial/Oazis')and sentinel.is_file()and not sentinel.is_symlink()and not sentinel.is_dir()and sha(sentinel)==proof['scope_sentinel']['sha256']
 assert proof['retired_processes']and all(retired(x)for x in proof['retired_processes'])
 g_proof_sha=sha(proofpath);assert read(proofpath)==proof
 D=C/'serial';plan=read(C/'plan.json');variants={v['role']:v for v in read(C/'resolved-variants.json')['variants']};roles=plan['roles'];assert len(roles)==len(set(roles))==14 and set(roles)==set(variants)
 assert len(plan['seeds'])==len(set(plan['seeds']))==32 
 assert proof['plan_sha256']==sha(C/'plan.json')and proof['runner_sha256']==sha(C/'runner.py')
 assert not list((D/'Oazis').glob('*/receipt.json'))and not list((C/'waves').glob('*/*/receipt.json')),'Unexpected later campaign work exists'
 if a.output.exists()and(a.output/'receipt.json').exists():
  for rel,h in proof['receipt_hashes'].items():assert sha(D/rel)==h
  for rel,h in proof['wave_hashes'].items():assert sha(D/rel)==h
  old=read(a.output/'receipt.json');assert old['state']=='playground448_exported_stopped_awaiting_user'and old['script_sha256']==sha(Path(__file__))and old['gate_sha256']==sha(a.gate)and old['stop_proof_sha256']==sha(proofpath)
  for name,h in old['outputs'].items():assert sha(a.output/name)==h
  print('Verified completed PG-only output; awaiting user decision');return
 # Only now load compact corpus and independent parser, after all games retired.
 spec=importlib.util.spec_from_file_location('independent_parser',F/'independent-audit/audit-whole-games.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 paths=list((D/'Playground').glob('*/receipt.json'));assert len(paths)==448 and set(proof['receipt_hashes'])=={p.relative_to(D).as_posix()for p in paths}
 rows=[];lookup={};declared={(x['seed'],x['role']):x for x in plan['declared_commands']['serial']if x['map']=='Playground'};expected={(s,r)for s in plan['seeds']for r in roles}
 for path in paths:
  assert sha(path)==proof['receipt_hashes'][path.relative_to(D).as_posix()];r=read(path);key=r['seed'],r['role'];assert key in expected and key not in lookup and r['map']=='Playground'
  v=variants[r['role']];d=declared[key];assert r['binary_sha256']==v['sha256']and r['cwd']==v['cwd']and r['base_revision']==plan['base_revision']and r['fixture_sha256']==plan['fixtures']
  assert r['seed_index']==plan['seeds'].index(r['seed'])and r['command']==d['command']and r['allocation']==d['allocation']and r['compute_requested']==d['compute']and len(r['allocation'])==32
  assert r['command_sha256']==hashlib.sha256(json.dumps(r['command'],separators=(',',':')).encode()).hexdigest()
  env=dict(v.get('env',{}));env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(path.parent/'profile'),GLOB2_USER_DATA_DIR=str(path.parent/'profile'),GLOB2_TEST_SEED=str(r['seed']),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd']);assert env==r['env_overrides']
  assert sha(path.parent/'stdout.log')==r['stdout_sha256'];assert math.isfinite(r['e2e_seconds'])and r['e2e_seconds']>0 and m.close(r['finished_monotonic']-r['started_monotonic'],r['e2e_seconds'])and r['cap_seconds']==3600
  assert(r['status']=='cap')==(r['e2e_seconds']>=3600)and r['status']in('winner','cap','no_winner')
  status,ticks,winner,inner,data=m.parse_raw((path.parent/'stdout.log').read_text(errors='replace'),'Playground',r['status']=='cap',r['returncode'],32,32)
  assert inner is None or(math.isfinite(inner)and inner>=0)
  assert(status,ticks,winner,data)==(r['status'],r['ticks'],r['winner'],r['reported'])and m.close(inner,r['inner_seconds'])
  assert all(math.isfinite(r[f])and r[f]>=0 for f in ('user_cpu_seconds','system_cpu_seconds','total_cpu_seconds','peak_rss_kib','voluntary_context_switches','involuntary_context_switches'))and m.close(r['total_cpu_seconds'],r['user_cpu_seconds']+r['system_cpu_seconds'])
  for f,value in [('cpu_per_tick',r['total_cpu_seconds']/ticks if ticks else None),('inner_tps',ticks/inner if ticks and inner else None),('e2e_tps',ticks/r['e2e_seconds']if ticks else None)]:assert m.close(r[f],value)
  assert r['observed_checksum']==(data.get('checksum')or data.get('finalChecksum'));lookup[key]=r;rows.append(r)
 assert set(lookup)==expected
 assert sha(D/'pairs.json')==proof['pairs_sha256'];pairs_file=read(D/'pairs.json');assert len(pairs_file)==416 and {(x['seed'],x['candidate'])for x in pairs_file}=={(s,r)for s in plan['seeds']for r in roles if r!='baseline'}
 for x in pairs_file:
  b,w=lookup[x['seed'],'baseline'],lookup[x['seed'],x['candidate']];both=b['status']==w['status']=='winner';same=both and (b['ticks'],b['winner'])==(w['ticks'],w['winner'])
  assert x['map']=='Playground'and x['both_winners']==both and x['endpoints_match']==(same if both else None)and x['eligible_winner_timing_pair']==same
 orders=[x for x in plan['actual_order_inventory']['serial']if x['map']=='Playground'];assert len(orders)==32
 wavekeys=set();previous=None
 for number,entry in enumerate(orders):
  assert entry['wave']==number;shift=(number//2)%14;forward=roles[shift:]+roles[:shift];assert entry['roles']==(forward if number%2==0 else list(reversed(forward)))
  for role in entry['roles']:
   name='wave-'+str(number)+'-Playground-'+role+'.json';wavekeys.add(name);path=D/name;assert sha(path)==proof['wave_hashes'][name];w=read(path);r=lookup[plan['seeds'][number],role]
   assert(w['map'],w['wave'],w['role'],w['seed_indices'])==('Playground',number,role,[number]);assert w['started_monotonic']<=r['started_monotonic']<=r['finished_monotonic']<=w['finished_monotonic'];span=w['finished_monotonic']-w['started_monotonic'];assert span>0 and m.close(span,w['makespan_seconds'])and m.close(w['sum_game_seconds'],r['e2e_seconds'])
   assert w['winners']==int(r['status']=='winner')and w['caps']==int(r['status']=='cap')
   if previous is not None:assert previous<=w['started_monotonic']
   previous=w['finished_monotonic']
 assert set(proof['wave_hashes'])==wavekeys
 for role in roles:
  assert statistics.mean(x['roles'].index(role)for x in orders)==6.5
  for other in roles:
   if role!=other:assert sum(x['roles'].index(role)<x['roles'].index(other)for x in orders)==16
 def metric(pairs,field):
  selected=[(b,w)for b,w in pairs if b.get(field)is not None and w.get(field)is not None];n=len(selected)
  if not n:return {'n':0,'change_percent':None,'CI95':None,'adverse':0,'baseline_mean':None,'candidate_mean':None}
  b=[x[field]for x,y in selected];w=[y[field]for x,y in selected];before=math.fsum(b);after=math.fsum(w);rng=random.Random(605441);samples=[]
  for _ in range(10000):
   indices=rng.choices(range(n),k=n);den=math.fsum(b[i]for i in indices)
   if den:samples.append(100*(math.fsum(w[i]for i in indices)/den-1))
  samples.sort();return {'n':n,'change_percent':100*(after/before-1)if before else None,'CI95':[samples[int(.025*(len(samples)-1))],samples[int(.975*(len(samples)-1))]]if samples and n>1 else None,'undefined_draws':10000-len(samples),'adverse':sum((y<x if field.endswith('tps')else y>x)for x,y in zip(b,w)),'baseline_mean':before/n,'candidate_mean':after/n}
 summaries=[];csvsummary=[]
 for role in roles:
  rr=[lookup[s,role]for s in plan['seeds']];pairs=[];excluded=[]
  for seed in plan['seeds']:
   b,w=lookup[seed,'baseline'],lookup[seed,role]
   if b['status']==w['status']=='winner':
    assert(b['ticks'],b['winner'])==(w['ticks'],w['winner'])
    if b['observed_checksum']is not None and w['observed_checksum']is not None:assert b['observed_checksum']==w['observed_checksum']
    pairs.append((b,w))
   else:excluded.append({'seed':seed,'baseline':b['status'],'candidate':w['status']})
  values={f:metric(pairs,f)for f in FIELDS};wins=[r for r in rr if r['status']=='winner'];summary={'role':role,'winners':len(wins),'caps':sum(r['status']=='cap'for r in rr),'matching_pairs':len(pairs),'excluded':excluded,'metrics':values,'all_winner_means':{f:statistics.mean(r[f]for r in wins if r.get(f)is not None)if any(r.get(f)is not None for r in wins)else None for f in FIELDS}};summaries.append(summary)
  out={'role':role,'winners':len(wins),'caps':summary['caps'],'matching_pairs':len(pairs)}
  for f,v in values.items():
   for k in ('n','change_percent','adverse','baseline_mean','candidate_mean'):out[f+'_'+k]=v[k]
   out[f+'_CI95_low']=v['CI95'][0]if v['CI95']else None;out[f+'_CI95_high']=v['CI95'][1]if v['CI95']else None
  csvsummary.append(out)
 assert not census();static_check()
 # Dataset proof rechecked after the bootstrap: no changing corpus silently used.
 assert sha(proofpath)==g_proof_sha
 for path in paths:assert sha(path)==proof['receipt_hashes'][path.relative_to(D).as_posix()]
 for rel,h in proof['wave_hashes'].items():assert sha(D/rel)==h
 assert sha(D/'pairs.json')==proof['pairs_sha256']
 if a.output.exists():a.output.rename(a.output.with_name(a.output.name+'.partial-'+uuid.uuid4().hex))
 a.output.mkdir(parents=True);out=a.output
 limits='''Stopped by user after Playground32seeds x14designs =448serial/unrestricted-auto32 games. No Oazis, concurrent waves, host-throughput or56full-game-checksum continuation. Timing telemetry intentionally Off; empty checksum_files/observed_checksum null means winner/tick outcome agreement only, not448whole-game checksum proof.280boundedper-tick qualification contexts remain separate. Two baseline engine failures remain failures acrossall14roles: ResourceGrowth historical payload rejection(3events) and TurretTargeting overlapping publication entries-empty(1event);160otherselectedenginecases passedperrole. Original bare-steal suite timeout180s remains unexplained despite subsequent nonreproduction/requalification. Historical firststaticRR/strict-affinity failures are distinct from the current local-completionRR research arm, which passed bounded checks. OneLinuxhost, no all-platform/nativefeel proof; manyexploratory pairedcomparisons lackfamilywisecontrol. Primaryall32 retained. A regular administrative sentinel file blocks firstOazis output-directory admission beforePopen afterallPGreceipts/windows/pairs commit. No PGgame/parent pause or administrative signal was applied; rejected SIGSTOP drafts were never launched. All32seed results retained without sensitivity exclusions. CIs are10000pairedseed MonteCarlo draws, floorquantiles, fixedseed605441; conditionalmatchingwinners, caps separate.'''
 save(out/'dataset-manifest.json',{'state':'448_receipts_448_single_game_windows_416_pairs_verified','stop_proof_sha256':g_proof_sha,'gate_sha256':sha(a.gate),'static_bindings':g['static_bindings'],'receipt_hashes':proof['receipt_hashes'],'wave_hashes':proof['wave_hashes'],'pairs_sha256':proof['pairs_sha256'],'dataset_manifest_differences':[],'administrative_native_signals_sent':False,'scope_sentinel':proof['scope_sentinel']})
 audit={'state':'playground448_raw_audit_and_paired_analysis','runs':448,'map':'Playground','regime':'serial','stop_proof_sha256':sha(proofpath),'dataset_receipt_hashes':proof['receipt_hashes'],'wave_hashes':proof['wave_hashes'],'summary':summaries,'telemetry_off_endpoint_only_rows':sum(not r['checksum_files']and r['observed_checksum']is None for r in rows),'limits':limits};save(out/'raw-audit.json',audit)
 for name,data in [('every-game.csv',[{k:r.get(k)for k in ('role','seed','status','ticks','winner',*FIELDS,'binary_sha256','base_revision')}for r in rows]),('comparisons.csv',csvsummary)]:
  with(out/name).open('w',newline='')as f:w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 for field,label in [('e2e_seconds','completion-time'),('total_cpu_seconds','total-cpu')]:
  fig,ax=plt.subplots(figsize=(11,8),constrained_layout=True);ax.axvline(0,color='black',lw=.8)
  for y,s in enumerate(summaries):
   series=[(s['metrics'][field],0,'#2878a8','all32primary')]
   for v,offset,color,lab in series:
    if v['change_percent']is None:continue
    if v['CI95']:ax.hlines(y+offset,*v['CI95'],color=color)
    ax.plot(v['change_percent'],y+offset,'o',color=color,ms=4,label=lab if y==0 else None)
  ax.set_yticks(range(14),roles,fontsize=8);ax.invert_yaxis();ax.set_xlabel('Change vs baseline(%); paired95% CI; negative lower');ax.set_title('Playground serial auto32: '+label+'; matchingwinners');ax.legend();ax.grid(axis='x',alpha=.2)
  for ext in ('svg','png'):fig.savefig(out/(label+'.'+ext),dpi=160)
  plt.close(fig)
 def fmt(x):return '—'if x is None else f'{x:.2f}'
 def delta(v):return fmt(v['change_percent'])+('% ['+fmt(v['CI95'][0])+', '+fmt(v['CI95'][1])+']'if v['CI95']else'%')
 table=''
 for s in summaries:
  table+='<tr>'+''.join('<td>'+html.escape(str(v))+'</td>'for v in (s['role'],s['winners'],s['caps'],s['matching_pairs'],fmt(s['all_winner_means']['e2e_seconds']),delta(s['metrics']['e2e_seconds']),fmt(s['all_winner_means']['total_cpu_seconds']),delta(s['metrics']['total_cpu_seconds']),s['metrics']['e2e_seconds']['adverse'],s['metrics']['total_cpu_seconds']['adverse']))+'</tr>'
 body='<!doctype html><meta charset="utf-8"><title>Playground448 stopped comparison</title><style>body{font:15px/1.5 system-ui;margin:25px}td,th{border:1px solid #aaa;padding:6px}table{border-collapse:collapse}img{max-width:100%}</style><h1>Playground448: stopped, awaiting user decision</h1><p>'+html.escape(limits)+'</p><p><a href="every-game.csv">448rawgames</a> · <a href="comparisons.csv">Allmetrics/pairedmeans/CIs</a> · <a href="raw-audit.json">Rawaudit</a></p><table><tr>'+''.join('<th>'+x+'</th>'for x in ('Role','Winners','Caps','Pairs','AllwinnerE2Eseconds','PairedE2Echange','AllwinnerCPU-seconds','PairedCPUchange','AdverseE2Eseeds','AdverseCPUseeds'))+'</tr>'+table+'</table>'
 for name in ('completion-time','total-cpu'):body+='<p><a href="'+name+'.svg">SVGfigure</a></p><img src="'+name+'.png" alt="'+name+' pairedCI">'
 (out/'index.html').write_text(body+'\n');files=[p for p in out.iterdir()if p.is_file()];save(out/'receipt.json',{'state':'playground448_exported_stopped_awaiting_user','script_sha256':sha(Path(__file__)),'gate_sha256':sha(a.gate),'stop_proof_sha256':sha(proofpath),'runs':448,'outputs':{p.name:sha(p)for p in files},'no_further_automation':True,'matplotlib':matplotlib.__version__})
 save(O/'analysis-progress.json',{'state':'report_ready_awaiting_user_decision','runs':448,'output':str(out),'receipt_sha256':sha(out/'receipt.json'),'no_further_automation':True})
 print('Playground448 exported; stop here and await user decision')
if __name__=='__main__':
 try:main()
 except BaseException as e:save(O/'analysis-failure.json',{'state':'failed_preserved','error':repr(e)});raise
