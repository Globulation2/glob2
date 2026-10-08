from pathlib import Path
import subprocess,shlex,json,sys,shutil
root=Path.cwd();out=root/'artifacts/resource-growth/remaining';commands=[]
lines=(out/'build.log').read_text().splitlines()
for p in ['artifacts/resource-growth/copy-policy/build-committed.log','artifacts/resource-growth/copy-policy/build.log','artifacts/resource-growth/simple/build.log','artifacts/resource-growth/simple/final-build.log']:
 lines=Path(p).read_text().splitlines()+lines

def compile(name,source,tag,flags=''):
 objname='build/linux/client/release/'+name[:-4]+'.o'
 if name.endswith('Test.cpp') or name.endswith('Benchmark.cpp'):objname='build/linux/client/release/test/engine-'+name[:-4].replace('/','_')+'.o'
 cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+objname+' ')))
 p=out/(tag+'-'+Path(name).name);p.write_text(source);obj=p.with_suffix('.o');cmd[cmd.index('-o')+1]=str(obj);cmd[-1]=str(p);cmd.insert(cmd.index('-c'),'-I'+str((root/name).parent));cmd[2:2]=shlex.split(flags)
 with (out/'variants-build.log').open('a') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 commands.append(cmd);return objname,str(obj)
def link(target,tag,objects,extra=()):
 cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o '+target+' ')));cmd[2]=str(out/tag);cmd=[objects.get(a,a) for a in cmd];cmd[3:3]=extra
 with (out/'variants-build.log').open('a') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 commands.append(cmd)

if __name__=='__main__':
 cache={}
 for tag in ['control','A','B','C','D','AB','ABCD']:
  overrides={}
  for name,opts in [('src/map/MapResourceState.cpp','AC'),('src/map/ResourceGrowth.cpp','B'),('src/engine/sim/snapshot/WorldCapture.cpp','CD')]:
   enabled=''.join(c for c in opts if c in tag and tag!='control');key=(name,enabled)
   if key not in cache:
    flags=' '.join(f'-DGROWTH_OPT_{c}={int(c in enabled)}' for c in opts)
    cache[key]=compile(name,(root/name).read_text(),(enabled or 'off')+'-'+Path(name).stem,flags)
   overrides.update([cache[key]])
  if '--tests-only' not in sys.argv:link('build/linux/client/release/src/glob2',tag,overrides)
  if '--engine-only' not in sys.argv:link('build/linux/client/release/test/glob2-engine-tests',tag+'-tests',overrides)
  print('built',tag,flush=True)
  (out/'build-commands.json').write_text(json.dumps(commands,indent=2))
 (out/'object-overrides.json').write_text(json.dumps({k[0]+':'+k[1]:v for k,v in cache.items()},indent=2))
