import pathlib,json,subprocess,os,hashlib,datetime
R=pathlib.Path('/Users/bradley/glob2/artifacts/gradient-contradictions-20261018');B=R/'candidate-next/runtime-macos';binary=R/'candidate-next/integrated-macos-fix1/bin/glob2';mp=R/'macos-next-round/qualification/map.map.gz';cwd=pathlib.Path('/Users/bradley/.codex/worktrees/gradient-contradiction-candidate/glob2');job=json.loads((R/'macos-next-round/qualification/job.json').read_text());runs=[]
h=lambda p:hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def status(state): (B/'status.json').write_text(json.dumps(dict(status=state,source='2d4dc8e9e',binary_sha256=h(binary),map_sha256=h(mp),runs=runs,updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()),indent=2)+'\n')
def run(name,threads,probe,load=None,ticks=2048):
 out=B/name;out.mkdir(exist_ok=True);a=[str(binary),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--ticks',str(ticks),'--save','final','--save','every:512','--telemetry','checksums','--write-replay','--gpu','off','--compute-telemetry','off','--compute-threads',str(threads)]
 if load:a+=['--load-game',str(load)]
 else:
  a+=['--map-file',str(mp),'--game-seed',str(job['seeds']['game'])]
  for p in job['config']['players']:a+=['--player',p]
 if probe:a+=['--resource-gradient-probe']
 (out/'command.json').write_text(json.dumps(a,indent=2)+'\n')
 with (out/'stdout.log').open('w') as log,(out/'stderr.log').open('w') as err:p=subprocess.run(a,cwd=cwd,env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy'),stdout=log,stderr=err,timeout=1800)
 row=dict(name=name,threads=threads,probe=probe,returncode=p.returncode,hashes={n:h(out/n) for n in ['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz'] if (out/n).exists()});runs.append(row);status('running');assert p.returncode==0,(name,p.returncode)
 return out,row
status('running')
one,a=run('t1-probeoff',1,False);two,b=run('t1-probeon',1,True);four,c=run('t4-probeon',4,True)
assert a['hashes']==b['hashes']==c['hashes'],'Observer/thread simulation mismatch'
for n in ['gradient-episodes.csv','gradient-quality.csv','gradient-planning-quality.csv','gradient-contradictions.csv']:assert h(two/n)==h(four/n),n
x,d=run('continuation-t1',1,True,two/'final.game.gz',4096);y,e=run('continuation-t4',4,True,two/'final.game.gz',4096)
assert d['hashes']==e['hashes'],'Continuation thread mismatch'
for n in ['game.replay','gradient-episodes.csv','gradient-quality.csv','gradient-planning-quality.csv','gradient-contradictions.csv']:assert h(x/n)==h(y/n),n
status('passed');(B/'MAC_RUNTIME_PASSED.json').write_text(json.dumps(dict(source='2d4dc8e9e',observer_thread_and_continuation=True,cross_platform='pending Linux candidate'),indent=2)+'\n')
