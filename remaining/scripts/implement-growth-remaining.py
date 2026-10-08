from pathlib import Path
root=Path.cwd();out=root/'artifacts/resource-growth/remaining';(out/'sources').mkdir(exist_ok=True)
files=['src/map/Map.h','src/map/MapResourceState.cpp','src/map/ResourceGrowth.cpp','src/engine/sim/snapshot/WorldSnapshot.h','src/engine/sim/snapshot/WorldCapture.cpp']
for f in files:(out/'sources'/Path(f).name).write_text((root/f).read_text())
def edit(f,fn):
 p=root/f;p.write_text(fn(p.read_text()))
def defs(*names):return ''.join(f'#ifndef GROWTH_OPT_{n}\n#define GROWTH_OPT_{n} 1\n#endif\n' for n in names)
edit('src/map/Map.h',lambda s:s.replace('void materialStockChanged(size_t index, MaterialMask before);','''void materialStockChanged(size_t index, MaterialMask before);
    void commitMaterialAmount(size_t index, unsigned material, Uint16 oldAmount, Uint16 amount, const ResourceProperties& properties);
    MapState::ChangeTracker resourceStockChanges;
    void markResourceStock(size_t slot);
    void resetResourceStockChanges();''').replace('void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);','''enum class MaterialDeltaResult { AppliedExisting, AppliedSeed, Rejected, CapacityClamped };
    MaterialDeltaResult applyMaterialDelta(size_t index, Uint16 type, unsigned material, int delta);
    const MapState::ChangeTracker& stockChanges() const { return resourceStockChanges; }
    void setMaterialAmount(size_t index, MaterialId material, Uint16 amount);'''))
edit('src/engine/sim/snapshot/WorldSnapshot.h',lambda s:s.replace('ChunkStamps stamps; // cells and stockIndices','ChunkStamps stamps; // cells and stockIndices\n\tChunkStamps stockStamps; // contiguous blocks of 256 stock records'))
p=root/'src/map/MapResourceState.cpp';s=defs('A','C')+p.read_text()
s=s.replace('resourceStocks=std::move(stocks);','resourceStocks=std::move(stocks);\n    resetResourceStockChanges();')
s=s.replace('void Map::refreshResourceTotal(size_t index)', '''void Map::markResourceStock(size_t slot)
{
#if GROWTH_OPT_C
    const auto count=(resourceStocks.size()+255)/256;
    if (resourceStockChanges.chunks.size()!=count) resourceStockChanges.reset(count);
    resourceStockChanges.mark(slot/256);
#endif
}
void Map::resetResourceStockChanges()
{
#if GROWTH_OPT_C
    resourceStockChanges.reset((resourceStocks.size()+255)/256);
#endif
}

void Map::refreshResourceTotal(size_t index)''')
s=s.replace('resourceCells[index].resource.amount=total;','resourceCells[index].resource.amount=total;\n    markResourceStock(slot-1);')
s=s.replace('resourceStockIndices.clear(); resourceStocks.clear(); freeResourceStocks.clear(); materialSourceCounts.fill(0);','resourceStockIndices.clear(); resourceStocks.clear(); freeResourceStocks.clear(); materialSourceCounts.fill(0);\n    resetResourceStockChanges();')
a=s.index('    const bool crossesZero=',s.index('void Map::setMaterialAmount('));b=s.index('\nvoid Map::setResourceAmount',a)
s=s[:a]+'''    commitMaterialAmount(index,material,oldAmount,amount,p);
}

void Map::commitMaterialAmount(size_t index, unsigned material, Uint16 oldAmount, Uint16 amount, const ResourceProperties& p)
{
    auto& r=resourceCells[index].resource;
    const bool crossesZero=(oldAmount==0)!=(amount==0);
    const auto before=crossesZero ? resourceMaterialMaskAt(index) : MaterialMask(0);
    markResource(index);
    if (std::has_single_bit(p.materialMask)) r.amount=amount;
    else {
        resourceStocks[resourceStockIndices[index]-1][material]=amount;
#if GROWTH_OPT_A
        r.amount = r.amount - oldAmount + amount;
        markResourceStock(resourceStockIndices[index]-1);
#else
        refreshResourceTotal(index);
#endif
    }
    if (crossesZero) materialStockChanged(index,before);
    if (!r.amount && !p.persistsWhenEmpty) replaceResource(index,Resource{});
}

Map::MaterialDeltaResult Map::applyMaterialDelta(size_t index, Uint16 type, unsigned material, int delta)
{
    if (index>=cellCount() || material>=MaterialCount || (delta!=1 && delta!=-1) || !resourceRegistry().valid(type))
        return MaterialDeltaResult::Rejected;
    const auto oldType=resourceCells[index].resource.type;
    if (oldType!=type && (oldType!=NO_RES_TYPE || delta<0)) return MaterialDeltaResult::Rejected;
    const auto& p=resourcePropertiesByIndex(type);
    if (!(p.materialMask&(1u<<material))) return MaterialDeltaResult::Rejected;
    const auto capacity=resourceRegistry().yields(static_cast<ResourceId>(type))[material].capacity;
    const unsigned amount=oldType==NO_RES_TYPE ? 0 : materialAmountAtSlot(index,material);
    if (delta>0 && amount>=capacity) return MaterialDeltaResult::CapacityClamped;
    if (delta<0 && !amount) return MaterialDeltaResult::Rejected;
    if (oldType==NO_RES_TYPE) {
        std::array<Uint16,MaterialCount> stocks{}; stocks[material]=1;
        replaceResource(index,Resource{type,0,1,0},&stocks);
        return MaterialDeltaResult::AppliedSeed;
    }
    commitMaterialAmount(index,material,amount,amount+delta,p);
    return MaterialDeltaResult::AppliedExisting;
}
''' + s[b:];p.write_text(s)
p=root/'src/map/ResourceGrowth.cpp';s=defs('B')+p.read_text();a=s.index('\t\t// Kernel output');b=s.index('\n\t}\n\tmetrics.publicationNs',a)
s=s[:a]+'''#if GROWTH_OPT_B
        const auto result=map.applyMaterialDelta(p.tile,p.type,p.material,p.delta);
        if (result==Map::MaterialDeltaResult::Rejected || result==Map::MaterialDeltaResult::CapacityClamped) {
            ++metrics.rejected;
            metrics.clamped += result==Map::MaterialDeltaResult::CapacityClamped;
            continue;
        }
        const bool newTile=result==Map::MaterialDeltaResult::AppliedSeed;
        metrics.tilesAdded += newTile;
        ++metrics.accepted;
        metrics.stockAdded += p.delta>0;
        recordDelta(map,p,newTile);
#else
'''+s[a:b]+'\n#endif'+s[b:];s=s.replace('\tconst auto v = map.cellView();','''#if !GROWTH_OPT_B
	const auto v = map.cellView();
#endif''',1);p.write_text(s)
p=root/'src/engine/sim/snapshot/WorldCapture.cpp';s=defs('C','D')+p.read_text();a=s.index('\t\telse for (std::size_t chunk = 0;',s.index('const auto refresh ='));b=s.index('\n\t\tstamps.worldIdentity',a)
s=s[:a]+'''#if GROWTH_OPT_D
        else for (std::size_t chunk=0;chunk<live.chunks.size();) {
            if (stamps.chunks[chunk]==live.chunks[chunk]) { ++chunk; continue; }
            const auto first=chunk, rowEnd=(chunk/geometry.chunksWide+1)*geometry.chunksWide;
            do { stamps.chunks[chunk]=live.chunks[chunk]; ++chunk; }
            while (chunk<rowEnd && stamps.chunks[chunk]!=live.chunks[chunk]);
            const auto x=(first%geometry.chunksWide)*MapState::ChunkGeometry::Side;
            const auto y=(first/geometry.chunksWide)*MapState::ChunkGeometry::Side;
            const auto length=std::min(std::size_t(geometry.width)-x,(chunk-first)*MapState::ChunkGeometry::Side);
            for (std::size_t r=y;r<std::min(std::size_t(geometry.height),y+MapState::ChunkGeometry::Side);++r)
                copyRange(r*geometry.width+x,length);
        }
#else
'''+s[a:b]+'\n#endif'+s[b:]
s=s.replace('\t\tcopyArray(resources->stocks, game.map.resourceStockState());','''#if GROWTH_OPT_C
        const auto source=game.map.resourceStockState();
        const bool resizedStocks=prepare(resources->stocks,source.size());
        auto& stamps=resources->stockStamps;
        const auto& live=game.map.stockChanges();
        const bool full=resizedStocks || stamps.worldIdentity!=identity || stamps.chunks.size()!=live.chunks.size();
        std::size_t dirty=0;
        if (!full) for (std::size_t i=0;i<live.chunks.size();++i) dirty+=stamps.chunks[i]!=live.chunks[i];
        if (full || dirty>live.chunks.size()/2) {
            copyCells(resources->stocks,source,0,source.size());
            reserve(stamps.chunks,live.chunks.size());
            stamps.chunks.assign(live.chunks.begin(),live.chunks.end());
        } else for (std::size_t i=0;i<live.chunks.size();) {
            if (stamps.chunks[i]==live.chunks[i]) { ++i; continue; }
            const auto first=i;
            do { stamps.chunks[i]=live.chunks[i]; ++i; }
            while (i<live.chunks.size() && stamps.chunks[i]!=live.chunks[i]);
            copyCells(resources->stocks,source,first*256,std::min(source.size(),i*256)-first*256);
        }
        stamps.worldIdentity=identity;
#else
        copyArray(resources->stocks, game.map.resourceStockState());
#endif''');p.write_text(s)
