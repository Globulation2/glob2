from pathlib import Path
import json,subprocess,os,datetime,hashlib
R=Path('/home/bradley/glob2-gradient-unified-h128');P=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/candidate-gen005/linux');P.mkdir(parents=True,exist_ok=True)
S='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e';assert not subprocess.check_output(['git','status','--porcelain'],cwd=R,text=True).strip()
subprocess.run(['git','fetch','/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/native-gen005.bundle','HEAD'],cwd=R,check=True);subprocess.run(['git','checkout','--detach',S],cwd=R,check=True)
old=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-004/candidate-gen004/linux-fix2/build-command.json');command=json.loads(old.read_text());command.update(source_revision=S,qualification='Generic12 cumulative1024 A16 coefficient-only research candidate',started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),pid=os.getpid());(P/'build-command.json').write_text(json.dumps(command,indent=2)+'\n');env=dict(os.environ,**command['env']);os.nice(19)
with (P/'build.log').open('w') as log:r=subprocess.run(command['argv'],cwd=R,env=env,stdout=log,stderr=subprocess.STDOUT)
status=dict(source_revision=S,exit_code=r.returncode,completed_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());(P/'build-status.json').write_text(json.dumps(status,indent=2)+'\n');assert r.returncode==0
subprocess.run(['python3',str(P/'check-parity.py')],cwd=R,env=env,check=True,stdout=(P/'parity.log').open('w'),stderr=subprocess.STDOUT)
