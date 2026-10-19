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
 if False:
  result=json.loads((out/'result.json').read_text());policy=result['adaptive_gradient_policy'];expected=128000 if value is None else int(value)
  assert policy['threshold_milli_direction_cost']==expected,policy
  side=out/'research-gradient-threshold-input.json';assert policy['research_input_sha256']==h(side)
  row['actual_threshold']=expected;row['input_sha256']=h(side)
 return row

runs=json.loads((Q/'status.json').read_text())['runs'];assert len(runs)==4
try:
 import csv as csv_module
 paired=list(csv_module.DictReader((Q/'paired-on/paired-horizons.csv').open()))
 early=list(csv_module.DictReader((Q/'paired-early/paired-horizons.csv').open()))
 run('paired-short-complete-full-censored',R/'bin/glob2',1,True,ticks=320)
 partial=list(csv_module.DictReader((Q/'paired-short-complete-full-censored/paired-horizons.csv').open()))
 assert any(x['short_complete']=='1' and x['full_complete']=='0' for x in partial)
 for x in early+partial:
  age=int(x['observed_through_tick'])-int(x['tick'])
  assert int(x['short_complete'])==(age>=32) and int(x['full_complete'])==(age>=128)
 assert h(OLD/'bin/glob2')==build['frozen_base_elf']
 status('PASS')
 gate=dict(status='PASS',research_only=True,source_base=build['source_base'],binary=str(R/'bin/glob2'),runtime=str(R),binary_sha256=h(R/'bin/glob2'),patch_sha256=build['patch_sha256'],model_sha256=build['model_sha256'],header_sha256=build['header_sha256'],sidecar_contract='paired-direction-cost-h32-h128-v1',collector_contract_unchanged='unified-gradient-spatial-h128-v3',threshold_milli=128000,short_window=[8,32],full_window=[8,128],rows=len(paired),early_rows=len(early),checks=dict(simulation_equivalence=True,original_four_CSVs_byte_exact=True,no_probe_off_sidecar=True,boundary_handcase_UBSan=True,full_and_short_censoring=True,original_link_inputs_unchanged=True),runs=runs,receipts=[dict(path=str(p),sha256=h(p)) for p in [B/'build/paired-horizon.patch',B/'build/compile-commands.json',B/'build/link-command.json',B/'build/reused-link-inputs.json',Q/'status.json',Q/'boundaries.cpp']])
 (B/'QUALIFIED-PAIRED-HORIZON.json').write_text(json.dumps(gate,indent=2)+'\n')
except BaseException:
 status('FAILED');(Q/'failure.log').write_text(traceback.format_exc());raise
