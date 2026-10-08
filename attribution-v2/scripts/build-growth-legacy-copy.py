from pathlib import Path
import json,shlex,subprocess
root=Path.cwd();out=root/'artifacts/resource-growth/attribution-v2';cwd=root/'artifacts/resource-growth/baseline-src';name='src/engine/sim/snapshot/WorldCapture.cpp'
src=(cwd/name).read_text();needle='\t\tif (everything)\n\t\t{\n\t\t\tcopyRange';replacement='''\t\tstd::size_t dirty = 0;
        if (!everything)
            for (std::size_t chunk=0; chunk<live.chunks.size(); ++chunk)
                dirty += stamps.chunks[chunk] != live.chunks[chunk];
        if (everything || dirty > live.chunks.size()/2)
        {
            copyRange'''
assert needle in src;src=src.replace(needle,replacement,1);path=out/'legacy-bulk-WorldCapture.cpp';path.write_text(src)
objname='build/linux/client/release/'+name[:-4]+'.o';lines=(root/'artifacts/resource-growth/baseline-build.log').read_text().splitlines();cmd=shlex.split(next(l for l in reversed(lines) if l.startswith('/usr/bin/ccache g++ -o '+objname+' ')));obj=path.with_suffix('.o');cmd[cmd.index('-o')+1]=str(obj);cmd[-1]=str(path);cmd.insert(cmd.index('-c'),'-I'+str((cwd/name).parent))
link=json.load(open(root/'artifacts/resource-growth/profiling/legacy-relink-command.json'));link[2]=str(out/'legacy-bulk-copy');link=[str(obj) if a==objname else a for a in link]
with (out/'legacy-copy-build.log').open('w') as log:
 subprocess.run(cmd,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True);subprocess.run(link,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
(out/'legacy-copy-build-commands.json').write_text(json.dumps({'cwd':str(cwd),'compile':cmd,'link':link},indent=2))
