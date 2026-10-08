from pathlib import Path
import os,subprocess
p=Path('artifacts/resource-growth/remaining/seed-rule').resolve()
# Sequential reproduction is sufficient; these are ecology/correctness runs.
for variant in ['baseline','candidate']:
 env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib',GLOB2_SEED_RULE_OUTPUT=str(p/f'{variant}.json'))
 env.pop('GLOB2_SEED_RULE_CANDIDATE',None)
 env.pop('GLOB2_SEED_RULE_PILOT',None)
 if variant=='candidate':env['GLOB2_SEED_RULE_CANDIDATE']='1'
 with (p/f'{variant}.log').open('w') as log:
  subprocess.run([str(p/variant),'-tc=one per material seed rules*'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 if variant=='candidate':
  with (p/'correctness.log').open('w') as log:
   subprocess.run([str(p/variant),'-tc=experimental seed marker*'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
