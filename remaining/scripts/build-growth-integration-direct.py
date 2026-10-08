from pathlib import Path
import subprocess,shlex,json
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';cwd=base/'integration-src';lines=(base/'integration-optimized-build.log').read_text().splitlines();commands=[]
name='src/map/ResourceGrowth.cpp';source=(cwd/name).read_text();a=source.index('\t\tjob->work =\n');z=source.index('\n\t}\n\tcatch',a)
source=source[:a]+'''        if (shared)
            job->work = executor->submit(std::span(&group, 1), ComputeExecutor::Placement::Shared);
        else
            run(job, 0); // direct serial control; preserve delayed publication
'''.rstrip()+source[z:]
p=base/'integration-direct-ResourceGrowth.cpp';p.write_text(source);obj=p.with_suffix('.o');objname='build/linux/client/release/src/map/ResourceGrowth.o'
cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+objname+' ')));cmd[cmd.index('-o')+1]=str(obj);cmd[-1]=str(p);cmd.insert(cmd.index('-c'),'-I'+str((cwd/name).parent));commands.append(cmd)
with (base/'integration-direct-build.log').open('w') as log:
 subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
 for target,tag in [('build/linux/client/release/src/glob2','integration-direct'),('build/linux/client/release/test/glob2-engine-tests','integration-direct-tests')]:
  cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('g++ -o '+target+' ')));cmd[2]=str(base/tag);cmd=[str(obj) if a==objname else a for a in cmd];subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True);commands.append(cmd)
(base/'integration-direct-build-commands.json').write_text(json.dumps({'cwd':str(cwd),'commands':commands},indent=2))
