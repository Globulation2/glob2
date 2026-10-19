import pathlib,json,subprocess,os,hashlib,datetime,shutil,traceback
B=pathlib.Path(__file__).resolve().parent;Q=B/'qualification';Q.mkdir(exist_ok=True);R=B/'runtime'
OLD=pathlib.Path('/Users/bradley/glob2/artifacts/gradient-unified-20261019/round-003/candidate-gen003/macos-package')
W=pathlib.Path('/Users/bradley/glob2/artifacts/gradient-unified-20261019/threshold-sweep-gen003/public-ready/warm/66294c58700ef1b4.game.gz')
ENV='GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI';h=lambda p:hashlib.file_digest(pathlib.Path(p).open('rb'),'sha256').hexdigest()
assert h(W)=='fa4506373ba0ea0bc24294a45582f0350c5f8f4ede2834fa84c1d1629dc5ce35'
build=json.loads((B/'build/status.json').read_text());assert build['status']=='built-research-only';assert not R.exists()
shutil.copytree(OLD,R,symlinks=True);shutil.copy2(build['binary'],R/'bin/glob2');(R/'runtime-provenance.json').unlink();(R/'research-runtime-provenance.json').write_text(json.dumps(build,indent=2)+'\n')
runs=[];invalid=[];simulation=['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz'];diagnostics=['gradient-episodes.csv','gradient-quality.csv','gradient-planning-quality.csv','gradient-contradictions.csv']
def status(s):(Q/'status.json').write_text(json.dumps(dict(status=s,runs=runs,invalid=invalid),indent=2)+'\n')
def run(name,binary,probe,value=None,fixed=None,ticks=17408):
 out=Q/name;out.mkdir();a=[str(binary),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--load-game',str(W),'--ticks',str(ticks),'--save','final','--telemetry','checksums','--gpu','off','--compute-telemetry','off','--compute-threads','1']
 if fixed is None:a+=['--write-replay']
 else:a+=['--resource-gradient-exploration',str(fixed)]
 if probe:a+=['--resource-gradient-probe']
 env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy');env.pop(ENV,None)
 if value is not None:env[ENV]=value
 (out/'command.json').write_text(json.dumps(dict(argv=a,environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER',ENV] if k in env}),indent=2)+'\n')
 with (out/'stdout.log').open('w') as log,(out/'stderr.log').open('w') as err:p=subprocess.run(a,cwd=R,env=env,stdout=log,stderr=err,timeout=300)
 row=dict(name=name,value=value,fixed=fixed,probe=probe,returncode=p.returncode,hashes={n:h(out/n) for n in simulation if (out/n).exists()});runs.append(row);status('running');assert p.returncode==0,(name,p.returncode,(out/'stderr.log').read_text()[-500:])
 if binary==R/'bin/glob2':
  result=json.loads((out/'result.json').read_text());policy=result['adaptive_gradient_policy'];expected=int(value) if value is not None else 128000
  assert policy['threshold_milli_direction_cost']==expected
  assert policy['research_input_sha256']==h(out/'research-gradient-threshold-input.json')
  assert policy['prediction_horizon_ticks']==128 and policy['minimum_interval']==8 and policy['demanded_age_cap'] is None
  if fixed is not None:assert policy['mode']=='explicit-per-field-fixed-interval-research' and result['resolved']['resource_gradient_collection']['intervals']==str(fixed)
  row['actual_policy']=policy
 return row
try:
 status('running')
 original=run('frozen128-on',OLD/'bin/glob2',True)
 defaultoff=run('research128-off',R/'bin/glob2',False,'128000');defaulton=run('research128-on',R/'bin/glob2',True,'128000')
 assert original['hashes']==defaultoff['hashes']==defaulton['hashes']
 for n in diagnostics:assert h(Q/'frozen128-on'/n)==h(Q/'research128-on'/n)
 for v in ['1000','4000',str(2**63-1)]:
  a=run('threshold-'+v+'-off',R/'bin/glob2',False,v);b=run('threshold-'+v+'-on',R/'bin/glob2',True,v);assert a['hashes']==b['hashes']
 for interval in [32,128]:
  a=run('fixed-'+str(interval)+'-off',R/'bin/glob2',False,fixed=interval);b=run('fixed-'+str(interval)+'-on',R/'bin/glob2',True,fixed=interval);assert a['hashes']==b['hashes'] and len(a['hashes'])==3
 for i,v in enumerate(['','0','-1','+1',' 1','1 ','x','1.0','9223372036854775808']):
  out=Q/('invalid-'+str(i));out.mkdir();env=dict(os.environ);env[ENV]=v
  p=subprocess.run([str(R/'bin/glob2'),'game','run','--load-game',str(W),'--ticks','16385','--output-dir',str(out),'--profile',str(out/'profile')],cwd=R,env=env,capture_output=True,text=True,timeout=30)
  (out/'stdout.log').write_text(p.stdout);(out/'stderr.log').write_text(p.stderr);assert p.returncode!=0 and 'Invalid research gradient threshold' in p.stdout+p.stderr
  invalid.append(dict(value=v,returncode=p.returncode))
 assert h(OLD/'bin/glob2')==build['frozen_base_elf'];status('PASS')
 gate=dict(status='PASS',research_only=True,source_base=build['source_base'],source_revision=build['source_base'],binary=str(R/'bin/glob2'),runtime=str(R),data_dir=str(R/'data'),binary_sha256=h(R/'bin/glob2'),patch_sha256=build['patch_sha256'],model_sha256=build['model_sha256'],header_sha256=build['header_sha256'],environment_key=ENV,threshold_units='milli-direction-cost',warm_save=dict(path=str(W),sha256=h(W),tick=16384),checks=dict(default_frozen_equivalence=True,original_diagnostics_byte_exact=True,lower_1_4_observer_parity=True,recovery_only_INT64MAX_observer_parity=True,fixed32_fixed128_probeOFF_ON_parity=True,strict_environment_validation=True,actual_metadata_sidecars=True,original_link_inputs_unchanged=True),runs=runs,invalid=invalid,limitations=['Fixed interval CLI still rejects replay recording; fixed parity uses world+snapshot+finalsave only','Artifact research binary only; no stable policy or production changes'],receipts=[dict(path=str(p),sha256=h(p)) for p in [B/'build/threshold-and-fixed-guards.patch',B/'build/compile-commands.json',B/'build/link-command.json',B/'build/reused-link-inputs.json',Q/'status.json']])
 for name in ['MACOS-RESEARCH-QUALIFICATION.json','QUALIFIED-MAC-THRESHOLD-RUNTIME.json']:
  (B/(name+'.tmp')).write_text(json.dumps(gate,indent=2)+'\n');(B/(name+'.tmp')).replace(B/name)
except BaseException:
 status('FAILED');(Q/'failure.log').write_text(traceback.format_exc());raise
