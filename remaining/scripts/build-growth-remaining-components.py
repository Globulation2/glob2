from pathlib import Path
import sys,json
sys.path.insert(0,'docs/.work');from importlib.machinery import SourceFileLoader
b=SourceFileLoader('remaining_build','docs/.work/build-growth-remaining-isolated.py').load_module()
root=b.root;out=b.out;overrides=json.load(open(out/'isolated-overrides.json'))
cache={};benches={}
for tag in ['control','A','B','C','D','AB','ABCD']:
 cwd=b.family(tag)
 if cwd not in benches:
  bench='#include <deque>\n'+(cwd/'src/map/ResourceGrowthBenchmark.cpp').read_text()+'\n'+(root/'docs/.work/remaining-component.cpp').read_text()
  benches[cwd]=b.compile('src/map/ResourceGrowthBenchmark.cpp',bench,'components-'+('c-layout' if cwd!=root else 'base'),cwd=cwd)
 objects=dict(overrides[tag]['objects']);objects.update([benches[cwd]])
 enabled=''.join(c for c in 'CD' if c in tag and tag!='control');key=(cwd,enabled)
 if key not in cache:
  wc=(cwd/'src/engine/sim/snapshot/WorldCapture.cpp').read_text();pos=wc.index('namespace SimulationSnapshot')
  wc=wc[:pos]+'''static thread_local Uint64 experimentCopyCalls=0;
extern "C" Uint64 resourceExperimentCopyCalls(){return experimentCopyCalls;}
'''+wc[pos:]
  wc=wc.replace('if (!source.empty()) std::memcpy','if (!source.empty()) ++experimentCopyCalls;\n        if (!source.empty()) std::memcpy')
  wc=wc.replace('if (length) std::memcpy','if (length) ++experimentCopyCalls;\n        if (length) std::memcpy')
  cache[key]=b.compile('src/engine/sim/snapshot/WorldCapture.cpp',wc,'isolated-copy-probe-'+('c-layout-' if cwd!=root else '')+(enabled or 'off'),' '.join(f'-DGROWTH_OPT_{c}={int(c in enabled)}' for c in 'CD'),cwd=cwd)
 objects.update([cache[key]]);b.link('build/linux/client/release/test/glob2-engine-tests',tag+'-components',objects,cwd=cwd)
 print('component build',tag,flush=True)
(out/'component-build-commands.json').write_text(json.dumps(b.commands,indent=2))
