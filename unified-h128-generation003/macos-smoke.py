import pathlib,json,subprocess,os,hashlib,datetime
R=pathlib.Path('/Users/bradley/glob2/artifacts/gradient-unified-20261019');B=R/'round-003/candidate-gen003/macos-smoke';binary=pathlib.Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2/build/candidate-release/src/glob2');mp=R/'runtime-macos/map.map.gz';cwd=pathlib.Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2');job=json.loads((R/'runtime-macos/input-job.json').read_text());runs=[]
h=lambda p:hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def status(state): (B/'status.json').write_text(json.dumps(dict(status=state,source='4a2cf3dac9eb4eab0aab932307ca5096be953491',binary_sha256=h(binary),map_sha256=h(mp),runs=runs,updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()),indent=2)+'\n')
def run(name,threads,probe,load=None,ticks=1024):
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
status('passed')
