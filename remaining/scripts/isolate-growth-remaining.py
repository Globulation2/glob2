from pathlib import Path
import shutil,json
root=Path.cwd();r=root/'artifacts/resource-growth/remaining';c=r/'c-layout-src'
# Preserve the first prototypes and exploratory measurements without rewriting their raw commands.
for name in ['components','verification']:
 if (r/name).exists():(r/name).rename(r/('pilot-'+name))
for name in ['components.log','components-summary.log','verification.log','cpuset-components.json','governor-components.json']:
 if (r/name).exists():(r/name).rename(r/('pilot-'+name))
(r/'pilot-binaries').mkdir(exist_ok=True)
for tag in ['control','A','B','C','D','AB','ABCD']:
 for suffix in ['','-tests','-components']:
  p=r/(tag+suffix)
  if p.exists():shutil.move(p,r/'pilot-binaries'/p.name)
# The common headers for variants without C contain declarations only, no new state.
orig=(r/'sources/Map.h').read_text()
base=orig.replace('void materialStockChanged(size_t index, MaterialMask before);','void materialStockChanged(size_t index, MaterialMask before);\n    void commitMaterialAmount(size_t index, unsigned material, Uint16 oldAmount, Uint16 amount, const ResourceProperties& properties);')
base=base.replace('void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);','''enum class MaterialDeltaResult { AppliedExisting, AppliedSeed, Rejected, CapacityClamped };
    MaterialDeltaResult applyMaterialDelta(size_t index, Uint16 type, unsigned material, int delta);
    void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);''')
(root/'src/map/Map.h').write_text(base)
shutil.copy2(r/'sources/WorldSnapshot.h',root/'src/engine/sim/snapshot/WorldSnapshot.h')
# C does not use the common commit helper unless B is enabled. All-off setter is the original body.
s=(c/'src/map/MapResourceState.cpp').read_text()
s='#ifndef GROWTH_OPT_B\n#define GROWTH_OPT_B 1\n#endif\n'+s
s=s.replace('    resetResourceStockChanges();','''#if GROWTH_OPT_C
    resetResourceStockChanges();
#endif''')
s=s.replace('void Map::markResourceStock(size_t slot)\n{\n#if GROWTH_OPT_C','''#if GROWTH_OPT_C
void Map::markResourceStock(size_t slot)
{''').replace('    resourceStockChanges.mark(slot/256);\n#endif','    resourceStockChanges.mark(slot/256);')
s=s.replace('void Map::resetResourceStockChanges()\n{\n#if GROWTH_OPT_C','void Map::resetResourceStockChanges()\n{').replace('    resourceStockChanges.reset((resourceStocks.size()+255)/256);\n#endif\n}', '    resourceStockChanges.reset((resourceStocks.size()+255)/256);\n}\n#endif')
s=s.replace('    markResourceStock(slot-1);','''#if GROWTH_OPT_C
    markResourceStock(slot-1);
#endif''')
original=(r/'sources/MapResourceState.cpp').read_text();a=original.index('    const bool crossesZero=',original.index('void Map::setMaterialAmount('));b=original.index('\n}\n',a)
flat=original[a:b]
flat=flat.replace('else { resourceStocks[resourceStockIndices[index]-1][material]=amount; refreshResourceTotal(index); }','''else {
        resourceStocks[resourceStockIndices[index]-1][material]=amount;
#if GROWTH_OPT_A
        r.amount = r.amount - oldAmount + amount;
#if GROWTH_OPT_C
        markResourceStock(resourceStockIndices[index]-1);
#endif
#else
        refreshResourceTotal(index);
#endif
    }''')
s=s.replace('    commitMaterialAmount(index,material,oldAmount,amount,p);\n}', '#if GROWTH_OPT_B\n    commitMaterialAmount(index,material,oldAmount,amount,p);\n#else\n'+flat+'\n#endif\n}',1)
s=s.replace('void Map::commitMaterialAmount(', '#if GROWTH_OPT_B\nvoid Map::commitMaterialAmount(',1)
s=s.replace('\nvoid Map::setResourceAmount(', '\n#endif\n\nvoid Map::setResourceAmount(',1)
s=s.replace('        markResourceStock(resourceStockIndices[index]-1);','''#if GROWTH_OPT_C
        markResourceStock(resourceStockIndices[index]-1);
#endif''')
# Repeated nested guard in the flat branch is harmless but avoid leaving it in the source.
s=s.replace('#if GROWTH_OPT_C\n#if GROWTH_OPT_C\n', '#if GROWTH_OPT_C\n').replace('        markResourceStock(resourceStockIndices[index]-1);\n#endif\n#endif', '        markResourceStock(resourceStockIndices[index]-1);\n#endif')
(c/'src/map/MapResourceState.cpp').write_text(s)
(root/'src/map/MapResourceState.cpp').write_text(s.replace('#define GROWTH_OPT_C 1','#define GROWTH_OPT_C 0'))
p=root/'src/engine/sim/snapshot/WorldCapture.cpp';p.write_text(p.read_text().replace('#define GROWTH_OPT_C 1','#define GROWTH_OPT_C 0'))
(r/'pilot-explanation.txt').write_text('The first timing campaign was deliberately stopped after the all-options-off control improved dense performance. All-off shared helper extraction and C-only data fields contaminated attribution. All completed rows are retained, not used for adoption. New builds exclude C fields when C is disabled and retain the original flat setter when B is disabled. Baseline/master executables and fixture inputs are unchanged. Reservations and governors were restored.\n')
