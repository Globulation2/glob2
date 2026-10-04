#!/usr/bin/env python3
"""Retained fixed-seed custom-rule qualification; temporary review evidence only."""
import argparse, concurrent.futures, gzip, hashlib, json, os, pathlib, subprocess, time, shutil
ROOT=pathlib.Path(__file__).resolve().parents[2]
PROFILES=[('standard',{}),('no-upgrades',{'noUpgrades':1}),('no-hunger',{'noHunger':1}),
 ('peaceful',{'peaceful':1}),('no-regrowth',{'noGrowth':1}),('maximum-scarcity',{'scarcity':3}),
 ('instant-construction',{'instantConstruction':1}),
 ('combat-modifiers',{'glassCannon':2,'fearless':1,'noPermadeath':1,'fortress':2}),
 ('combined-disabled',{'noUpgrades':1,'noHunger':1,'peaceful':1,'noGrowth':1,'stockpile':3})]
AIS=['numbi','castor','warrush','econo','nicowar','cortex','cabino','maxima']
def main():
 p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--engine-tests');p.add_argument('--output',required=True);p.add_argument('--slots',type=int,default=4);p.add_argument('--ticks',type=int,default=30000);p.add_argument('--mode',choices=['all','default','probe'],default='all');a=p.parse_args()
 output=pathlib.Path(a.output).resolve();output.mkdir(parents=True,exist_ok=True)
 maps=[ROOT/'maps/SmallForTwo.map.gz',ROOT/'maps/balanced_for_2.map.gz']
 jobs=[]
 for profile,(name,rules) in enumerate(PROFILES if a.mode=='all' else PROFILES[:1]):
  for i,ai in enumerate(AIS):
   for swap in range(2):
    players=[ai,AIS[(i+1)%len(AIS)]]
    if swap:players.reverse()
    jobs.append({'name':f'{name}-{i}-{swap}','profile':name,'rules':rules,'players':players,'seed':713+17*i,'map':str(maps[profile%2]),'script':None})
  if a.mode=='all':
   for swap in range(2):
    players=['javascript','numbi']
    if swap:players.reverse()
    jobs.append({'name':f'{name}-javascript-{swap}','profile':name,'rules':rules,'players':players,'seed':977,'map':str(maps[profile%2]),'script':str(ROOT/'examples/javascript/ai.js')})
 if a.mode=='probe':jobs=jobs[:1]+[dict(jobs[0],name='map-probe',map=str(maps[1]))]
 # Pin executables so an incremental build cannot replace a running tournament's inputs.
 source_binary=pathlib.Path(a.binary).resolve();shutil.copy2(source_binary,output/'glob2')
 binary=str(output/'glob2');test_binary=None
 if a.engine_tests:
  shutil.copy2(pathlib.Path(a.engine_tests).resolve(),output/'glob2-engine-tests');test_binary=str(output/'glob2-engine-tests')
 (output/'plan.json').write_text(json.dumps({'jobs':jobs,'ticks':a.ticks,'binary':binary,'binary_sha256':hashlib.sha256(pathlib.Path(binary).read_bytes()).hexdigest()},indent=2))
 def run(j):
  target=output/j['name'];target.mkdir(exist_ok=True)
  if (target/'qualification.json').exists():return json.loads((target/'qualification.json').read_text())
  command=[binary,'--run-game','--map-file',j['map'],'--game-seed',str(j['seed']),'--ticks',str(a.ticks),'--compute-threads','1','--compute-experiments','none','--output-dir',str(target),'--profile','rule-qualification-'+j['name'],'--save','initial','--save','final','--replay','true','--telemetry','checksums','--telemetry','team-timeline']
  for ai in j['players']:command+=['--player',ai]
  for k,v in sorted(j['rules'].items()):command+=['--rule',f'{k}={v}']
  if j['script']:command+=['--ai-script',f"{j['players'].index('javascript')}:{j['script']}"]
  env=dict(os.environ,GLOB2_USER_DIR=str(target/'user-profile'),GLOB2_TEST_AI_RULE_AUDIT='1')
  started=time.time()
  with (target/'run.log').open('w') as log:
   completed=subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=3600)
  record=dict(j,command=command,exit_code=completed.returncode,elapsed_seconds=time.time()-started)
  if (target/'result.json').exists():record['result']=json.loads((target/'result.json').read_text())
  if completed.returncode==0 and test_binary:
   replays=list(target.glob('*.replay'))
   assert len(replays)==1,replays
   audit=[test_binary,'--test-suite=AIRules','--test-case=retained tournament replay contains no unavailable orders','--no-breaks','--no-colors']
   with (target/'audit.log').open('w') as log:
    checked=subprocess.run(audit,cwd=ROOT,env=dict(env,GLOB2_RULE_REPLAY=str(replays[0])),stdout=log,stderr=subprocess.STDOUT,timeout=3600)
   record['audit_exit_code']=checked.returncode
  # Preserve exact tick records and telemetry in compressed form after auditing.
  for path in target.iterdir():
   if path.suffix in ('.checksums','.jsonl','.tsv'):
    with path.open('rb') as source,gzip.open(str(path)+'.gz','wb',compresslevel=6) as dest:
     import shutil;shutil.copyfileobj(source,dest)
    path.unlink()
  (target/'qualification.json').write_text(json.dumps(record,indent=2))
  print(j['name'],record['exit_code'],record.get('audit_exit_code'),flush=True)
  return record
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.slots) as executor:
  results=list(executor.map(run,jobs))
 (output/'qualification.json').write_text(json.dumps({'games':len(results),'native_games':sum(j['script'] is None for j in results),'javascript_games':sum(j['script'] is not None for j in results),'failures':[j['name'] for j in results if j['exit_code'] or j.get('audit_exit_code',0)],'results':results},indent=2))
if __name__=='__main__':main()
