import os, subprocess, time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
os.chdir(root)
log=Path('/tmp/glob2-fertility-build.log')
while True:
    text=log.read_text()
    if 'scons: done building targets.' in text: break
    if 'building terminated because of errors' in text: raise SystemExit('Build failed')
    time.sleep(5)
env=dict(os.environ,GLOB2_BUILD_DIR='build/darwin/client/debug/dev-dev_fast-true-linker-auto')
commands=[
 ['python3','test/run_tests.py','--update-fixtures','--filter','TurnEngineHarness/the committed*','--junit','artifacts/fertility-128/golden-update.xml'],
 ['python3','test/run_tests.py','--filter','TerrainProperties/*','--filter','TerrainRuntime/*','--filter','FertilityField/*','--filter','TerrainEcology/*','--filter','TurnEngineHarness/*','--exclude-tag','benchmark','--junit','artifacts/fertility-128/focused.xml'],
 ['python3','test/check_sim_revision.py','--base','origin/master'],
]
for i,cmd in enumerate(commands):
    print('Running:',cmd,flush=True)
    with open(f'artifacts/fertility-128/check-{i}.log','w') as out:
        result=subprocess.run(cmd,env=env,stdout=out,stderr=subprocess.STDOUT)
    print('Result:',result.returncode,flush=True)
    if result.returncode: raise SystemExit(result.returncode)
print('VALIDATION PASSED',flush=True)
