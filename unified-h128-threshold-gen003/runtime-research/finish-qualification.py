import pathlib,json,subprocess,os,hashlib,datetime,shutil,traceback
B=pathlib.Path(__file__).resolve().parent
OLD=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-003/candidate-gen003/runtime-linux-frozen')
R=B/'runtime';Q=B/'qualification';Q.mkdir(exist_ok=True)
h=lambda p:hashlib.file_digest(pathlib.Path(p).open('rb'),'sha256').hexdigest()
build=json.loads((B/'build/status.json').read_text());assert build['status']=='built-research-only'
job=json.loads((B/'input-job.json').read_text());runs=[];invalid=[]
ENV='GLOB2_RESEARCH_GRADIENT_THRESHOLD_MILLI';files=['world.checksums','snapshot.checksums','game.replay.checksums','final.game.gz'];csv=['gradient-episodes.csv','gradient-quality.csv','gradient-planning-quality.csv','gradient-contradictions.csv']
def status(s):
 (Q/'status.json').write_text(json.dumps(dict(status=s,runs=runs,invalid=invalid),indent=2)+'\n')
def run(name,binary,threads,probe,value=None,ticks=1024):
 out=Q/name;out.mkdir();a=['taskset','-c','4,5,8,10','nice','-n','19',str(binary),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--ticks',str(ticks),'--save','final','--save','every:512','--telemetry','checksums','--write-replay','--gpu','off','--compute-telemetry','off','--compute-threads',str(threads),'--map-file',str(B/'map.map.gz'),'--game-seed',str(job['seeds']['game'])]
 for p in job['config']['players']:a+=['--player',p]
 if probe:a+=['--resource-gradient-probe']
 env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',LD_LIBRARY_PATH=str(R/'lib/glob2'));env.pop(ENV,None)
 if value is not None:env[ENV]=value
 (out/'command.json').write_text(json.dumps(dict(argv=a,environment={k:env[k] for k in ['SDL_VIDEODRIVER','SDL_AUDIODRIVER','LD_LIBRARY_PATH',ENV] if k in env}),indent=2)+'\n')
 with (out/'stdout.log').open('w') as log,(out/'stderr.log').open('w') as err:p=subprocess.run(a,cwd=R,env=env,stdout=log,stderr=err,timeout=300)
 row=dict(name=name,value=value,probe=probe,threads=threads,returncode=p.returncode,hashes={n:h(out/n) for n in files if (out/n).exists()});runs.append(row);status('running');assert p.returncode==0,(name,p.returncode,(out/'stderr.log').read_text()[-300:]);assert len(row['hashes'])==4
 if binary==R/'bin/glob2':
  result=json.loads((out/'result.json').read_text());policy=result['adaptive_gradient_policy'];expected=128000 if value is None else int(value)
  assert policy['threshold_milli_direction_cost']==expected,policy
  side=out/'research-gradient-threshold-input.json';assert policy['research_input_sha256']==h(side)
  row['actual_threshold']=expected;row['input_sha256']=h(side)
 return row

runs=json.loads((Q/'status.json').read_text())['runs'];assert len(runs)==12
assert len({json.dumps(x['hashes'],sort_keys=True) for x in runs[:6]})==1
assert runs[6]['hashes']==runs[7]['hashes'] and runs[8]['hashes']==runs[9]['hashes']

if (Q/'invalid-0').exists(): (Q/'invalid-0').rename(Q/'invalid-0-invalid-fixture-first-attempt')

try:
 for i,v in enumerate(['','0','-1','+128000',' 128000','128000 ','junk','1.5','9223372036854775808']):
  out=Q/('invalid-'+str(i));out.mkdir();env=dict(os.environ,LD_LIBRARY_PATH=str(R/'lib/glob2'));env[ENV]=v
  a=[str(R/'bin/glob2'),'game','run','--output-dir',str(out),'--profile',str(out/'profile'),'--map-file',str(B/'map.map.gz'),'--ticks','8','--game-seed',str(job['seeds']['game'])]+[item for player in job['config']['players'] for item in ['--player',player]]
  p=subprocess.run(a,cwd=R,env=env,capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(p.stdout);(out/'stderr.log').write_text(p.stderr)
  assert p.returncode!=0 and 'Invalid research gradient threshold' in p.stdout+p.stderr,(v,p.returncode,p.stderr)
  invalid.append(dict(value=v,returncode=p.returncode));status('running')
 assert h(OLD/'bin/glob2')==build['frozen_base_elf']
 status('PASS')
 gate=dict(status='PASS',research_only=True,source_base=build['source_base'],binary=str(R/'bin/glob2'),runtime=str(R),binary_sha256=h(R/'bin/glob2'),patch_sha256=build['patch_sha256'],model_sha256=build['model_sha256'],header_sha256=build['header_sha256'],environment_key=ENV,threshold_units='milli-direction-cost',tested_values=[1000,4000,128000,2**63-1],checks=dict(default_six_equivalence=True,lower_1_4_observer_parity=True,strict_invalid_environment=True,actual_metadata_sidecars=True,original_link_inputs_unchanged=True),runs=runs,invalid=invalid,limitations=['Artifact-only research engine; no production promotion or benchmark timing claim','Only threshold comparison and reporting changed; H128 model/targets unchanged'],receipts=[dict(path=str(p),sha256=h(p)) for p in [B/'build/threshold-only.patch',B/'build/compile-commands.json',B/'build/link-command.json',B/'build/reused-link-inputs.json',Q/'status.json']])
 (B/'QUALIFIED-THRESHOLD-RUNTIME.json').write_text(json.dumps(gate,indent=2)+'\n')
except BaseException:
 status('FAILED');(Q/'failure.log').write_text(traceback.format_exc());raise
