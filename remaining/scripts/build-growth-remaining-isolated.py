from pathlib import Path
import subprocess,shlex,json,sys
root=Path.cwd();out=root/'artifacts/resource-growth/remaining';commands=[]
lines=[]
for name in ['copy-policy/build-committed.log','copy-policy/build.log','simple/build.log','simple/final-build.log','remaining/build.log','remaining/build-tests-final.log','remaining/build-isolated.log']:
 p=root/'artifacts/resource-growth'/name
 if p.exists():lines+=p.read_text().splitlines()
c_templates=json.load(open(out/'c-layout-link-templates.json'))
def family(tag):return out/'c-layout-src' if 'C' in tag else root

def compile(name,source,tag,flags='',cwd=None):
 cwd=cwd or root
 objname='build/linux/client/release/'+name[:-4]+'.o'
 if name.endswith('Test.cpp') or name.endswith('Benchmark.cpp'):objname='build/linux/client/release/test/engine-'+name[:-4].replace('/','_')+'.o'
 cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+objname+' ')))
 p=out/(tag+'-'+Path(name).name);p.write_text(source);obj=p.with_suffix('.o');cmd[cmd.index('-o')+1]=str(obj);cmd[-1]=str(p);cmd.insert(cmd.index('-c'),'-I'+str((cwd/name).parent));cmd[2:2]=shlex.split(flags)
 with (out/'isolated-variants-build.log').open('a') as log:subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
 commands.append({'cwd':str(cwd),'command':cmd});return objname,str(obj)
def link(target,tag,objects,cwd=None,extra=()):
 cwd=cwd or root
 cmd=c_templates[target].copy() if cwd!=root else shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o '+target+' ')))
 cmd[2]=str(out/tag);cmd=[objects.get(a,a) for a in cmd];cmd[3:3]=extra
 with (out/'isolated-variants-build.log').open('a') as log:subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
 commands.append({'cwd':str(cwd),'command':cmd})

if __name__=='__main__':
 cache={};variant_objects={}
 for tag in ['control','A','B','C','D','AB','ABCD']:
  cwd=family(tag);overrides={}
  for name,opts in [('src/map/MapResourceState.cpp','ABC'),('src/map/ResourceGrowth.cpp','B'),('src/engine/sim/snapshot/WorldCapture.cpp','CD')]:
   enabled=''.join(c for c in opts if c in tag and tag!='control');key=(str(cwd),name,enabled)
   if key not in cache:
    flags=' '.join(f'-DGROWTH_OPT_{c}={int(c in enabled)}' for c in opts)
    cache[key]=compile(name,(cwd/name).read_text(),'isolated-'+('c-layout-' if cwd!=root else '')+(enabled or 'off')+'-'+Path(name).stem,flags,cwd=cwd)
   overrides.update([cache[key]])
  if '--tests-only' not in sys.argv:link('build/linux/client/release/src/glob2',tag,overrides,cwd=cwd)
  if '--engine-only' not in sys.argv:link('build/linux/client/release/test/glob2-engine-tests',tag+'-tests',overrides,cwd=cwd)
  variant_objects[tag]={'cwd':str(cwd),'objects':overrides}
  print('built isolated',tag,flush=True)
  (out/'isolated-build-commands.json').write_text(json.dumps(commands,indent=2))
 (out/'isolated-overrides.json').write_text(json.dumps(variant_objects,indent=2))
