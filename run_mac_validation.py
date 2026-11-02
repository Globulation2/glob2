import hashlib,json,os,shutil,subprocess,time
from pathlib import Path
root=Path.cwd();out=root/'artifacts/terrain-decoration-pr';build=root/'build/darwin/client/debug/dev-dev_fast-true-linker-auto'
while subprocess.run(['kill','-0','46804'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:time.sleep(2)
if 'scons: done building targets.' not in (out/'build-integrated.log').read_text():raise SystemExit('Build unsuccessful; no validation launched')
with (out/'build-final.log').open('w') as log:
 code=subprocess.run(['python3','tools/dev_build.py','-j4','build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2','engine-tests','unit-tests'],stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,PYTHONPATH=str(root))).returncode
if code:raise SystemExit('Final incremental build failed')
frozen=out/'mac-build';(frozen/'test').mkdir(parents=True,exist_ok=False);(frozen/'src').mkdir()
for name in ['src/glob2','test/glob2-engine-tests','test/glob2-unit-tests']:shutil.copy2(build/name,frozen/name)
if (build/'shaders').exists():shutil.copytree(build/'shaders',frozen/'shaders')
manifest={'source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'base':'726b37f573','configuration':'native macOS development dev_fast=1 -O0 -g1 linker=auto; not a performance measurement','binaries':{n:hashlib.sha256((frozen/n).read_bytes()).hexdigest() for n in ['src/glob2','test/glob2-engine-tests','test/glob2-unit-tests']}}
(out/'mac-manifest.json').write_text(json.dumps(manifest,indent=2))
command=['python3','test/run_tests.py','--build-dir',str(frozen),'--binary','engine','--timeout','300','-j','2','--display-jobs','1','--junit',str(out/'mac-tests.xml'),'--artifacts',str(out/'mac-tests')]
for pattern in ['TerrainValidation/decorated borders*','TerrainValidation/connected foliage*','TerrainValidation/terrain catalogue gallery*','TerrainPresentation/GPU terrain pages match*','TerrainMaterials/*','TurnEngineHarness/the committed match record*','RuntimeResources/frozen seeded resource compositions*']:command+=['--filter',pattern]
(out/'mac-command.json').write_text(json.dumps(command,indent=2))
with (out/'mac-tests.log').open('w') as log:code=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT).returncode
(out/'mac-validation-status.json').write_text(json.dumps({'exit':code},indent=2))

input=out/'initial191.game.gz';shutil.copy2('/Users/bradley/.codex/worktrees/04a5/glob2/artifacts/terrain-pr-review/linux/base-match/initial.game.gz',input)
cmd=[str(frozen/'src/glob2'),'game','run','--load-game',str(input),'--compute-threads','8','--ticks','4096','--telemetry','checksums','--output-dir',str(out/'mac-checksums')]
(out/'mac-checksum-command.json').write_text(json.dumps(cmd,indent=2))
with (out/'mac-checksums.log').open('w') as log:result=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=600).returncode
(out/'mac-checksums-status.json').write_text(json.dumps({'exit':result,'input_sha256':hashlib.sha256(input.read_bytes()).hexdigest()},indent=2))
