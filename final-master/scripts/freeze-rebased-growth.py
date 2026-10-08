import subprocess,json,hashlib,platform
from pathlib import Path
r=Path.cwd();b=r/'artifacts/resource-growth/final-master';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();exe=r/'build/linux/client/release/src/glob2';master=b/'master-src';ref=master/'build/linux/client/release/src/glob2'
assert not subprocess.check_output(['git','status','--porcelain'],text=True).strip()
libs={}
for e in [exe,ref]:
 for line in subprocess.check_output(['ldd',str(e)],text=True).splitlines():
  chunks=line.split('=>')
  if len(chunks)==2:
   p=Path(chunks[1].strip().split()[0])
   if p.is_file():libs[str(p.resolve())]=sha(p)
freeze={'branch_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'master_revision':(b/'master-revision.txt').read_text().strip(),'platform':platform.platform(),'compiler':subprocess.check_output(['g++','--version'],text=True),'flags':'release=1 server=0 optimized_assets=0;GCC O3;CCACHE=1;GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix;GLOB2_RECORDING_PREFIX=/home/bradley/glob2-terrain-art2/build/linux/client/release/recording/prefix','binaries':{str(p):sha(p) for p in [exe,ref]},'libraries_sha256':libs,'fixtures':json.loads((b/'manifest.json').read_text()),'master_instrumentation':'Only CLI tick duration vector and percentile reporting, identical to branch, no simulation changes.'}
(b/'freeze.json').write_text(json.dumps(freeze,indent=2)+'\n');(b/'master-instrumentation.patch').write_bytes(subprocess.check_output(['git','-C',str(master),'diff']))
print(json.dumps(freeze['binaries'],indent=2))
