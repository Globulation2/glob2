from pathlib import Path
import json
source=Path('docs/.work/build-growth-attribution-v2.py').read_text();exec(source[:source.index("src=(root/'src/map/ResourceGrowth.cpp')")])
# WorldCapture did not need rebuilding in the last incremental compile; recover
# its command from the prior same-toolchain build logs.
lines[:0]=(root/'artifacts/resource-growth/candidate-build-final.log').read_text().splitlines()
name='src/engine/sim/snapshot/WorldCapture.cpp';src=(root/name).read_text()
needle='\t\tif (everything)\n\t\t{\n\t\t\tcopyRange'
replacement='''\t\tstd::size_t dirty = 0;
        if (!everything)
            for (std::size_t chunk=0; chunk<live.chunks.size(); ++chunk)
                dirty += stamps.chunks[chunk] != live.chunks[chunk];
        if (everything || dirty > live.chunks.size()/2)
        {
            copyRange'''
assert needle in src;src=src.replace(needle,replacement,1)
objects=dict([compile_source(name,src,'bulk-copy')])
link_binary('build/linux/client/release/src/glob2','bulk-copy',objects)
link_binary('build/linux/client/release/test/glob2-engine-tests','bulk-copy-tests',objects)
(out/'copy-build-commands.json').write_text(json.dumps(commands,indent=2))
