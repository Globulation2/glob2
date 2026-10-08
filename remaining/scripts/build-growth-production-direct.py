from pathlib import Path
from importlib.machinery import SourceFileLoader
import json
b=SourceFileLoader('growth_build','docs/.work/build-growth-remaining-isolated.py').load_module()
s=(b.root/'src/map/ResourceGrowth.cpp').read_text();a=s.index('\t\tjob->work =\n');z=s.index('\n\t}\n\tcatch',a)
s=s[:a]+'''        if (shared)
            job->work = executor->submit(std::span(&group, 1), ComputeExecutor::Placement::Shared);
        else
            run(job, 0); // serial control: do not execute unrelated queued work
'''.rstrip()+s[z:]
obj=b.compile('src/map/ResourceGrowth.cpp',s,'production-direct')
b.link('build/linux/client/release/src/glob2','production-direct',dict([obj]))
b.link('build/linux/client/release/test/glob2-engine-tests','production-direct-tests',dict([obj]))
(b.out/'production-direct-build-commands.json').write_text(json.dumps(b.commands,indent=2))
