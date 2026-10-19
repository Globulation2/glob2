import pathlib,json,subprocess,os,hashlib,datetime
R=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019');B=R/'round-005/candidate-gen005/runtime-linux';binary=B.parent/'linux/frozen/glob2';mp=B/'map.map.gz';cwd=pathlib.Path('/home/bradley/glob2-gradient-unified-h128');job=json.loads((B/'input-job.json').read_text());runs=[]
h=lambda p:hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
def status(state): (B/'status.json').write_text(json.dumps(dict(status=state,source='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e',research_candidate_only=True,binary_sha256=h(binary),map_sha256=h(mp),runs=runs,updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()),indent=2)+'\n')
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
warm=R/'round-004/candidate-gen004/runtime-linux-fix2/mature-t1-probeoff/final.game.gz'
assert h(warm)=='c7fc30db59440d54060ce8d665fd73ba30f386bbaf06d516ea31df2e6f2fb4be'
w0,m0=run('old179-mature-t1-probeoff',1,False,warm,18432)
continued,cr=run('new180-continuation-t1',1,False,w0/'checkpoint-17920.game.gz',18432)
assert cr['hashes']['final.game.gz']==m0['hashes']['final.game.gz'],'New180 finalsave continuity mismatch'
continuity={}
for n in ['world.checksums','snapshot.checksums']:
 expected=[line for line in (w0/n).read_text().splitlines() if int(line.split()[0])>=17920]
 actual=(continued/n).read_text().splitlines();assert expected==actual,(n,'continuation mismatch');continuity[n]=len(actual)
for out in [one,two,four,w0,continued]:assert json.loads((out/'result.json').read_text())['adaptive_gradient_policy']['threshold_milli_direction_cost']==16000
(B/'MATURE-CONTINUITY.json').write_text(json.dumps(dict(status='PASS',old179_warm_sha256=h(warm),new180_checkpoint_sha256=h(w0/'checkpoint-17920.game.gz'),finalsave_sha256=cr['hashes']['final.game.gz'],matching_tick_lines=continuity,threshold_milli=16000),indent=2)+'\n')
status('passed')
