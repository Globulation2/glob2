from pathlib import Path
import json,sys
from importlib.machinery import SourceFileLoader
b=SourceFileLoader('remaining_build','docs/.work/build-growth-remaining-isolated.py').load_module()
root=b.root;out=b.out
(out/'StageProbe.h').write_bytes((root/'artifacts/resource-growth/deeper/StageProbe.h').read_bytes())
overrides=json.load(open(out/'isolated-overrides.json'));cache={}
for tag in ['control','A','B','C','D','AB','ABCD']:
 cwd=b.family(tag);objects=dict(overrides[tag]['objects'])
 for name,scopes in [('src/engine/sim/snapshot/SnapshotStore.cpp',[('Handle Store::captureBoundary(',0)]),('src/map/ResourceGrowth.cpp',[('void calculate(',2),('void apply(',3),('void Pipeline::prepare(',4)]),('src/app/cli/Headless.cpp',[])]:
  flags='-DGROWTH_OPT_B='+str(int('B' in tag)) if name.endswith('ResourceGrowth.cpp') else ''
  key=(cwd,name,flags)
  if key not in cache:
   source=(cwd/name).read_text()
   for prefix,slot in scopes:
    pos=source.index(prefix);brace=source.index('\n{',pos)+2;source=source[:brace]+f'\n StageProbe::Scope stageProbe({slot});'+source[brace:]
   if name.endswith('Headless.cpp'):
    source='#include "sim/snapshot/SnapshotStore.h"\n'+source
    source=source.replace('const auto runStart =','const auto snapshotBefore=engine.gui.game.snapshotStore().metrics;\n        StageProbe::begin();\n        const auto runStart =',1)
    needle='const auto runEnd = std::chrono::steady_clock::now();'
    replacement=needle+'''
        StageProbe::finish();
        if (const char* probePath=std::getenv("GLOB2_STAGE_CPU")) {
            const auto& m=engine.gui.game.snapshotStore().metrics;
            const std::string path=std::string(probePath)+".snapshots.json";
            if(FILE* f=std::fopen(path.c_str(),"w")) {
                std::fprintf(f,"{\\"capture_ns\\":%llu,\\"preparation_ns\\":%llu,\\"bytes_copied\\":%llu,\\"allocations\\":%llu,\\"captures\\":%llu}\\n",
                    (unsigned long long)(m.captureNs-snapshotBefore.captureNs),
                    (unsigned long long)(m.preparationNs-snapshotBefore.preparationNs),
                    (unsigned long long)(m.bytesCopied-snapshotBefore.bytesCopied),
                    (unsigned long long)(m.allocations-snapshotBefore.allocations),
                    (unsigned long long)(m.captures-snapshotBefore.captures));
                std::fclose(f);
            }
        }
'''
    assert needle in source;source=source.replace(needle,replacement,1)
   source='#include "StageProbe.h"\n'+source
   cache[key]=b.compile(name,source,'stages-'+('c-layout-' if cwd!=root else '')+Path(name).stem+('-B' if flags.endswith('1') else ''),flags,cwd=cwd)
  objects.update([cache[key]])
 b.link('build/linux/client/release/src/glob2',tag+'-stages',objects,cwd=cwd)
 print('stages build',tag,flush=True)
(out/'stages-build-commands.json').write_text(json.dumps(b.commands,indent=2))
