from pathlib import Path
import os,subprocess,json,hashlib
root=Path.cwd();out=root/'artifacts/resource-growth/remaining/delay-ablation'
manifest={'base_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'delays':[1,2,3,4,8,12,16],'seeds':list(range(1,21)),'ticks':512,'map_side':64,'scenarios':['dense','sparse','multi','saturated','blocked','disabled'],'variants':['immediate-reference','owner','shared'],'design':'Same generated inputs/seeds; only delay changes. Owner/shared exact per-tick checksums/statistics. Reference repeated for fixture stability, not extra independent replicates. Diagnostic terminal flush separately applies pending proposals, no new computation. No timing claim.','binary_sha256':hashlib.sha256((root/'build/linux/client/release/test/glob2-engine-tests').read_bytes()).hexdigest(),'source_patch_sha256':hashlib.sha256(subprocess.check_output(['git','diff'])).hexdigest()}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'source.patch').write_bytes(subprocess.check_output(['git','diff']))
for delay in manifest['delays']:
 env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib',GLOB2_GROWTH_PLAYER_FREE_DELAY=str(delay),GLOB2_GROWTH_PLAYER_FREE_OUTPUT=str(out/f'delay-{delay}.json'))
 cmd=['python3','test/run_tests.py','--binary','engine','--no-display','-j1','--timeout','600','--filter','ResourceGrowthBenchmark/player-free*','--artifacts',str(out/f'delay-{delay}'),'--junit',str(out/f'delay-{delay}.xml')]
 with (out/f'delay-{delay}.log').open('w') as log:subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 print(f'Delay {delay}: 360 runs passed',flush=True)
