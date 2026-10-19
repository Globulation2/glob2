import pathlib,subprocess,json,time,datetime,fcntl,os
D=pathlib.Path(__file__).resolve().parent;lock=(D/'mirror.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
R='/home/bradley/glob2/artifacts/gradient-unified-20261019/threshold-sweep-gen003/paired-horizon/collection/'
O=D/'remote';O.mkdir(exist_ok=True)
while True:
 p=subprocess.run(['rsync','-a','--exclude=profile/','devlaptop.local:'+R,str(O)+'/'],capture_output=True,text=True)
 v=dict(pid=os.getpid(),utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),last_sync_returncode=p.returncode,error=p.stderr[-2000:] if p.returncode else None)
 if (O/'STATUS.json').exists():
  s=json.loads((O/'STATUS.json').read_text());v['remote_status']=s.get('status');v['completed']=s.get('completed',0);v['active']=s.get('active')
 (D/'MIRROR-STATUS.json').write_text(json.dumps(v)+'\n')
 if p.returncode==0 and v.get('remote_status') in ('COMPLETE','FAILED-REQUIRES-REVIEW'):break
 time.sleep(30)
