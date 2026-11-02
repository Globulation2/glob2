#!/usr/bin/env python3
"""Detached scoped supervisor: preserved coordinator, fixed dispatch cutoff, drain/report."""
import argparse,datetime as dt,fcntl,gzip,json,os,signal,subprocess,sys,threading,time
from pathlib import Path
UTC=dt.timezone.utc
HELPER=r'''import json,sys,fcntl,time
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from tournaments.coordinator import Coordinator
from tournaments.transport import Transport
from tournaments.common import read_json
root=Path(sys.argv[2]);action=sys.argv[3];host=read_json(root/'hosts.json')[0]
c=Coordinator(root,repair=False);t=Transport(host,root/'worker.pyz');t.deployed=True
try:
 if action=='status':
  value={'coordinator':c.status(),'worker':t.rpc('status')}
 elif action=='drain':
  c.control('draining');value={'coordinator_mode':c.mode,'worker':t.rpc('control',experiment=c.manifest['id'],mode='draining'),'time':time.time()}
 elif action=='stop':
  value={'worker':t.rpc('stop')}
  path=Path(t.root)/'daemon.lock'
  for i in range(120):
   try:
    with path.open('a+b')as f:fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB);fcntl.flock(f,fcntl.LOCK_UN)
    value['daemon_lock_released']=True;break
   except BlockingIOError:time.sleep(.25)
  else:value['daemon_lock_released']=False
 else:raise ValueError(action)
 print(json.dumps(value))
finally:t.close();c.close()
'''
def atomic(path,value):
 temp=path.with_suffix(path.suffix+'.tmp');temp.write_text(json.dumps(value,indent=2)+'\n');temp.replace(path)
def now():return dt.datetime.now(UTC)
def decision(clock,cutoff,drained,states,live):
 if not states.get('pending',0)and not states.get('active',0)and live==0:return 'complete'
 if clock>=cutoff and not drained:return 'drain'
 if drained and live==0:return 'idle_drained'
 return 'wait'
def idle_count(previous,live,ack,rpc_ok=True):
 return previous+1 if rpc_ok and ack and live==0 else 0
def deadline_watch(stop_event,delay,callback):
 if not stop_event.wait(max(0,delay)):callback()
def capture_output(stream,path,overflow,report,limit=8*1024**2):
 total=0
 try:
  with gzip.open(path,'wb',compresslevel=1)as out:
   while block:=stream.read(65536):
    total+=len(block)
    if total>limit:
     if not overflow.is_set():overflow.set();report('coordinator_log_limit',raw_bytes=total)
     continue
    out.write(block)
 finally:stream.close()
def self_test():
 x=dt.datetime(2026,11,2,11,30,tzinfo=UTC)
 assert decision(x-dt.timedelta(seconds=1),x,False,{'pending':1},0)=='wait'
 assert decision(x,x,False,{'pending':1},0)=='drain'
 assert decision(x,x,True,{'pending':1},1)=='wait'
 assert decision(x,x,True,{'pending':1},0)=='idle_drained'
 assert decision(x,x,True,{'completed':768},0)=='complete'
 assert decision(x,x,True,{'completed':768},1)=='wait'
 count=0
 for live,ack,ok,expected in [(0,False,True,0),(1,True,True,0),(0,True,True,1),(0,True,False,0),(0,True,True,1),(0,True,True,2),(1,True,True,0),(0,True,True,1),(0,True,True,2),(0,True,True,3)]:
  count=idle_count(count,live,ack,ok);assert count==expected
 # Deadline callback proceeds despite a monitor loop repeatedly failing RPC.
 trigger=threading.Event();stop=threading.Event();watch=threading.Thread(target=deadline_watch,args=(stop,.02,trigger.set));watch.start()
 until=time.monotonic()+.06
 while time.monotonic()<until:
  try:raise RuntimeError('synthetic RPC outage')
  except RuntimeError:time.sleep(.001)
 watch.join();assert trigger.is_set()
 trigger.clear();stop.set();watch=threading.Thread(target=deadline_watch,args=(stop,.01,trigger.set));watch.start();watch.join();assert not trigger.is_set()
 import tempfile,io
 with tempfile.TemporaryDirectory()as temp:
  captures=[];processes=[]
  for seq,text in enumerate(['first','second','third']):
   proc=subprocess.Popen([sys.executable,'-c','import sys;sys.stdout.write(sys.argv[1]*1000)',text],stdout=subprocess.PIPE);path=Path(temp)/f'{seq}.gz';events=[]
   thread=threading.Thread(target=capture_output,args=(proc.stdout,path,threading.Event(),lambda *args,**kwargs:events.append((args,kwargs))));thread.start();captures.append((thread,path,text));processes.append(proc)
  for proc in processes:assert proc.wait()==0
  for thread,path,text in captures:
   thread.join();assert gzip.open(path,'rt').read()==text*1000
  overflow=threading.Event();events=[];path=Path(temp)/'overflow.gz';capture_output(io.BytesIO(b'x'*1000),path,overflow,lambda *args,**kw:events.append((args,kw)),limit=10)
  assert overflow.is_set()and len(events)==1;assert gzip.open(path,'rb').read()==b''
 print(json.dumps({'self_test':'passed','cases':22,'covers':['cutoff','live-late-attempt','unacknowledged-drain','RPC-error-reset','deadline-during-RPC-outage','cancelled-watchdog']}))
def main():
 p=argparse.ArgumentParser();p.add_argument('--directory');p.add_argument('--detach',action='store_true');p.add_argument('--self-test',action='store_true');a=p.parse_args()
 if a.self_test:self_test();return
 root=Path(a.directory).resolve();archive=root/'worker.pyz';assert archive.is_file();manifest=json.load((root/'experiment.json').open());expected=json.load((root/'expected-inventory.json').open());assert len(manifest['jobs'])==expected['counts']['total']
 if a.detach:
  with (root/'supervisor-launch.log').open('ab')as log:
   child=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--directory',str(root)],stdout=log,stderr=subprocess.STDOUT,start_new_session=True,cwd=root)
  atomic(root/'supervisor-launch.json',{'pid':child.pid,'time':now().isoformat(),'command':[sys.executable,str(Path(__file__).resolve()),'--directory',str(root)]});print(json.dumps({'detached_pid':child.pid,'directory':str(root)}));return
 os.sched_setaffinity(0,set(json.load((root/'hosts.json').open())[0]['cpu_ids']))
 os.setpriority(os.PRIO_PROCESS,0,10)
 with (root/'supervisor.lock').open('a+b')as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  cutoff=dt.datetime.fromisoformat(manifest['design']['dispatch_cutoff_utc']);analysis_guard=dt.datetime(2026,11,2,12,35,tzinfo=UTC)
  event_lock=threading.Lock();stop_watch=threading.Event();drained=threading.Event();drain_ack=threading.Event();drain_mutex=threading.Lock();overflow=threading.Event();events_bytes=0
  events=gzip.open(root/'supervisor-events.jsonl.gz','ab',compresslevel=1)
  def event(kind,**fields):
   nonlocal events_bytes
   row=json.dumps({'time':now().isoformat(),'event':kind,**fields},separators=(',',':')).encode()+b'\n'
   with event_lock:
    events_bytes+=len(row)
    if events_bytes>8*1024**2:raise RuntimeError('supervisor event log exceeded8MiB; explicit controller failure')
    events.write(row);events.flush()
  def rpc(action):
   result=subprocess.run([sys.executable,'-c',HELPER,str(archive),str(root),action],capture_output=True,text=True,timeout=210)
   if result.returncode:raise RuntimeError(f'{action} failed:{result.stderr[-3000:]}')
   return json.loads(result.stdout)
  def drain(reason):
   if drain_ack.is_set()or not drain_mutex.acquire(blocking=False):return
   drained.set();event('drain_requested',reason=reason,nominal_cutoff=cutoff.isoformat())
   for attempt in range(3):
    try:
     value=rpc('drain')
     if value.get('coordinator_mode')!='draining'or value.get('worker',{}).get('mode')!='draining':raise RuntimeError('drain acknowledgment mode mismatch')
     event('drain_acknowledged',value=value);atomic(root/'dispatch-cutoff.json',{'reason':reason,'nominal_cutoff':cutoff.isoformat(),'acknowledged':now().isoformat(),'value':value});drain_ack.set();drain_mutex.release();return
    except Exception as e:event('drain_error',attempt=attempt,error=str(e))
   event('drain_acknowledgement_unavailable');drain_mutex.release()
  def watchdog():
   # Independent from slow status RPCs. Begin5s early to allow the local worker
   # control roundtrip; coordinator DB dispatch is disabled before its RPC call.
   delay=max(0,(cutoff-now()).total_seconds()-5)
   deadline_watch(stop_watch,delay,lambda:drain('fixed_dispatch_cutoff'))
  watchdog_thread=threading.Thread(target=watchdog,daemon=True);watchdog_thread.start()
  sequence=0;capture_threads=[]
  def launch():
   nonlocal sequence
   sequence+=1;process=subprocess.Popen([sys.executable,str(archive),'run',str(root),'--hosts',str(root/'hosts.json')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,cwd=root)
   thread=threading.Thread(target=capture_output,args=(process.stdout,root/f'coordinator-supervised-{sequence}.log.gz',overflow,event));capture_threads.append(thread);thread.start();event('coordinator_started',pid=process.pid,sequence=sequence);return process
  child=launch();atomic(root/'supervisor-pid.json',{'pid':os.getpid(),'coordinator_pid':child.pid,'started':now().isoformat(),'cutoff':cutoff.isoformat()})
  idle=0;last=None;reason=None;safe_to_stop=False;caretaker=False;infrastructure_failure=False;analysis_thread=None;analysis_state={};analysis_mutex=threading.Lock()
  def analyze_once(why,refresh=False):
   nonlocal analysis_thread
   with analysis_mutex:
    if analysis_thread is not None and (analysis_thread.is_alive() or not refresh):return
    def work():
     event('analysis_started',reason=why);analysis_state['reason']=why;analysis_state['started']=now().isoformat()
     try:
      result=subprocess.run([sys.executable,str(root/'scripts/analyze.py'),'--directory',str(root)],cwd=root,capture_output=True,text=True,timeout=max(1,min(1080,int((dt.datetime(2026,11,2,12,55,tzinfo=UTC)-now()).total_seconds()))))
      with gzip.open(root/'analysis.log.gz','wb',compresslevel=1)as log:log.write((result.stdout+result.stderr).encode())
      analysis_state.update(returncode=result.returncode,finished=now().isoformat())
     except Exception as e:analysis_state.update(returncode=None,error=str(e),finished=now().isoformat())
     atomic(root/'analysis-completion.json',analysis_state);event('analysis_finished',value=analysis_state)
    analysis_thread=threading.Thread(target=work);analysis_thread.start()
  def analysis_watchdog():
   deadline_watch(stop_watch,(analysis_guard-now()).total_seconds(),lambda:analyze_once('fixed_analysis_guard_snapshot'))
  analysis_watchdog_thread=threading.Thread(target=analysis_watchdog,daemon=True);analysis_watchdog_thread.start()
  try:
   while True:
    if overflow.is_set():drain('controller_log_overflow')
    try:
     last=rpc('status');states=last['coordinator']['jobs'];live=sum(x['experiment']==manifest['id']and x['state']in('running','executed','packing','done')for x in last['worker']['attempts']);state=decision(now(),cutoff,drain_ack.is_set(),states,live)
     atomic(root/'supervisor-status.json',{'time':now().isoformat(),'supervisor_pid':os.getpid(),'coordinator_pid':child.pid if child else None,'caretaker':caretaker,'draining':drained.is_set(),'live_worker_attempts':live,'status':last})
     event('checkpoint',jobs=states,live=live,draining=drained.is_set())
     if state=='drain':drain('fixed_dispatch_cutoff')
     if state=='complete':drain('all_jobs_terminal')
     idle=idle_count(idle,live,drain_ack.is_set())
     if idle>=3:reason='all_jobs_terminal'if state=='complete'else'cutoff_drained';safe_to_stop=True;break
     if now()>=analysis_guard:analyze_once('analysis_guard_snapshot_with_explicit_unfinished_inventory')
    except Exception as e:idle=idle_count(idle,None,drain_ack.is_set(),False);event('status_error',error=str(e))
    if child is not None and child.poll()is not None:
     event('coordinator_exited',returncode=child.returncode)
     for thread in capture_threads:thread.join(timeout=5)
     if sequence<4:child=launch()
     else:
      drain('coordinator_restart_limit');caretaker=True;infrastructure_failure=True;child=None
      atomic(root/'caretaker-infrastructure-failure.json',{'time':now().isoformat(),'actual_restarts':3,'mode':'draining; collection-only caretaker; no new run launches'})
      analyze_once('coordinator_restart_limit_snapshot')
    if caretaker:
     try:
      collected=subprocess.run([sys.executable,str(archive),'collect',str(root),'--hosts',str(root/'hosts.json')],capture_output=True,text=True,timeout=210)
      event('caretaker_collection',returncode=collected.returncode,error=collected.stderr[-2000:])
     except Exception as e:event('caretaker_collection_error',error=str(e))
    time.sleep(15)
  finally:
   stop_watch.set()
   if safe_to_stop:
    if child is not None:
     if child.poll()is None:child.terminate()
     try:child.wait(timeout=30)
     except subprocess.TimeoutExpired:child.kill();child.wait();event('coordinator_force_stop')
    try:stopped=rpc('stop');atomic(root/'worker-stop.json',stopped);event('worker_stopped',value=stopped)
    except Exception as e:event('worker_stop_error',error=str(e))
   else:
    analyze_once('exceptional_snapshot_while_worker_left_collecting')
    event('unsafe_to_stop_worker_left_collecting',coordinator_pid=child.pid if child else None)
    atomic(root/'caretaker-required.json',{'coordinator_pid':child.pid if child else None,'reason':'supervisor exceptional exit; no verified idle; worker/coordinator deliberately left alive'})
  for thread in capture_threads:thread.join()
  watchdog_thread.join(timeout=10);analysis_watchdog_thread.join(timeout=10)
  analyze_once(reason or 'exceptional_snapshot')
  analysis_thread.join(timeout=1100)
  if safe_to_stop and 'snapshot' in analysis_state.get('reason',''):
   remaining=(dt.datetime(2026,11,2,12,55,tzinfo=UTC)-now()).total_seconds()
   if remaining>=180:
    analyze_once('final_idle_refresh',refresh=True);analysis_thread.join(timeout=min(1100,remaining+10))
   else:
    atomic(root/'late-results-not-refreshed.json',{'time':now().isoformat(),'reason':'insufficient bounded time for final refresh','remaining_seconds':remaining,'snapshot_analysis':dict(analysis_state)})
  atomic(root/'supervisor-finished.json',{'time':now().isoformat(),'reason':reason,'analysis':analysis_state,'target':'2026-11-02T12:55:00+00:00','before_target':now()<dt.datetime(2026,11,2,12,55,tzinfo=UTC),'analysis_snapshot_may_precede_final_idle':'snapshot' in analysis_state.get('reason',''),'safe_idle_stop':safe_to_stop,'infrastructure_failure':infrastructure_failure,'controller_log_overflow':overflow.is_set()});event('finished',analysis=analysis_state,reason=reason);events.close()
if __name__=='__main__':main()
