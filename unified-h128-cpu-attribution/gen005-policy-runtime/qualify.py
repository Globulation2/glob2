from pathlib import Path
import json,hashlib,subprocess,os,datetime,csv,traceback
P=Path(__file__).resolve().parent;C=P.parents[1];OLD=Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2');reference=C/'runtime-macos/t1-probeoff';binary=P/'glob2';h=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();build=json.loads((P/'BUILD-STATUS.json').read_text());assert build['status']=='BUILT';assert h(OLD/'build/candidate-release/src/glob2')=='3781b88988069b18b7588a50608c652aec2459a4ef81a30ae4c80777843a9236'
base=json.loads((reference/'command.json').read_text());runs=[];simfiles=['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz'];envname='GLOB2_RESEARCH_GRADIENT_INTERVAL'
def save(state,**extra):(P/'QUALIFICATION-STATUS.json').write_text(json.dumps(dict(status=state,runs=runs,source_revision=build['source_revision'],patch_sha256=build['patch_sha256'],binary_sha256=h(binary),utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**extra),indent=2)+'\n')
def run(name,value,probe,threads=1):
 out=P/'proofs'/name;out.mkdir(parents=True,exist_ok=False);argv=base.copy();argv[0]=str(binary)
 for flag,v in [('--output-dir',str(out)),('--profile',str(out/'profile')),('--compute-threads',str(threads))]:argv[argv.index(flag)+1]=v
 if probe:argv.append('--resource-gradient-probe')
 env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env.pop(envname,None)
 if value is not None:env[envname]=str(value)
 (out/'command.json').write_text(json.dumps(dict(argv=argv,cwd=str(OLD),env={envname:env.get(envname)},source_revision=build['source_revision'],model_sha256='0f6def629ad66ef0f789d0a8b60282560c4f65b83d9097bc664d040299a85561',research_only=True),indent=2)+'\n')
 with (out/'stdout.log').open('w') as stdout,(out/'stderr.log').open('w') as stderr:r=subprocess.run(argv,cwd=OLD,env=env,stdout=stdout,stderr=stderr,timeout=1800)
 assert r.returncode==0,(name,r.returncode);result=json.loads((out/'result.json').read_text());actual=result['adaptive_gradient_policy'];interval=value or 0;assert actual['research_fixed_interval_ticks']==interval and actual['research_override_active']==bool(interval) and actual['research_policy_runtime'];assert actual['quality_collection']==probe;assert actual['threshold_milli_direction_cost']==16000
 if not probe:assert not list(out.glob('gradient-*.csv')),'Observer/decisions output unexpectedly allocated'
 assert not (out/'gradient-decisions.csv').exists(),'No exploration CSV permitted'
 if probe:
  with (out/'gradient-episodes.csv').open() as f:episodes=list(csv.DictReader(f))
  assert episodes and all(int(e['behavior_interval'])==interval for e in episodes),'Actual fixed interval annotation missing'
 hashes={f:h(out/f) for f in simfiles};runs.append(dict(name=name,interval=value,probe=probe,threads=threads,hashes=hashes,episode_rows=len(episodes) if probe else 0));save('RUNNING');return hashes
try:
 os.nice(15);save('RUNNING')
 default=run('adaptive-default-off',None,False);on=run('adaptive-explicit0-on-t1',0,True);four=run('adaptive-explicit0-on-t4',0,True);assert default==on==four;assert default=={f:h(reference/f) for f in simfiles},'Default policy differs from immutable3781 reference'
 for interval in [8,32,128]:assert run(f'fixed{interval}-off',interval,False)==run(f'fixed{interval}-on',interval,True),'Fixed policy observer mismatch'
 invalid=[]
 for value in ['', '-1','12','32junk','18446744073709551616']:
  out=P/'invalid'/str(len(invalid));out.mkdir(parents=True);argv=[str(binary),'game','run','--output-dir',str(out),'--ticks','1','--player','numbi','--player','numbi'];env=dict(os.environ,**{envname:value})
  r=subprocess.run(argv,cwd=OLD,env=env,capture_output=True,text=True,timeout=30);assert r.returncode!=0 and envname in r.stderr+r.stdout;assert not list(out.glob('world.checksums')) and not list(out.glob('*.game.gz'));(out/'stdout.log').write_text(r.stdout);(out/'stderr.log').write_text(r.stderr);invalid.append(dict(value=value,returncode=r.returncode))
 out=P/'invalid/mixed';out.mkdir();argv=base.copy();argv[0]=str(binary);argv[argv.index('--output-dir')+1]=str(out);argv[argv.index('--profile')+1]=str(out/'profile');argv.remove('--write-replay');argv+=['--resource-gradient-probe','--resource-gradient-exploration','32'];r=subprocess.run(argv,cwd=OLD,env=dict(os.environ,**{envname:'32'}),capture_output=True,text=True,timeout=30);assert r.returncode!=0 and 'cannot combine' in r.stderr+r.stdout;(out/'stderr.log').write_text(r.stderr);invalid.append(dict(value='32+existing-exploration',returncode=r.returncode))
 assert h(OLD/'build/candidate-release/src/glob2')=='3781b88988069b18b7588a50608c652aec2459a4ef81a30ae4c80777843a9236'
 save('PASS',default_adaptive_matches_immutable3781=True,observer_thread_parity=True,fixed_off_on_parity=True,invalid_env_rejected=invalid,no_probe_or_decisions_csv_off=True,label_contract_unchanged=True,release_flags_match=True,compatible_assets=str(OLD/'data'),runtime_env=envname,allowed_values=[0,8,32,128],research_only=True,not_collection_qualification=True)
except BaseException:save('FAILED',error=traceback.format_exc());raise
