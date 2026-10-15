#!/usr/bin/env python3
"""Explicit gated multi-role whole-game campaign; no builds. Resume completed role waves only."""
from pathlib import Path
import argparse,concurrent.futures,datetime,fcntl,hashlib,importlib.util,json,os,signal,subprocess,sys,threading,time
O=Path(__file__).resolve().parent;P=O.parents[1];sys.path.insert(0,str(P));sys.path.insert(0,str(O))
from process_cleanup import process_identity,kill_owned_tree
import verify
MASKS=[[0,1,2,16,17,18],[3,4,5,19,20,21],[6,7,8,22,23,24],[9,10,11,25,26,27]]
def sha(q):
 with Path(q).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(q):return json.loads(Path(q).read_text())
def save(q,x):
 q=Path(q);q.parent.mkdir(parents=True,exist_ok=True);t=q.with_name(q.name+'.tmp-'+str(threading.get_ident()));t.write_text(json.dumps(x,indent=2)+'\n');t.replace(q)
def order_for(roles,number):
 k=len(roles);shift=(number//2)%k;v=roles[shift:]+roles[:shift];return v if number%2==0 else list(reversed(v))
def main():
 ap=argparse.ArgumentParser();ap.add_argument('regime',choices=['serial','waves']);ap.add_argument('--execute',action='store_true');ap.add_argument('--resume',action='store_true');a=ap.parse_args();assert a.execute
 lock=(O/'campaign.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 plan=read(O/'plan.json');approval=read(O/'root-approved.json');review=read(O/'independent-review.json')
 assert approval['approved'] and review['approved'] and approval['source_sha256']==review['source_sha256']==sha(__file__) and approval['plan_sha256']==sha(O/'plan.json') and review['template_sha256']==sha(O/'plan-template.json')
 resolved=read(plan['resolved_variants']);assert sha(plan['resolved_variants'])==approval['resolved_sha256']
 assert resolved['state']==verify.KNOWN_BASELINE_STATE and resolved['baseline_failure_review']==approval['baseline_failure_review']
 variants=resolved['variants'];roles=[v['role'] for v in variants];assert len(roles)==len(set(roles)) and 'baseline'in roles
 assert len(plan['seeds'])==len(set(plan['seeds']))==32 and plan['base_revision']=='054804590ee1dd109f09642fc05654c324f08344'
 original=Path(plan['shared_launcher']);assert sha(original)==plan['shared_launcher_sha256'];spec=importlib.util.spec_from_file_location('fixed_cohort_helpers',original);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 global parse_log,checksum_evidence,command;parse_log=module.parse_log;checksum_evidence=module.checksum_evidence;command=module.command
 baseline_review,baseline_entries=verify.baseline_failure_review(approval['baseline_failure_review'],roles)
 for v in variants:
  assert v['base_revision']==plan['base_revision'] and sha(v['binary'])==v['sha256']==approval['binary_hashes'][v['role']]
  q=v['qualification'];assert sha(q['path'])==q['sha256'] and read(q['path'])[q['state_key']]==q['expected_state'] and q['expected_state']==verify.KNOWN_BASELINE_STATE
  qualified=read(q['path']);assert qualified['baseline_failure_review']==approval['baseline_failure_review']and qualified['research_qualification_only']is True
  for fixture,h in plan['fixtures'].items():assert sha(Path(v['cwd'])/'maps'/Path(fixture).name)==h
 active={};mu=threading.Lock();stop=threading.Event()
 def interrupt(*_):stop.set()
 signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
 def gate(allow_children=False):
  assert not stop.is_set(),'Administrativestop; retaincleanhold'
  verify.bindings(approval)
  assert sha(approval['expanded_complete'])==approval['expanded_complete_sha256'] and read(approval['expanded_complete'])['runs']==608
  for f in Path('/proc').glob('[0-9]*/cmdline'):
   try:args=[x.decode(errors='replace')for x in f.read_bytes().split(b'\0')if x];ident=process_identity(int(f.parent.name));pid=int(f.parent.name)
   except OSError:continue
   if not args or not ident or ident[0]=='Z' or pid==os.getpid():continue
   name=Path(args[0]).name
   owned=allow_children and pid in active and ident[1]==active[pid]['starttime']
   assert owned or not name.startswith(('glob2','cc1','clang','g++','gcc','c++')),('Competingnative/compiler',pid,args)
   if name.startswith('python') and len(args)>1:assert Path(args[1]).name not in ['scons','timing-runner.py','launcher.py','paired-controller.py','parallel-parity.py'],('Competingtimingcontroller',pid,args)
  for f,h in plan['fixtures'].items():assert sha(f)==h
 gate();out=O/a.regime
 if out.exists():assert a.resume and read(out/'claim.json')['plan_sha256']==sha(O/'plan.json')
 else:out.mkdir()
 save(out/'claim.json',{'pid':os.getpid(),'starttime':process_identity(os.getpid())[1],'source_sha256':sha(__file__),'plan_sha256':sha(O/'plan.json')})
 start=time.monotonic();receipts=[];waves=[];pairs=[]
 def run(v,mapname,seed,index,mask,budget,wave):
  if stop.is_set():raise RuntimeError('No new games after sibling failure')
  target=out/mapname/(str(seed)+'-'+v['role']);assert not target.exists();target.mkdir(parents=True)
  assert sha(v['binary'])==v['sha256'];assert sha(Path(v['cwd'])/'maps'/(mapname+'.map'))==next(h for f,h in plan['fixtures'].items() if Path(f).name==mapname+'.map');profile=target/'profile';profile.mkdir();env={k:value for k,value in os.environ.items() if not k.startswith('GLOB2_')};env.update(v.get('env',{}));env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(profile),GLOB2_USER_DATA_DIR=str(profile),GLOB2_TEST_SEED=str(seed),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd'])
  cmd=command(v,mapname,budget,mask);declared=next(x for x in plan['declared_commands'][a.regime] if (x['map'],x['seed'],x['role'])==(mapname,seed,v['role']));assert cmd==declared['command'] and mask==declared['allocation'] and str(profile)==declared['profile'];command_hash=hashlib.sha256(json.dumps(cmd,separators=(',',':')).encode()).hexdigest();child=None;capped=False;t=time.monotonic();done=False
  try:
   with(target/'stdout.log').open('w') as log:
    with mu:
     if stop.is_set():raise RuntimeError('No new games after sibling failure')
     gate(allow_children=True)
     child=subprocess.Popen(cmd,cwd=v['cwd'],env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
     claim={'pid':child.pid,'starttime':process_identity(child.pid)[1],'command':cmd,'command_sha256':command_hash,'seed':seed,'role':v['role']};save(target/'active-claim.json',claim)
     active[child.pid]=claim
    while True:
     pid,status,usage=os.wait4(child.pid,os.WNOHANG)
     if pid:child.returncode=os.waitstatus_to_exitcode(status);done=True;break
     if stop.is_set():raise RuntimeError('Other wave task failed')
     if time.monotonic()-t>=plan['cap_seconds']:
      capped=True;os.killpg(child.pid,signal.SIGTERM);grace=time.monotonic()+10
      while time.monotonic()<grace:
       pid,status,usage=os.wait4(child.pid,os.WNOHANG)
       if pid:child.returncode=os.waitstatus_to_exitcode(status);done=True;break
       time.sleep(.1)
      if not done:kill_owned_tree(child.pid,target/'cap-cleanup.json');_,status,usage=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status);done=True
      break
     time.sleep(.05)
   elapsed=time.monotonic()-t;capped=capped or elapsed>=plan['cap_seconds'];n=parse_log((target/'stdout.log').read_text(errors='replace'),mapname,32 if budget=='auto' else int(budget),len(mask),child.returncode,capped);cpu=usage.ru_utime+usage.ru_stime
   checks=checksum_evidence(target);assert sha(v['binary'])==v['sha256']
   r=dict(n,seed=seed,seed_index=index,map=mapname,role=v['role'],wave=wave,allocation=mask,compute_requested=budget,command=cmd,cwd=v['cwd'],env_overrides={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','GLOB2_USER_DIR','GLOB2_USER_DATA_DIR','GLOB2_TEST_SEED','GLOB2_TEST_MAX_TICKS','GLOB2_ASSET_DIR']+list(v.get('env',{}))},command_sha256=command_hash,fixture_sha256=plan['fixtures'],binary_sha256=v['sha256'],base_revision=plan['base_revision'],returncode=child.returncode,cap_seconds=plan['cap_seconds'],started_monotonic=t,finished_monotonic=t+elapsed,e2e_seconds=elapsed,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,total_cpu_seconds=cpu,peak_rss_kib=usage.ru_maxrss,voluntary_context_switches=usage.ru_nvcsw,involuntary_context_switches=usage.ru_nivcsw,checksum_files=checks,stdout_sha256=sha(target/'stdout.log'))
   r['cpu_per_tick']=cpu/n['ticks'] if n['ticks'] else None;r['e2e_tps']=n['ticks']/elapsed if n['ticks'] else None;r['inner_tps']=n['ticks']/n['inner_seconds'] if n['ticks'] and n['inner_seconds'] else None
   save(target/'receipt.json',r);assert n['status']!='failed',target
   with mu:receipts.append(r);save(out/'progress.json',{'completed':len(receipts),'total':64*len(variants),'active':active,'elapsed_seconds':time.monotonic()-start})
   return r
  except BaseException:stop.set();raise
  finally:
   if child is not None:
    if not done:
     kill_owned_tree(child.pid,target/'cleanup.json');_,abort_status,abort_usage=os.wait4(child.pid,0)
     save(target/'aborted.json',{'status':'infrastructure_aborted','command':cmd,'returncode':os.waitstatus_to_exitcode(abort_status),'wall_seconds':time.monotonic()-t,'user_cpu_seconds':abort_usage.ru_utime,'system_cpu_seconds':abort_usage.ru_stime,'peak_rss_kib':abort_usage.ru_maxrss,'warning':'Not a winner or planned cap; partial log retained.'})
    with mu:active.pop(child.pid,None)
 def wave(v,mapname,indices,number):
  gate();begin=time.monotonic()
  if a.regime=='serial':rows=[run(v,mapname,plan['seeds'][i],i,list(range(32)),'auto',number) for i in indices]
  else:
   with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(lambda pair:run(v,mapname,plan['seeds'][pair[1]],pair[1],MASKS[pair[0]],6,number),enumerate(indices)))
  end=time.monotonic();span=end-begin;wins=sum(r['status']=='winner' for r in rows);ticks=sum(r['ticks'] or 0 for r in rows)
  w={'map':mapname,'role':v['role'],'wave':number,'seed_indices':indices,'started_monotonic':begin,'finished_monotonic':end,'makespan_seconds':span,'winners':wins,'caps':sum(r['status']=='cap' for r in rows),'games_per_hour':wins*3600/span,'observed_ticks_per_second':ticks/span,'complete_tick_coverage':all(r['ticks'] is not None for r in rows),'sum_game_seconds':sum(r['e2e_seconds'] for r in rows),'scope':'Actual homogeneous wave makespan including launches/waits, not sum of parallel game runtimes.'};waves.append(w);save(out/('wave-'+str(number)+'-'+mapname+'-'+v['role']+'.json'),w);save(out/('wave-'+str(number)+'-'+mapname+'-'+v['role']+'.binding.json'),{'sha256':sha(out/('wave-'+str(number)+'-'+mapname+'-'+v['role']+'.json'))})
  return rows
 for map_index,mapname in enumerate(['Playground','Oazis']):
  width=1 if a.regime=='serial' else 4
  for offset in range(0,32,width):
   indices=list(range(offset,offset+width));number=offset//width;order=order_for(variants,number+map_index*(32//width));results={}
   for v in order:
    q=out/('wave-'+str(number)+'-'+mapname+'-'+v['role']+'.json')
    if q.exists():
     w=read(q);binding=read(q.with_suffix('.binding.json'));assert binding['sha256']==sha(q);rr=[verify.receipt(plan,v,a.regime,out/mapname/(str(plan['seeds'][i])+'-'+v['role']))for i in indices];verify.wave(w,rr,indices);receipts.extend(rr)
     waves.append(w);results[v['role']]=rr
    else:
     # Preserve an incomplete attempted role wave, never silently reuse partial timing.
     for i in indices:
      t=out/mapname/(str(plan['seeds'][i])+'-'+v['role'])
      if t.exists():
       dest=out/'abandoned-attempts'/(t.name+'-'+str(time.time_ns()));dest.parent.mkdir(exist_ok=True);t.rename(dest)
     results[v['role']]=wave(v,mapname,indices,number)
   for i in indices:
    b=next(r for r in results['baseline']if r['seed_index']==i)
    for role in roles:
     if role=='baseline':continue
     w=next(r for r in results[role]if r['seed_index']==i);valid=b['status']==w['status']=='winner';same=valid and verify.endpoints(b,w)
     if valid and b['observed_checksum']is not None and w['observed_checksum']is not None:same=same and b['observed_checksum']==w['observed_checksum']
     pairs.append({'map':mapname,'seed':plan['seeds'][i],'seed_index':i,'reference':'baseline','candidate':role,'both_winners':valid,'endpoints_match':same if valid else None,'eligible_winner_timing_pair':same,'statuses':[b['status'],w['status']]})
   save(out/'pairs.json',pairs)
   bad=[x for x in pairs if x['both_winners']and not x['endpoints_match']]
   if bad:save(out/'trajectory-mismatch.json',{'state':'failed_endpoint_equivalence','pairs':bad});raise RuntimeError('Endpointmismatch atcleanmultiroleboundary; holdforroot')
 span=time.monotonic()-start;save(out/'complete.json',{'state':'complete','regime':a.regime,'runs':len(receipts),'plan_sha256':sha(O/'plan.json'),'launcher_sha256':sha(__file__),'wave_hashes':{str(q.relative_to(out)):sha(q)for q in out.glob('wave-*.json')if not q.name.endswith('.binding.json')},'receipt_hashes':{str(q.relative_to(out)):sha(q)for q in out.glob('*/*/receipt.json')},'waves':waves,'pairs':pairs,'session_elapsed_seconds':span,'scope':'Resumedcampaignmaycontainidlebuild/recoverygaps; comparehomogeneousactualwavewindows. Capscensoredseparately; noautoacceptance.'})
if __name__=='__main__':main()
