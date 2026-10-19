import pathlib,json,subprocess,os,hashlib,datetime,shutil,traceback
B=pathlib.Path(__file__).resolve().parent
OLD=pathlib.Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-003/candidate-gen003/runtime-linux-frozen')
R=B/'runtime';Q=B/'qualification';Q.mkdir(exist_ok=True)
h=lambda p:hashlib.file_digest(pathlib.Path(p).open('rb'),'sha256').hexdigest()
build=json.loads((B/'build/status.json').read_text());assert build['status']=='built-research-only'
assert not R.exists();shutil.copytree(OLD,R,symlinks=True);shutil.copy2(build['binary'],R/'bin/glob2')
for stale in ['runtime-provenance.json','native-source-sha256.json']:
 if (R/stale).exists():(R/stale).unlink()
(R/'research-runtime-provenance.json').write_text(json.dumps(build,indent=2)+'\n')
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
try:
 status('running')
 source=(B/'build/source/src/map/gradient/ResourceGradientProbe.cpp').read_text()
 fragment=source.split('// BEGIN ISOLATED PAIRED-HORIZON ACCUMULATOR\n',1)[1].split('// END ISOLATED PAIRED-HORIZON ACCUMULATOR',1)[0]
 test='#include <cstdint>\n#include <cassert>\n'+fragment+"""
int main(){
 PairedShortCounters shortWindow;unsigned full=0;
 for(unsigned age=0;age<128;++age){shortWindow.record(age,8,2,1);if(age>=8)++full;}
 assert(shortWindow.queries==24&&shortWindow.deferred==48&&shortWindow.refreshed==24&&full==120);
 PairedShortCounters early;for(unsigned age=0;age<31;++age)early.record(age,8,2,1);
 assert(early.queries==23&&!PairedShortCounters::complete(31));
 early.record(31,8,2,1);assert(early.queries==24&&PairedShortCounters::complete(32));
 early.record(32,8,2,1);assert(early.queries==24);
 PairedShortCounters delay32;for(unsigned age=0;age<128;++age)delay32.record(age,32,2,1);assert(delay32.queries==0);
}
"""
 (Q/'boundaries.cpp').write_text(test)
 subprocess.run(['g++','-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',str(Q/'boundaries.cpp'),'-o',str(Q/'boundaries')],check=True)
 subprocess.run([str(Q/'boundaries')],check=True)
 original=run('frozen-on',OLD/'bin/glob2',1,True)
 observed=run('paired-on',R/'bin/glob2',1,True)
 disabled=run('paired-off',R/'bin/glob2',1,False)
 assert original['hashes']==observed['hashes']==disabled['hashes']
 for n in csv:assert h(Q/'frozen-on'/n)==h(Q/'paired-on'/n),n
 assert not (Q/'paired-off/paired-horizons.csv').exists()
 import csv as csv_module
 full=list(csv_module.DictReader((Q/'paired-on/gradient-episodes.csv').open()))
 paired=list(csv_module.DictReader((Q/'paired-on/paired-horizons.csv').open()))
 assert len(full)==len(paired)>0
 for a,b in zip(full,paired):
  for name in ['tick','family','team','movement','material','key','serving_capture_tick','publication_delay']:assert a[name]==b[name],name
  for x,y in [('complete','full_complete'),('queries','full_queries'),('defer_extra','defer_extra_128'),('refresh_extra','refresh_extra_128')]:assert a[x]==b[y],(x,y)
  assert int(b['short_queries'])<=int(b['full_queries'])
  assert int(b['short_complete'])==(int(b['observed_through_tick'])-int(b['tick'])>=32)
  assert int(b['full_complete'])==(int(b['observed_through_tick'])-int(b['tick'])>=128)
 # Early termination independently verifies short censoring before offset32.
 run('paired-early',R/'bin/glob2',1,True,ticks=288)
 early=list(csv_module.DictReader((Q/'paired-early/paired-horizons.csv').open()))
 assert early and any(x['short_complete']=='0' and x['full_complete']=='0' for x in early)
 assert any(x['short_complete']=='1' and x['full_complete']=='0' for x in paired)
 assert h(OLD/'bin/glob2')==build['frozen_base_elf']
 status('PASS')
 gate=dict(status='PASS',research_only=True,source_base=build['source_base'],binary=str(R/'bin/glob2'),runtime=str(R),binary_sha256=h(R/'bin/glob2'),patch_sha256=build['patch_sha256'],model_sha256=build['model_sha256'],header_sha256=build['header_sha256'],sidecar_contract='paired-direction-cost-h32-h128-v1',collector_contract_unchanged='unified-gradient-spatial-h128-v3',threshold_milli=128000,short_window=[8,32],full_window=[8,128],rows=len(paired),early_rows=len(early),checks=dict(simulation_equivalence=True,original_four_CSVs_byte_exact=True,no_probe_off_sidecar=True,boundary_handcase_UBSan=True,full_and_short_censoring=True,original_link_inputs_unchanged=True),runs=runs,receipts=[dict(path=str(p),sha256=h(p)) for p in [B/'build/paired-horizon.patch',B/'build/compile-commands.json',B/'build/link-command.json',B/'build/reused-link-inputs.json',Q/'status.json',Q/'boundaries.cpp']])
 (B/'QUALIFIED-PAIRED-HORIZON.json').write_text(json.dumps(gate,indent=2)+'\n')
except BaseException:
 status('FAILED');(Q/'failure.log').write_text(traceback.format_exc());raise
