from pathlib import Path
import subprocess,os,json,concurrent.futures,time,sys
root=Path.cwd();out=root/'artifacts/resource-growth/remaining/generated-delay';master=out/'master-src'
cases=['river','swamp','crater-lakes','islands','rain-shadow','old-growth','braided-river','fjord-continent','stone-highlands','tidal-flats','canals','continents']
def run(case,stage):
 directory=out/stage/case;directory.mkdir(parents=True,exist_ok=True)
 env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib',GLOB2_GROWTH_GENERATED_CASE=case,GLOB2_GROWTH_GENERATED_TICKS='4096')
 if stage.startswith('master'):
  cwd=master;env['GLOB2_GROWTH_MASTER_OUTPUT']=str(directory/'results.json');casefilter='*generated master ecology*'
 else:
  cwd=root;env.update(GLOB2_GROWTH_GENERATED_OUTPUT=str(directory/'results.json'),GLOB2_GROWTH_GENERATED_DELAYS='1,2,3,4,8,12,16',GLOB2_GROWTH_GENERATED_WIDE='1',GLOB2_GROWTH_GENERATED_INPUT=str(out/'master'));casefilter='ResourceGrowthBenchmark/generated*'
 command=['python3','test/run_tests.py','--binary','engine','--no-display','-j1','--timeout','7200','--filter',casefilter,'--junit',str(directory/'junit.xml'),'--artifacts',str(directory/'test')]
 (directory/'command.json').write_text(json.dumps({'cwd':str(cwd),'environment':{k:v for k,v in env.items() if k.startswith('GLOB2_') or k=='LD_LIBRARY_PATH'},'command':command},indent=2)+'\n')
 start=time.time()
 with (directory/'run.log').open('w') as log:result=subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
 print(stage,case,result.returncode,round(time.time()-start,1),flush=True)
 if result.returncode:raise RuntimeError(f'{stage}/{case} failed; see log')
 return case
stage=sys.argv[1];selected=sys.argv[2:] or cases
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for f in concurrent.futures.as_completed([pool.submit(run,c,stage) for c in selected]):f.result()
