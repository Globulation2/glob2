#!/usr/bin/env python3
"""Prepare new post-matrix replication manifests only; never launch games/builds.
Untouched baseline vs V1/V2 and later combined. Prior evidence stays immutable.
"""
import argparse
import hashlib
import json
from pathlib import Path

ART=Path(__file__).resolve().parent
ROOT=ART.parents[1]
MASKS=[[0,1,2,16,17,18],[3,4,5,19,20,21],[6,7,8,22,23,24],[9,10,11,25,26,27]]

def load(p):return json.loads(Path(p).read_text())
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for data in iter(lambda:f.read(1024*1024),b''):h.update(data)
 return h.hexdigest()
def write(p,v):
 p.parent.mkdir(parents=True,exist_ok=True)
 p.write_text(json.dumps(v,indent=2)+'\n')

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--v2-metadata',type=Path)
 p.add_argument('--combined-metadata',type=Path,help='Later combined frozen release metadata; unbuilt remains pending')
 args=p.parse_args()
 acceptance=load(ART/'acceptance-plan.json');campaign=load(ART/'campaign.json');variants=load(ART/'variant-builds.json')
 out=ART/'telemetry-replication'
 factors={'telemetry-v1':dict(state='complete',binary=variants['variants']['telemetry']['binary'],root=variants['variants']['telemetry']['root'],binary_sha256=variants['variants']['telemetry']['binary_sha256']),
          'telemetry-v2':load(args.v2_metadata) if args.v2_metadata else dict(state='pending build/metadata'),
          'combined-later':load(args.combined_metadata) if args.combined_metadata else dict(state='pending freeze/build/metadata')}
 for factor,data in factors.items():
  if data['state']=='complete' and sha(data['binary'])!=data['binary_sha256']:raise ValueError('built binary changed: '+factor)
 baseline=dict(binary=campaign['baseline_binary'],root=campaign['baseline_source'],binary_sha256=campaign['baseline_binary_sha256'])
 if sha(baseline['binary'])!=baseline['binary_sha256']:raise ValueError('untouched baseline changed')
 jobs=[];reused=[];preparation=[];fixture_index=0
 original_jobs={j['scenario']+'-'+str(j['seed']):j for j in load(ART/'performance/worklist.json') if j['factor']=='telemetry-t3-gpuoff'}
 for scenario in acceptance['scenario_design']:
  seeds=list(acceptance['new_game_seeds'])
  if scenario['id']=='large-mixed':seeds.append(acceptance['holdout_game_seed'])
  for seed in seeds:
   key=scenario['id']+'-'+str(seed)
   slot=MASKS[fixture_index%4];fixture_index+=1
   original=ART/'correctness'/key/'fixture.json'
   if not original.exists():original=out/'fixtures'/key/'fixture.json'
   if original.exists():
    fixture=load(original);path=Path(fixture['path']);expected=fixture['header']['gzip_sha256']
    if sha(path)!=expected:raise ValueError('frozen fixture changed')
    start=fixture['start_tick'];fixture_state='verified existing fixture'
   else:
    path=out/'fixtures'/key/'initial.game.gz';expected=None;start=0;fixture_state='pending baseline creation and geometry/controller/seed/start validation'
    maps={m['id']:m for m in load(ART/'map-preparation.json')};m=maps[scenario['id']]
    command=['taskset','-c','12-14,28-30',baseline['binary'],'game','run','--map-file',m['path'],'--game-seed',str(seed),'--ticks','1','--compute-threads','1','--gpu','off','--save','initial','--telemetry','checksums','--output-dir',str(path.parent)]
    command += [token for ai in scenario['ais'] for token in ('--player',ai)]
    preparation.append(dict(id=key,command=command,frozen_map_sha256=m['sha256'],expected_width=scenario['width'],expected_height=scenario['height'],expected_teams=len(scenario['ais']),expected_ais=scenario['ais'],seed=seed,start_tick=0,validation='MapHeader/mapOffset dimensions; result players/team/seed; save SHA; first GCS tick0; no timing until sealed'))
   selected=list(factors) if scenario['id']=='large-mixed' else ['telemetry-v2','combined-later']
   if scenario['id']!='large-mixed':
    old=original_jobs[key];passed=Path(old['output'])/'campaign-passed.json';audit=Path(old['output'])/'input-verification.json'
    if not load(passed)['passed'] or not load(audit)['inputs_unchanged']:raise ValueError('reused V1 job not verified')
    reused.append(dict(job=old['id'],source=old['output'],passed_sha256=sha(passed),audit_sha256=sha(audit),scope='Original V1 paired3 evidence retained; not independent fresh replication'))
   for factor in selected:
    identifier=key+'-'+factor;repeats=6 if scenario['id']=='large-mixed' else 3
    manifest=dict(scenarios=[dict(id=key,start_tick=start,fixture_sha256={str(path):expected},args=['--load-game',str(path),'--ticks',str(scenario['ticks']),'--compute-threads','3','--gpu','off','--write-replay'])])
    manifest_path=out/'manifests'/f'{identifier}.json';write(manifest_path,manifest)
    data=factors[factor]
    ready=data['state']=='complete' and expected is not None
    jobs.append(dict(id=identifier,factor=factor,scenario=scenario['id'],seed=seed,repeats=repeats,discarded_warmup_pairs=1,manifest=str(manifest_path),manifest_sha256=sha(manifest_path),output=str(out/'runs'/identifier),baseline=baseline,candidate=data,fixture_state=fixture_state,ready=ready,slot=slot,ordering='Alternating paired order;6 repeats balanced3/3;3 repeats2/1. One fixed mask for all variants/repeats.',launch='pending root checkpoint/build/correctness gates; no compilers overlap',early_end='Retain native outcomes and fail; root must explicitly choose matched shorter window; never silently truncate'))
 plan=dict(state='plan-only; no child launched',baseline=baseline,factors=factors,jobs=jobs,reused_v1_jobs=reused,fixture_preparation=preparation,post_matrix_trigger='Large-mixed V1 credible CPU+6.61% andwall+6.32% despite RSS reduction; independently replicate rather than waive threshold.',targeted_seeds=acceptance['new_game_seeds']+[acceptance['holdout_game_seed']],targeted_repeats=6,allmap_seed_count=2,allmap_repeats=3,total_new_jobs=len(jobs),analysis='Two-level seed/paired-repeat log bootstrap per layout; no universal speed claim; old raw data immutable; RSS includes loading/replay.',source_sha256=sha(Path(__file__)))
 write(out/'plan.json',plan)
 print(json.dumps(dict(new_jobs=len(jobs),reused_v1=len(reused),holdout_preparations=len(preparation),targeted_seeds=plan['targeted_seeds'],ready_jobs=sum(j['ready'] for j in jobs))))
if __name__=='__main__':main()
