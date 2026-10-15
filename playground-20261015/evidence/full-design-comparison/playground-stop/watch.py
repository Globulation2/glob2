#!/usr/bin/env python3
"""Read-only completion watcher for root-installed PG-only output sentinel. No signals."""
from pathlib import Path
import json,hashlib,os,time,fcntl
D=Path(__file__).resolve().parent;C=D.parent/'campaign'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return json.loads(Path(p).read_text())
def save(p,x):
 t=p.with_suffix('.tmp');t.write_text(json.dumps(x,indent=2)+'\n');t.replace(p)
def proc(pid):
 try:
  z=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split();return z[0],z[19]
 except FileNotFoundError:return None
def retired(x):
 z=proc(x['pid']);return not z or z[1]!=str(x['starttime']) or z[0]=='Z'
def main():
 lock=(D/'sentinel.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 a=read(D/'root-approved.json');r=read(D/'independent-review.json')
 assert a['approved'] and r['approved'] and a['source_sha256']==r['source_sha256']==sha(__file__)
 assert a['review_sha256']==sha(D/'independent-review.json')
 assert a['runner']=={'pid':460600,'starttime':'334012252'} and not retired(a['runner'])
 assert a['runner_sha256']==sha(C/'runner.py') and a['plan_sha256']==sha(C/'plan.json')
 assert read(C/'serial'/'claim.json')['source_sha256']==a['runner_sha256']
 args=Path('/proc/460600/cmdline').read_bytes().split(b'\0')[:-1]
 assert str(C/'runner.py').encode() in args and b'serial'in args and b'--execute'in args
 assert not (C/'waves').exists()
 installation=read(D/'sentinel-installed.json');sentinel=C/'serial'/'Oazis'
 assert installation['path']==str(sentinel) and installation['sha256']==sha(sentinel)
 assert sentinel.is_file() and not sentinel.is_symlink()
 data=sentinel.read_bytes();body=json.loads(data)
 assert body['state']=='administrative_stop_requested_before_oazis' and body['not_game_data']is True
 assert a['sentinel_receipt_sha256']==sha(D/'sentinel-installed.json') and a['sentinel_sha256']==sha(sentinel)
 assert a['controller']=={'pid':460595,'starttime':'334012174'}
 controller_args=Path('/proc/460595/cmdline').read_bytes().split(b'\0')[:-1]
 assert str(C/'controller.py').encode() in controller_args
 for identity in [a['runner'],a['controller']]:
  while not retired(identity):time.sleep(.5)
 assert sentinel.is_file() and sentinel.read_bytes()==data and not(C/'waves').exists()
 plan=read(C/'plan.json');roles=plan['roles'];assert len(roles)==14 and len(plan['seeds'])==32
 expected={f"Playground/{seed}-{role}/receipt.json"for seed in plan['seeds']for role in roles}
 actual={str(q.relative_to(C/'serial'))for q in(C/'serial'/'Playground').glob('*/receipt.json')};assert actual==expected
 waves=[q for q in(C/'serial').glob('wave-*-Playground-*.json')if not q.name.endswith('.binding.json')];assert {q.name for q in waves}=={f'wave-{i}-Playground-{role}.json'for i in range(32)for role in roles}
 for q in waves:assert read(q.with_suffix('.binding.json'))['sha256']==sha(q)
 pairs=read(C/'serial'/'pairs.json');assert len(pairs)==416 and {(x['map'],x['seed_index'],x['seed'],x['reference'],x['candidate'])for x in pairs}=={('Playground',i,seed,'baseline',role)for i,seed in enumerate(plan['seeds'])for role in roles if role!='baseline'}
 save(D/'complete.json',{'state':'stopped_after_complete_playground','runs':448,'plan_sha256':a['plan_sha256'],'runner_sha256':a['runner_sha256'],'receipt_hashes':{q:sha(C/'serial'/q)for q in sorted(actual)},'wave_hashes':{q.name:sha(q)for q in waves},'pairs_sha256':sha(C/'serial'/'pairs.json'),'retired_processes':[a['runner'],a['controller']],'no_oazis_or_waves_started':True,'native_games_interrupted':False,'unexpected_native_abort':False,'authorized_cap_signal':False,'scope_sentinel':{'path':str(sentinel),'sha256':sha(sentinel)},'signals_sent':False,'limitation':'Administrative output sentinel raises before first OA target directory/Popen. Original game caps and frozen receipts remain authoritative; no observer suspension or extra final-game e2e delay.'})
if __name__=='__main__':main()
