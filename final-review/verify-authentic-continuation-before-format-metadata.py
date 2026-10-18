import os,subprocess,json,pathlib,time,hashlib
root=pathlib.Path.cwd(); evidence=root/'artifacts/initial-delay-split'; binary=root/'build/linux/client/release/client-full-direct-1111/src/glob2'; old=pathlib.Path('/home/bradley/.codex/worktrees/17b7/glob2/artifacts/pr1197-investigation/diagnostics/runs/tournament-split-00190-pr1197-baseline-7000/output')
plan=[('legacy174-unbroken',old/'initial.game.gz',512,1),('current175-resume',evidence/'authentic174-p1/output/final.game.gz',512,1),('legacy174-live-p1',old/'final.game.gz',7256,1),('legacy174-live-p8',old/'final.game.gz',7256,8)]
records=[]
for name,source,ticks,participants in plan:
 run=evidence/name;run.mkdir();env=os.environ.copy();env.update(LD_LIBRARY_PATH='/home/bradley/.codex/worktrees/repair-tls-rejection-fixture/glob2/build/sdl3-ci-current/prefix/lib',HOME=str(run/'home'),XDG_CONFIG_HOME=str(run/'home/.config'));pathlib.Path(env['XDG_CONFIG_HOME']).mkdir(parents=True)
 cmd=['taskset','-c','17-24',str(binary),'game','run','--load-game',str(source),'--ticks',str(ticks),'--compute-threads',str(participants),'--gpu','off','--gradient-backend','cpu','--telemetry','checksums','--write-replay','--save','initial','--save','final','--output-dir',str(run/'output'),'--profile',name]
 start=time.time()
 with (run/'stdout.log').open('w') as out,(run/'stderr.log').open('w') as err: code=subprocess.call(cmd,cwd=root,env=env,stdout=out,stderr=err)
 record=dict(command=cmd,returncode=code,seconds=time.time()-start,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),input_sha256=hashlib.sha256(source.read_bytes()).hexdigest());(run/'receipt.json').write_text(json.dumps(record,indent=2));records.append(record);print(name,code,flush=True)
 assert code==0
 result=json.load(open(run/'output/result.json'));assert result['status']=='completed' and result['ticks']==ticks and result['compute_resolved_threads']==participants and result['compute_workers']==participants-1 and result['gpu_requested']==0

def checks(name):return dict(line.split() for line in (evidence/name/'output/world.checksums').read_text().splitlines())
assert checks('authentic174-p1')==checks('authentic174-p8')
a=checks('legacy174-unbroken');b=checks('current175-resume');assert b and all(a[t]==v for t,v in b.items())
assert checks('legacy174-live-p1')==checks('legacy174-live-p8')
(evidence/'authentic-continuation-verification.json').write_text(json.dumps(dict(passed=True,checks=['actual published174 initial save loads','actual published174 tick7000 save loads with pending work','1/8 participants identical every world tick','current175 save/resume equals uninterrupted every overlapping world tick'],runs=records),indent=2))
