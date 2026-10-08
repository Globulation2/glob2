from pathlib import Path
import json
source=Path('docs/.work/build-growth-attribution-v2.py').read_text();exec(source[:source.index("src=(root/'src/map/ResourceGrowth.cpp')")])
src=(root/'src/map/ResourceGrowth.cpp').read_text()
needle='\t++metrics.submitted;\n\tmetrics.maxPending'
assert needle in src;src=src.replace(needle,'\tif (!shared) join(*job); // experiment: compute now, preserve publication deadline\n'+needle,1)
objects=dict([compile_source('src/map/ResourceGrowth.cpp',src,'eager-owner')])
objects['build/linux/client/release/src/engine/sim/snapshot/WorldCapture.o']=str(out/'bulk-copy-WorldCapture.o')
link_binary('build/linux/client/release/src/glob2','eager-owner-bulk',objects)
link_binary('build/linux/client/release/test/glob2-engine-tests','eager-owner-tests',objects)
(out/'eager-build-commands.json').write_text(json.dumps(commands,indent=2))
