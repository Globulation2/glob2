#!/usr/bin/env python3
"""Administrative PG-only stop. No engine signal except original one-hour cap."""
from pathlib import Path
import os,time,signal,json,hashlib,subprocess,sys,fcntl
D=Path(__file__).resolve().parent; C=D.parent/'campaign'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def save(p,x):
 p=Path(p);t=p.with_suffix('.tmp-'+str(os.getpid()));t.write_text(json.dumps(x,indent=2)+'\n');t.replace(p)
def proc(pid):
 try:
  s=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split();return {'state':s[0],'parent':int(s[1]),'starttime':s[19]}
 except FileNotFoundError:return None
def same(x):
 p=proc(x['pid']);return p if p and p['starttime']==str(x['starttime']) else None
def dead(x):
 p=same(x);return not p or p['state']=='Z'
def send(x,s):
 if same(x):os.kill(x['pid'],s)
def finish(a):
 # Both owner and independent rescue can enter; serialize all signalling.
 with (D/'finish.lock').open('a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX)
  if (D/'released.json').exists():return
  child=a['child']; runner=a['runner']; capped=False
  while not dead(child):
   # /proc birth is slightly later than runner's pre-Popen clock; original cap
   # dominates final receipt elapsed. Never terminate an uncapped live game.
   age=time.monotonic()-int(child['starttime'])/os.sysconf('SC_CLK_TCK')
   if age>=3600:
    assert os.getpgid(child['pid'])==child['pid'];os.killpg(child['pid'],signal.SIGTERM);capped=True
    until=time.monotonic()+10
    while not dead(child) and time.monotonic()<until:time.sleep(.05)
    if not dead(child):os.killpg(child['pid'],signal.SIGKILL)
   time.sleep(.05)
  # Defensive race path: if parent advanced before STOP, never abort a newer
  # native child. Report scope breach after it finishes; root receives failure.
  unexpected=[]
  for q in Path('/proc').glob('[0-9]*'):
   z=proc(int(q.name))
   if z and z['parent']==runner['pid'] and int(q.name)!=child['pid'] and z['state']!='Z':
    newer={'pid':int(q.name),'starttime':z['starttime']};unexpected.append(newer)
    while not dead(newer):
     if time.monotonic()-int(newer['starttime'])/os.sysconf('SC_CLK_TCK')>=3600:
      assert os.getpgid(newer['pid'])==newer['pid'];os.killpg(newer['pid'],signal.SIGTERM);time.sleep(10)
      if not dead(newer):os.killpg(newer['pid'],signal.SIGKILL)
     time.sleep(.05)
  if unexpected:save(D/'unexpected-children.json',{'state':'scope_breach_no_child_aborted','children':unexpected})
  # wait4 is FIRST in frozen runner poll: dead child reaped before stop check.
  send(runner,signal.SIGTERM);send(runner,signal.SIGCONT)
  save(D/'released.json',{'state':'runner_released_with_pending_administrative_stop','armed':a,'cap_signal_required':capped,'monotonic':time.monotonic(),'native_games_interrupted':capped,'scope':'Only original one-hour cap may terminate native; final e2e includes observer polling/reap delay.'})
def rescue():
 a=read(D/'armed.json');save(D/'guardian-ready.json',{'pid':os.getpid(),'starttime':proc(os.getpid())['starttime']})
 # Dedicated session, no inherited terminal signals. Parent death transfers ownership.
 while not (D/'released.json').exists():
  if dead(a['watcher']):finish(a);return
  time.sleep(.1)
def main():
 if '--guardian' in sys.argv:rescue();return
 stop=[False]
 for s in [signal.SIGINT,signal.SIGTERM]:signal.signal(s,lambda *_:stop.__setitem__(0,True))
 approval=read(D/'root-approved.json');assert approval['approved'] and approval['source_sha256']==sha(__file__)
 assert read(D/'independent-review.json')['approved']
 assert approval['review_sha256']==sha(D/'independent-review.json')
 assert approval['plan_sha256']==sha(C/'plan.json') and approval['runner_sha256']==sha(C/'runner.py')
 assert read(D/'independent-review.json')['source_sha256']==sha(__file__)
 runner=approval['runner'];assert runner=={'pid':460600,'starttime':'334012252'}
 assert read(C/'serial'/'claim.json')['source_sha256']==sha(C/'runner.py')
 args=Path(f"/proc/{runner['pid']}/cmdline").read_bytes().split(b'\0')[:-1]
 assert str(C/'runner.py').encode() in args and b'serial' in args and b'--execute' in args
 plan=read(C/'plan.json');roles=plan['roles'];roles=[x['role'] if isinstance(x,dict) else x for x in roles];assert len(roles)==14
 shift=15%14;last=list(reversed(roles[shift:]+roles[:shift]))[-1];seed=plan['seeds'][31]
 target=C/'serial'/'Playground'/(str(seed)+'-'+last);claim=target/'active-claim.json'
 assert not (D/'armed.json').exists(),'Never overwrite a previous stop attempt'
 save(D/'status.json',{'state':'waiting_final_playground_child','seed':seed,'role':last})
 while not claim.exists():
  assert same(runner) and not dead(runner),'Runner retired before target'
  if stop[0]:return
  time.sleep(.2)
 x=read(claim);child={'pid':x['pid'],'starttime':str(x['starttime'])};cp=same(child)
 assert cp and cp['state']!='Z' and cp['parent']==runner['pid'],'Target already finished; fail without signals'
 declared=next(z for z in plan['declared_commands']['serial'] if (z['map'],z['seed'],z['role'])==('Playground',seed,last))
 assert x['command']==declared['command'] and x['seed']==seed and x['role']==last
 assert Path(f"/proc/{child['pid']}/cmdline").read_bytes().split(b'\0')[:-1]==[z.encode() for z in x['command'][3:]]
 assert sorted(os.sched_getaffinity(child['pid']))==declared['allocation']
 assert not (target/'receipt.json').exists() and not (C/'serial'/'Oazis').exists() and not (C/'waves').exists()
 assert len(list((C/'serial'/'Playground').glob('*/receipt.json')))==447
 a={'runner':runner,'child':child,'watcher':{'pid':os.getpid(),'starttime':proc(os.getpid())['starttime']},'claim':str(claim),'claim_sha256':sha(claim),'seed':seed,'role':last}
 save(D/'armed.json',a)
 guardian=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--guardian'],start_new_session=True,stdout=(D/'guardian.log').open('a'),stderr=subprocess.STDOUT)
 deadline=time.monotonic()+5
 while not (D/'guardian-ready.json').exists():
  assert guardian.poll() is None and time.monotonic()<deadline;time.sleep(.02)
 assert read(D/'guardian-ready.json')['pid']==guardian.pid and guardian.poll() is None
 # Guardian is established before STOP, so owner crash cannot strand the runner.
 try:
  assert same(child) and same(runner);send(runner,signal.SIGSTOP)
  while (p:=same(runner)) and p['state']!='T':time.sleep(.01)
  assert p and p['state']=='T';save(D/'status.json',{'state':'runner_stopped_final_child_uninterrupted','armed':a})
 finally:
  finish(a)
 # Leave production scripts untouched; publish proof only after their own writes/exit.
 while not dead(runner):time.sleep(.1)
 receipts=list((C/'serial'/'Playground').glob('*/receipt.json'));waves=list((C/'serial').glob('wave-*-Playground-*.json'));waves=[q for q in waves if not q.name.endswith('.binding.json')]
 assert len(receipts)==448 and len(waves)==448 and len(read(C/'serial'/'pairs.json'))==416
 assert not (C/'serial'/'Oazis').exists() and not (C/'waves').exists()
 save(D/'complete.json',{'state':'stopped_after_complete_playground','runs':448,'plan_sha256':sha(C/'plan.json'),'runner_sha256':sha(C/'runner.py'),'receipt_hashes':{str(q.relative_to(C/'serial')):sha(q) for q in receipts},'wave_hashes':{q.name:sha(q) for q in waves},'pairs_sha256':sha(C/'serial'/'pairs.json'),'retired_processes':[runner],'no_oazis_or_waves_started':True,'native_games_interrupted':read(D/'released.json')['native_games_interrupted'],'limitation':'Last game e2e includes external 50ms poll and resume/reap latency; inner time remains original engine measurement. Caps retained separately.'})
if __name__=='__main__':main()
