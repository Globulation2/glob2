from pathlib import Path
import hashlib,json,os,subprocess,shutil
root=Path.cwd();out=root/'artifacts/hiring-prewarm-top-candidates';build='build/darwin/client/release/client-full-direct-1111';commands=[]
for name,mode,filters in [('baseline-focused','baseline',['HiringPipeline/*','HiringPlanner/*','GradientPipeline/*']),('shortlist-handoff','shortlist',['HiringGradientPrewarmProbe/*']),('top-handoff','top',['HiringGradientPrewarmProbe/*'])]:
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env['GLOB2_HIRING_PREWARM']=mode
 cmd=['python3','test/run_tests.py','--build-dir',build,'--timeout','300','-j4','--junit',str(out/(name+'-junit.xml'))]
 for f in filters:cmd+=['--filter',f]
 print('RUN',name,flush=True)
 with (out/(name+'.log')).open('w') as f:r=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
 commands.append({'name':name,'command':cmd,'GLOB2_HIRING_PREWARM':mode,'exit_code':r.returncode});(out/'qualification.json').write_text(json.dumps(commands,indent=2))
 if r.returncode:raise RuntimeError(name+' failed; see log')
 print('PASS',name,flush=True)
source=out/'source';source.mkdir(exist_ok=True)
tracked=subprocess.check_output(['git','diff','--name-only'],text=True).splitlines();tracked.append('src/building/HiringGradientProbe.h')
manifest={}
for path in tracked:
 dest=source/path;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/path,dest);manifest[path]=hashlib.sha256(dest.read_bytes()).hexdigest()
(out/'source-sha256.json').write_text(json.dumps(manifest,indent=2));(out/'experiment.patch').write_bytes(subprocess.check_output(['git','diff','--binary']))
print('QUALIFICATION COMPLETE',flush=True)
