#!/usr/bin/env python3
"""Prepared only: opt-in latest B/W full-game winner/throughput followup. No builds."""
from pathlib import Path
import argparse,concurrent.futures,datetime,fcntl,hashlib,json,mmap,os,re,signal,struct,subprocess,sys,threading,time
O=Path(__file__).resolve().parent;P=O.parents[1];sys.path.insert(0,str(P))
from process_cleanup import process_identity,kill_owned_tree
MASKS=[[0,1,2,16,17,18],[3,4,5,19,20,21],[6,7,8,22,23,24],[9,10,11,25,26,27]]
def sha(q):
 with Path(q).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(q):return json.loads(Path(q).read_text())
def save(q,x):
 q=Path(q);q.parent.mkdir(parents=True,exist_ok=True);t=q.with_name(q.name+'.tmp-'+str(threading.get_ident()));t.write_text(json.dumps(x,indent=2)+'\n');t.replace(q)
def parse_log(text,mapname,participants,eligible,returncode,capped):
 expected=(128,128,8,9) if mapname=='Playground' else (256,256,11,12)
 matches=re.findall(r'^HISTORY_RESULT (.*)$',text,re.M)
 if capped:
  v=dict(re.findall(r'(\w+)=([^\s]+)',matches[-1])) if matches else {}
  def number(key,kind=int):
   try:return kind(v[key])
   except (KeyError,ValueError):return None
  clocks=re.findall(r'^HISTORY_TIMING run_wall_seconds=([0-9.eE+-]+)$',text,re.M)
  try:inner=float(clocks[-1]) if clocks else None
  except ValueError:inner=None
  return {'status':'cap','ticks':number('ticks'),'winner':None,'reported':v,'roster_valid':None,'inner_seconds':inner,'observed_checksum':v.get('checksum') or v.get('finalChecksum'),'parse_warning':'Censored: partial/truncated summary permitted; dimensions/roster/terminal winner not asserted.'}
 assert len(matches)<=1
 v=dict(re.findall(r'(\w+)=([^\s]+)',matches[0])) if matches else {}
 clocks=re.findall(r'^HISTORY_TIMING run_wall_seconds=([0-9.eE+-]+)$',text,re.M);assert len(clocks)<=1
 roster=re.findall(r'Player: (Nicowar[^\n]*) for team (\d+)',text)
 roster_ok=len(roster)==expected[2] and sorted(int(t) for _,t in roster)==list(range(expected[2]))
 if v:
  assert tuple(int(v[k]) for k in ['width','height','teams','controllers'])==expected
  assert int(v['compute_participants'])==participants and int(v['eligible_cpus'])==eligible and v['gradient_backend']=='cpu'
  assert roster_ok,'Wrong AI roster'
 ticks=int(v['ticks']) if v else None;winner=int(v.get('winner',-1));ended=int(v.get('ended',0))
 winner_ok=not capped and returncode==0 and bool(v) and ended==1 and 0<=winner<expected[2] and roster_ok
 return {'status':'cap' if capped else 'winner' if winner_ok else 'failed' if returncode or not v else 'no_winner','ticks':ticks,'winner':winner if winner_ok else None,'reported':v,'roster_valid':roster_ok,'inner_seconds':float(clocks[0]) if clocks else None,'observed_checksum':v.get('checksum') or v.get('finalChecksum')}
def checksum_evidence(target):
 evidence=[]
 for f in target.rglob('*.checksums'):
  item={'path':str(f.relative_to(target)),'sha256':sha(f),'complete':False}
  try:
   stream=f.open('rb');data=mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ);magic,teams,players,rows,flags=struct.unpack_from('<4sIIII',data);assert magic==b'GCS1' and 1<=teams<=32 and rows>0 and flags==0
   pos=20;first=None;last=None
   def u32():
    nonlocal pos
    n=struct.unpack_from('<I',data,pos)[0];pos+=4;return n
   for _ in range(rows):
    tick=u32();total=u32();assert last is None or tick==last[0]+1;first=(tick,total) if first is None else first;last=(tick,total)
    for _ in range(teams):
     u32()
     for _ in range(2):
      count=u32();assert count<=65536
      for _ in range(count):pos+=6;words=u32();pos+=words*4;assert pos<=len(data)
   assert pos==len(data)
   item.update(complete=True,format='GCS1',teams=teams,players=players,rows=rows,first=list(first),last=list(last))
  except (AssertionError,ValueError,struct.error,OSError) as error:item['parse_error']=str(error)
  finally:
   if 'data' in locals():data.close();del data
   if 'stream' in locals():stream.close();del stream
  evidence.append(item)
 return evidence
def command(variant,mapname,budget,mask):
 return ['taskset','-c',','.join(map(str,mask)),variant['binary'],'dev','random-games','--runs','1','--ticks','2147483647','--map',mapname,'--matchup',','.join(['nicowar']*(8 if mapname=='Playground' else 11)),'--compute-threads',str(budget),'--gradient-backend','cpu']
def main():
 ap=argparse.ArgumentParser();ap.add_argument('regime',choices=['serial','waves']);ap.add_argument('--execute',action='store_true');a=ap.parse_args();assert a.execute,'Prepared only; explicit root --execute required'
 lock=(O/'launcher.lock').open('a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 plan=read(O/'plan.json');assert len(plan['seeds'])==len(set(plan['seeds']))==32 and plan['base_revision']=='054804590ee1dd109f09642fc05654c324f08344';review=read(O/'independent-review.json');approval=read(O/'root-approved.json')
 assert review['approved'] and review['source_sha256']==sha(__file__)
 assert approval['approved'] and approval['source_sha256']==sha(__file__) and approval['plan_sha256']==sha(O/'plan.json') and a.regime in approval['regimes']
 assert sha(plan['recipe'])==plan['recipe_sha256'] and sha(plan['seed_source'])==plan['seed_source_sha256']
 for item in approval['prior_confirmation_evidence']:assert sha(item['path'])==item['sha256']
 assert approval['prior_confirmation_evidence'],'Root must bind motivating completed confirmation evidence'
 resolved_path=Path(plan['latest_resolve']);assert review['plan_sha256']==sha(O/'plan.json');assert sha(resolved_path)==approval['latest_resolve_sha256'];resolved=read(resolved_path);assert resolved['base_revision']==plan['base_revision'];variants=resolved['variants'];assert {v['role'] for v in variants}=={'baseline','wake-only'}
 for v in variants:
  assert sha(v['binary'])==v['sha256']==approval['binary_hashes'][v['role']] and v['base_revision']==plan['base_revision']
  for fixture,h in plan['fixtures'].items():assert sha(Path(v['cwd'])/'maps'/Path(fixture).name)==h
 campaign=approval['campaign'];active={};mu=threading.Lock();stop=threading.Event()
 outer=None;outer_path=O.parent/'final-controller/final-controller.py'
 if approval.get('outer_controller_sha256'):
  parent=os.getppid();parent_args=[x.decode() for x in (Path('/proc')/str(parent)/'cmdline').read_bytes().split(b'\0') if x]
  assert Path(parent_args[0]).name.startswith('python') and len(parent_args)>1 and Path(parent_args[1]).resolve()==outer_path.resolve() and sha(outer_path)==approval['outer_controller_sha256']
  outer={'pid':parent,'starttime':process_identity(parent)[1]}
 def interrupt(*_):stop.set();raise KeyboardInterrupt('Final cohort administratively interrupted; owned children will be cleaned')
 signal.signal(signal.SIGTERM,interrupt);signal.signal(signal.SIGINT,interrupt)
 def gate(allow_children=False):
  if outer:assert os.getppid()==outer['pid'] and process_identity(outer['pid']) and process_identity(outer['pid'])[1]==outer['starttime'] and sha(outer_path)==approval['outer_controller_sha256']
  ident=process_identity(campaign['pid']);assert ident and ident[1]==str(campaign['starttime']);q=Path('/proc')/str(campaign['pid']);assert (q/'stat').read_text().split(') ',1)[1].split()[0]=='T' and b'expanded-runner.py' in(q/'cmdline').read_bytes()
  for f in Path('/proc').glob('[0-9]*/cmdline'):
   try:args=[x.decode(errors='replace') for x in f.read_bytes().split(b'\0') if x];state=(f.parent/'stat').read_text().split(') ',1)[1].split()[0];pid=int(f.parent.name)
   except (OSError,IndexError):continue
   if not args or state=='Z' or pid==os.getpid() or pid==campaign['pid'] or (outer and pid==outer['pid'] and process_identity(pid) and process_identity(pid)[1]==outer['starttime']):continue
   current_identity=process_identity(pid)
   if current_identity is None:continue
   assert not(Path(args[0]).name.startswith('glob2') and (not allow_children or pid not in active or current_identity[1]!=active[pid]['starttime'])),('Unexpected native process',pid,args)
   executable=Path(args[0]).name;script=None
   if executable.startswith('python'):
    for token in args[1:]:
     if token in ['-c','-m']:break
     if not token.startswith('-'):script=Path(token);break
   else:script=Path(args[0]).resolve()
   if script is not None and not script.is_absolute():
    try:script=(Path(os.readlink(f.parent/'cwd'))/script).resolve()
    except OSError:continue
   elif script is not None:script=script.resolve()
   blocked={P/'paired-controller.py',P/'timing-runner.py',P/'parallel-parity.py',P/'pilot.py',P/'resume-after-parity-handoff.py',O.parent/'wake-only-allcore/paired-controller.py',O.parent/'balanced-n4/controller.py',outer_path}
   assert script not in {q.resolve() for q in blocked},('Other controller',pid,script)
  for f,h in plan['fixtures'].items():assert sha(f)==h
 gate();out=O/a.regime;assert not out.exists(),'Do not overwrite/reuse attempted regime';out.mkdir();save(out/'claim.json',{'pid':os.getpid(),'starttime':process_identity(os.getpid())[1],'source_sha256':sha(__file__),'plan_sha256':sha(O/'plan.json')})
 start=time.monotonic();receipts=[];waves=[];pairs=[]
 def run(v,mapname,seed,index,mask,budget,wave):
  if stop.is_set():raise RuntimeError('No new games after sibling failure')
  target=out/mapname/(str(seed)+'-'+v['role']);assert not target.exists();target.mkdir(parents=True)
  assert sha(v['binary'])==v['sha256'];assert sha(Path(v['cwd'])/'maps'/(mapname+'.map'))==next(h for f,h in plan['fixtures'].items() if Path(f).name==mapname+'.map');profile=target/'profile';profile.mkdir();env={k:value for k,value in os.environ.items() if not k.startswith('GLOB2_')};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_USER_DIR=str(profile),GLOB2_USER_DATA_DIR=str(profile),GLOB2_TEST_SEED=str(seed),GLOB2_TEST_MAX_TICKS='2147483647',GLOB2_ASSET_DIR=v['cwd'])
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
   elapsed=time.monotonic()-t;n=parse_log((target/'stdout.log').read_text(errors='replace'),mapname,32 if budget=='auto' else int(budget),len(mask),child.returncode,capped);cpu=usage.ru_utime+usage.ru_stime
   checks=checksum_evidence(target);assert sha(v['binary'])==v['sha256']
   r=dict(n,seed=seed,seed_index=index,map=mapname,role=v['role'],wave=wave,allocation=mask,compute_requested=budget,command=cmd,cwd=v['cwd'],env_overrides={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','GLOB2_USER_DIR','GLOB2_USER_DATA_DIR','GLOB2_TEST_SEED','GLOB2_TEST_MAX_TICKS','GLOB2_ASSET_DIR']},command_sha256=command_hash,fixture_sha256=plan['fixtures'],binary_sha256=v['sha256'],base_revision=plan['base_revision'],returncode=child.returncode,cap_seconds=plan['cap_seconds'],started_monotonic=t,finished_monotonic=t+elapsed,e2e_seconds=elapsed,user_cpu_seconds=usage.ru_utime,system_cpu_seconds=usage.ru_stime,total_cpu_seconds=cpu,peak_rss_kib=usage.ru_maxrss,voluntary_context_switches=usage.ru_nvcsw,involuntary_context_switches=usage.ru_nivcsw,checksum_files=checks,stdout_sha256=sha(target/'stdout.log'))
   r['cpu_per_tick']=cpu/n['ticks'] if n['ticks'] else None;r['e2e_tps']=n['ticks']/elapsed if n['ticks'] else None;r['inner_tps']=n['ticks']/n['inner_seconds'] if n['ticks'] and n['inner_seconds'] else None
   save(target/'receipt.json',r);assert n['status']!='failed',target
   with mu:receipts.append(r);save(out/'progress.json',{'completed':len(receipts),'total':128,'active':active,'elapsed_seconds':time.monotonic()-start})
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
  w={'map':mapname,'role':v['role'],'wave':number,'seed_indices':indices,'started_monotonic':begin,'finished_monotonic':end,'makespan_seconds':span,'winners':wins,'caps':sum(r['status']=='cap' for r in rows),'games_per_hour':wins*3600/span,'observed_ticks_per_second':ticks/span,'complete_tick_coverage':all(r['ticks'] is not None for r in rows),'sum_game_seconds':sum(r['e2e_seconds'] for r in rows),'scope':'Actual homogeneous wave makespan including launches/waits, not sum of parallel game runtimes.'};waves.append(w);save(out/('wave-'+str(number)+'-'+mapname+'-'+v['role']+'.json'),w)
  return rows
 for mapname in ['Playground','Oazis']:
  width=1 if a.regime=='serial' else 4
  for offset in range(0,32,width):
   indices=list(range(offset,offset+width));order=variants if (offset//width)%2==0 else list(reversed(variants));results={}
   for v in order:results[v['role']]=wave(v,mapname,indices,offset//width)
   for i in indices:
    b=next(r for r in results['baseline'] if r['seed_index']==i);w=next(r for r in results['wake-only'] if r['seed_index']==i);valid=b['status']==w['status']=='winner';trajectory=valid and (b['ticks'],b['winner'])==(w['ticks'],w['winner'])
    if valid and b['checksum_files'] and w['checksum_files']:
     be=[(x.get('rows'),x.get('first'),x.get('last')) for x in b['checksum_files'] if x['complete']];we=[(x.get('rows'),x.get('first'),x.get('last')) for x in w['checksum_files'] if x['complete']];trajectory=trajectory and bool(be) and be==we
    if valid and b['observed_checksum'] is not None and w['observed_checksum'] is not None:trajectory=trajectory and b['observed_checksum']==w['observed_checksum']
    pairs.append({'map':mapname,'seed_index':i,'seed':plan['seeds'][i],'both_winners':valid,'endpoints_match':trajectory if valid else None,'statuses':[b['status'],w['status']],'eligible_winner_timing_pair':trajectory,'limitation':'Ticks/winner equality without checksum sidecar is not per-tick equivalence.'})
   save(out/'pairs.json',pairs)
   mismatches=[x for x in pairs if x['both_winners'] and not x['endpoints_match']]
   if mismatches:
    assert not active,'Endpoint validation must occur at clean completed pair/wave boundary'
    save(out/'trajectory-mismatch.json',{'state':'failed_endpoint_equivalence','pairs':mismatches,'all_logs_and_completed_pair_receipts_retained':True});raise RuntimeError('Both-winner endpoints differ; retain clean campaign hold for root')
 span=time.monotonic()-start;receipt_hashes={str(q.relative_to(out)):sha(q) for q in out.glob('*/*/receipt.json')};save(out/'complete.json',{'state':'complete','plan_sha256':sha(O/'plan.json'),'launcher_sha256':sha(__file__),'receipt_hashes':receipt_hashes,'wave_hashes':{str(q.relative_to(out)):sha(q) for q in out.glob('wave-*.json')},'regime':a.regime,'runs':len(receipts),'campaign_makespan_seconds':span,'winners':sum(r['status']=='winner' for r in receipts),'caps':sum(r['status']=='cap' for r in receipts),'campaign_completed_games_per_hour':sum(r['status']=='winner' for r in receipts)*3600/span,'campaign_tick_coverage_complete':all(r['ticks'] is not None for r in receipts),'waves':waves,'pairs':pairs,'scope':'Two-role interleaved campaign makespan; role throughput comparisons use matched homogeneous waves, not overlapping role campaign spans. Caps are censored outcomes, never winners. Campaign left paused for root decision.'})
if __name__=='__main__':main()
