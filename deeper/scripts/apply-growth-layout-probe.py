from pathlib import Path
import json,shutil
r=Path('artifacts/resource-growth/deeper');backup={}
def edit(name,old,new):
 p=Path(name);s=p.read_text();backup.setdefault(name,s)
 assert old in s,(name,old)
 p.write_text(s.replace(old,new))
edit('src/map/MapState.h',' Uint32 incarnation = 0;','')
edit('src/map/MapState.h','sizeof(ResourceCell) == 16','sizeof(ResourceCell) == 12')
edit('src/map/Map.h','std::vector<MapState::ResourceCell> resourceCells;','std::vector<MapState::ResourceCell> resourceCells;\n    std::vector<Uint32> resourceIncarnations;')
edit('src/map/Map.h','std::span<const MapState::ResourceCell> resourceState() const { return resourceCells; }','std::span<const MapState::ResourceCell> resourceState() const { return resourceCells; }\n    std::span<const Uint32> resourceIncarnationState() const { return resourceIncarnations; }')
edit('src/map/MapStateView.h','std::span<const ResourceCell> resources;','std::span<const ResourceCell> resources;\n    std::span<const Uint32> resourceIncarnations;')
for n in ['src/map/MapCells.cpp','src/map/MapMisc.cpp','src/map/io/MapIO.cpp']:
 p=Path(n);s=p.read_text();backup.setdefault(n,s)
 import re
 s=re.sub(r'resourceCells\[([^]]+)\]\.incarnation',r'resourceIncarnations[\1]',s)
 if n.endswith('MapCells.cpp'):s=s.replace('{resolved, tile.fertility, tile.canResourcesGrow, resourceIncarnations[index]}','{resolved, tile.fertility, tile.canResourcesGrow}')
 p.write_text(s)
for n in ['src/map/Map.cpp','src/map/io/MapIO.cpp','src/map/MapQueryTest.cpp','src/map/FertilityFieldTest.cpp','src/map/gradient/PathGradientHarness.cpp','src/map/gradient/GradientTest.cpp']:
 edit(n,'resourceCells.assign(size, {});','resourceCells.assign(size, {}); resourceIncarnations.assign(size, 0);')
edit('src/map/MapResourceState.cpp','view.resources=resourceCells;','view.resources=resourceCells; view.resourceIncarnations=resourceIncarnations;')
edit('src/map/ResourceGrowth.cpp','proposal.incarnation = target.incarnation;','proposal.incarnation = v.resourceIncarnations[i];')
edit('src/map/ResourceGrowth.cpp','cell.incarnation != p.incarnation','v.resourceIncarnations[p.tile] != p.incarnation')
edit('src/map/ResourceGrowthTest.cpp','m.cellView().resources[at].incarnation','m.cellView().resourceIncarnations[at]')
edit('src/engine/sim/snapshot/WorldSnapshot.h','std::vector<ResourceCell> cells;','std::vector<ResourceCell> cells;\n    std::vector<Uint32> incarnations;')
edit('src/engine/sim/snapshot/WorldSnapshot.cpp','v.resources = resources->cells;','v.resources = resources->cells; v.resourceIncarnations = resources->incarnations;')
edit('src/engine/sim/snapshot/WorldCapture.cpp','bool resized = prepare(resources->cells, cellsSource.size());','bool resized = prepare(resources->cells, cellsSource.size());\n        resized |= prepare(resources->incarnations, cellsSource.size());')
edit('src/engine/sim/snapshot/WorldCapture.cpp','copyCells(resources->cells, cellsSource, start, length);','copyCells(resources->cells, cellsSource, start, length);\n            copyCells(resources->incarnations, game.map.resourceIncarnationState(), start, length);')
edit('src/engine/sim/snapshot/WorldCapture.cpp','compare("resources", handle.resources->cells, game.map.resourceState());','compare("resources", handle.resources->cells, game.map.resourceState());\n        compare("incarnations", handle.resources->incarnations, game.map.resourceIncarnationState());')
edit('src/engine/sim/snapshot/SnapshotStorage.h','account(resources, cells);','account(resources, [&](const Resources& value, bool) { return vectorBytes(value.cells) + vectorBytes(value.incarnations); });')
(r/'layout-original-files.json').write_text(json.dumps(backup))
shutil.copy2('build/linux/client/release/src/glob2',r/'before-layout-glob2')
print(len(backup),'files changed for layout-only probe')
