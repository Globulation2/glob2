from pathlib import Path
import json
source=Path('docs/.work/build-growth-attribution-v2.py').read_text();exec(source[:source.index("src=(root/'src/map/ResourceGrowth.cpp')")])
src=(root/'src/map/ResourceGrowth.cpp').read_text()
a=src.index('\t\tjob->work =\n');b=src.index('\n\t}\n\tcatch',a)
src=src[:a]+'''        if (shared)
            job->work = executor->submit(std::span(&group, 1), ComputeExecutor::Placement::Shared);
        else
            run(job, 0); // pure serial control: do not pump unrelated executor jobs
'''.rstrip()+src[b:]
objects=dict([compile_source('src/map/ResourceGrowth.cpp',src,'direct-owner')])
objects['build/linux/client/release/src/engine/sim/snapshot/WorldCapture.o']=str(out/'bulk-copy-WorldCapture.o')
link_binary('build/linux/client/release/src/glob2','direct-owner-bulk',objects)
link_binary('build/linux/client/release/test/glob2-engine-tests','direct-owner-tests',objects)
(out/'direct-build-commands.json').write_text(json.dumps(commands,indent=2))
