#!/usr/bin/env python3
import argparse,concurrent.futures,hashlib,json,os,pathlib,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--binary',required=True);p.add_argument('--input',required=True);p.add_argument('--output',required=True);a=p.parse_args()
source=pathlib.Path(a.input).resolve();output=pathlib.Path(a.output).resolve();output.mkdir(parents=True,exist_ok=True)
binary=str(pathlib.Path(a.binary).resolve())
def run(initial):
 name=initial.parent.name;target=output/name;target.mkdir(exist_ok=True)
 command=[binary,'--run-game','--load-game',str(initial),'--ticks','4096','--compute-threads','1','--compute-experiments','none','--output-dir',str(target),'--save','final','--telemetry','checksums','--telemetry','team-timeline']
 with (target/'run.log').open('w') as log:r=subprocess.run(command,cwd=ROOT,env=dict(os.environ,GLOB2_TEST_AI_RULE_AUDIT='1'),stdout=log,stderr=subprocess.STDOUT,timeout=300)
 result={'name':name,'command':command,'exit_code':r.returncode,'initial_sha256':hashlib.sha256(initial.read_bytes()).hexdigest()}
 trace=target/'game.replay.checksums'
 if trace.exists():result['trace_sha256']=hashlib.sha256(trace.read_bytes()).hexdigest()
 (target/'validation.json').write_text(json.dumps(result,indent=2));return result
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(run,sorted(source.glob('*/initial.game.gz'))))
(output/'validation.json').write_text(json.dumps({'binary_sha256':hashlib.sha256(pathlib.Path(binary).read_bytes()).hexdigest(),'results':results},indent=2))
print(json.dumps(results,indent=2))
