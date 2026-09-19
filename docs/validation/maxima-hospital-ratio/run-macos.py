"""Run the final six paired cases from tick-zero saves on the Mac lab."""
import concurrent.futures,gzip,json,pathlib,shutil,subprocess,time
ROOT=pathlib.Path('/Users/bradley/glob2-maxima-defense-lab');OUT=ROOT/'artifacts/defense-study';INPUT=OUT/'hospital-macos-initials';DEST=OUT/'hospital-macos-games';design=json.loads((INPUT/'design.json').read_text());DEST.mkdir(exist_ok=True)
(DEST/'design.json').write_text(json.dumps(design,indent=2))
for p in INPUT.glob('*/*/initial.game.gz'):p.with_suffix('').write_bytes(gzip.decompress(p.read_bytes()))
def play(job):
 v,c=job;dest=DEST/v/c['id']
 if (dest/'execution.json').exists() and json.loads((dest/'execution.json').read_text())['exit']==0:return
 dest.mkdir(parents=True,exist_ok=True)
 cmd=[str(OUT/'binaries'/('towers-lazy' if v=='control' else 'hospital-ratio')),'--run-game','--load-game',str(INPUT/v/c['id']/'initial.game'),'--ticks','60000','--save','initial','--save','final','--telemetry','team-timeline','--telemetry','maxima','--output-dir',str(dest)]
 (dest/'command.json').write_text(json.dumps(dict(command=cmd,platform='macOS arm64',setup=json.loads((INPUT/v/c['id']/'command.json').read_text())),indent=2));start=time.time()
 if shutil.disk_usage(OUT).free<2*1024**3:raise RuntimeError('Disk admission limit')
 with gzip.open(dest/'stdout.log.gz','wb',compresslevel=1) as out:
  with subprocess.Popen(cmd,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT) as p:shutil.copyfileobj(p.stdout,out);code=p.wait()
 for p in dest.glob('*.game'):
  with p.open('rb') as src,gzip.open(str(p)+'.gz','wb',compresslevel=1) as out:shutil.copyfileobj(src,out)
  p.unlink()
 (dest/'execution.json').write_text(json.dumps(dict(exit=code,seconds=time.time()-start,platform='macOS arm64')))
 if code:raise RuntimeError((dest,code))
 print(v,c['id'],'complete',flush=True)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for f in concurrent.futures.as_completed([pool.submit(play,(v,c)) for c in design['cases'] for v in design['variants']]):f.result()
