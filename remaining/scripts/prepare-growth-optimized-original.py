from pathlib import Path
import subprocess,json
root=Path.cwd();base=root/'artifacts/resource-growth/remaining';out=base/'optimized-original-src'
scope={};script=(root/'docs/.work/strip-growth-remaining.py').read_text();exec(script[:script.index('names=')],scope)
s=(base/'c-layout-src/src/map/MapResourceState.cpp').read_text();s=scope['strip'](s.replace('#if GROWTH_OPT_B','#if !GROWTH_OPT_B'))
s=s.replace('    gradientRuntime->growth.reset();\n','')
s=s.replace('void Map::initializeResourceStock(size_t index, const std::array<Uint16, MaterialCount> *initialStocks)','void Map::initializeResourceStock(size_t index)')
s=s.replace('initialStocks ? (*initialStocks)[materialIndex(p.primaryMaterial)] : r.amount','r.amount')
s=s.replace('initialStocks ? std::min((*initialStocks)[m], yields[m].capacity) : yields[m].initial','yields[m].initial')
assert 'initialStocks' not in s and 'growth.reset' not in s
(out/'src/map/MapResourceState.cpp').write_text(s)
p=out/'src/map/Map.h';s=p.read_text();needle='\tvoid materialStockChanged(size_t index, MaterialMask before);';assert needle in s
s=s.replace(needle,needle+'\n    MapState::ChangeTracker resourceStockChanges;\n    void markResourceStock(size_t slot);\n    void resetResourceStockChanges();')
needle='\tvoid setMaterialAmount(size_t index, MaterialId material, Uint16 amount);';assert needle in s
s=s.replace(needle,'    const MapState::ChangeTracker& stockChanges() const { return resourceStockChanges; }\n'+needle);p.write_text(s)
p=out/'src/engine/sim/snapshot/WorldSnapshot.h';s=p.read_text();needle='\tChunkStamps stamps; // cells and stockIndices';assert needle in s;s=s.replace(needle,needle+'\n\tChunkStamps stockStamps; // contiguous blocks of 256 stock records');p.write_text(s)
name='src/engine/sim/snapshot/WorldCapture.cpp';(out/name).write_bytes((base/'integration-src'/name).read_bytes())
patch=subprocess.check_output(['git','diff','HEAD','--',name]);subprocess.run(['git','apply','--directory='+str(out.relative_to(root))],input=patch,check=True)
p=out/'src/engine/sim/snapshot/WorldSnapshotTest.cpp';extra=(root/'src/engine/sim/snapshot/WorldSnapshotTest.cpp').read_text().split('TEST_CASE("stock snapshots survive slot reuse rebuild and sparse refresh"',1)[1];p.write_text('#include <nlohmann/json.hpp>\n'+p.read_text()+'\nTEST_CASE("stock snapshots survive slot reuse rebuild and sparse refresh"'+extra)
(base/'optimized-original-identity.json').write_text(json.dumps({'source_revision':'0f1a2569ab7f23c8702a078978054f73f4ddb9cc','growth':'unchanged immediate algorithm','common_improvements':['baseline contiguous snapshot copy','A incremental totals','C tracked stock blocks','D coalesced sparse spatial copies'],'B':'not applicable; original growth emits no proposals'},indent=2))
