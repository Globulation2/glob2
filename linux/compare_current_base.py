from pathlib import Path
import hashlib,json,os,shutil,subprocess,time
root=Path.cwd();out=root/'artifacts/terrain-decoration-pr/linux';render='0be5f6a9f2c32b9d35c0370ee8b3a3b8136be1e6';base='726b37f573d2b1401033b5536562c7ee92b4050d'
for attempt in range(1200):
 p=out/'render-0be5-results.json'
 if p.exists():break
 time.sleep(2)
else:raise SystemExit('Render verification prerequisite timed out')
if any(v!=0 for k,v in json.loads(p.read_text()).items() if k!='goldens-0be5'):raise SystemExit('Render tests failed; no baseline checkout performed')
if subprocess.check_output(['git','status','--porcelain'],text=True).strip():raise SystemExit('Unexpected source edits; leaving checkout alone')
env=os.environ.copy();sdk=Path('/home/bradley/glob2-terrain-review-05fb152deb/artifacts/terrain-review/sdk');env.update(GLOB2_SDL3_PREFIX=str(sdk),PKG_CONFIG_PATH=str(sdk/'lib/pkgconfig'),LD_LIBRARY_PATH=str(sdk/'lib'),GLOB2_GLSLANG=str(out/'glslang-tools/StandAlone/glslang'),PYTHONPATH=str(root))
def run(label,cmd,timeout=1800):return subprocess.call(['python3',str(out/'run_bounded.py'),'--label',label,'--timeout',str(timeout),'--']+cmd,env=env)
input=out/'inputs/initial191.game.gz';input.parent.mkdir(exist_ok=True);shutil.copy2('/home/bradley/glob2-terrain-review-05fb152deb/artifacts/terrain-review/final/base-match/initial.game.gz',input)
results={}
try:
 subprocess.run(['git','checkout','--detach',base],check=True)
 results['base-build']=run('base726-build',['scons','release=1','CXX=g++-13','CC=gcc-13','-j16','build/linux/client/release/src/glob2','engine-tests'])
 if results['base-build']:raise RuntimeError('Base game build failed')
 results['base-resource-golden']=run('base726-resource-golden',['python3','test/run_tests.py','--build-dir','build/linux/client/release','--binary','engine','--no-display','--timeout','300','-j1','--filter','RuntimeResources/frozen seeded resource*','--junit',str(out/'base726-resource-golden.xml'),'--artifacts',str(out/'base726-resource-golden')],timeout=600)
 frozen=out/'frozen-base726/src';frozen.mkdir(parents=True,exist_ok=True);shutil.copy2(root/'build/linux/client/release/src/glob2',frozen/'glob2')
 for name,binary in [('base726',frozen/'glob2'),('render0be5',out/'frozen-0be5/src/glob2')]:
  results[name]=run(name+'-checksums',[str(binary),'game','run','--load-game',str(input),'--compute-threads','8','--ticks','4096','--telemetry','checksums','--output-dir',str(out/(name+'-checksums'))],timeout=600)
  if results[name]:raise RuntimeError(name+' run failed')
 rows={}
 for f in ['world.checksums','snapshot.checksums']:
  a=(out/'base726-checksums'/f).read_bytes();b=(out/'render0be5-checksums'/f).read_bytes();rows[f]={'identical':a==b,'rows':len(a.splitlines()),'sha256':hashlib.sha256(a).hexdigest()}
 report={'base':base,'render':render,'input_sha256':hashlib.sha256(input.read_bytes()).hexdigest(),'traces':rows,'identical':all(row['identical'] for row in rows.values()),'results':results};(out/'current-base-comparison.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
finally:
 subprocess.run(['git','fetch','origin','codex/terrain-decoration-borders'],check=True)
 subprocess.run(['git','checkout','--detach','659ae9c956'],check=True)
 results['restore-render-build']=run('restore-render-build',['scons','release=1','CXX=g++-13','CC=gcc-13','-j16','build/linux/client/release/src/glob2','engine-tests','unit-tests'])
 (out/'base-comparison-steps.json').write_text(json.dumps(results,indent=2))
