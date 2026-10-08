// SPDX-License-Identifier: GPL-3.0-or-later
#include "SceneExtract.h"
#include "sim/presentation/SceneInputs.h"
#include "OverlayAreas.h"
#include "UnitTiming.h"
#include "WinProbability.h"
#include "ai/model/BuildingProjection.h"
#include <unordered_map>
#include <utility>
namespace {
void prepareSource(const SceneInputs& input,PresentationFrame& scene)
{
    const auto& world=input.world;
    scene.world=world;scene.request=input.request;
    const auto& request=input.request;
    const auto& state=*world.session;
    scene.buildingTypes=world.catalogs->typeDefinitions;
    scene.editor=state.editor; scene.tick=world.tick;
    scene.tickTime=request.tickTime; scene.tickInterval=request.tickInterval;
    scene.executedOrderRevision=state.executedOrderRevision;
    scene.map.bindSnapshot(world,request.view.displayW,request.view.displayH,request.localTeam);
    auto& e=scene.entities;
    e.world=world; e.typeDefinitions=world.catalogs->typeDefinitions; e.units=world.entities->units;e.unitIndex=world.entities->unitSlotIndices;
    e.buildings=world.entities->buildings;e.buildingIndex=world.entities->buildingSlotIndices;
    e.connectionMasks.assign(e.buildings.size(),0);
    e.teamCount=world.teams->values.size();e.teams=world.teams->values;
    for (const auto& t:world.teams->values) {
        for (unsigned m=0;m<MaterialCount;++m) if (t.materials[m] || t.reservedMaterials[m]) e.materialPresence|=materialBit(static_cast<MaterialId>(m));
        e.virtualBuildings[t.number]=t.virtualBuildings;
    }
    if (world.effects) e.sectors=world.effects->sectors;
    const auto unit=[&](UnitRef ref)->const SimulationSnapshot::UnitView* {
        if (ref.gid>=world.entities->unitSlotIndices.size()) return nullptr;
        const auto index=world.entities->unitSlotIndices[ref.gid];
        if (index>=world.entities->units.size()) return nullptr;
        const auto& u=world.entities->units[index]; return u.identity==ref ? &u : nullptr;
    };
    const auto building=[&](BuildingRef ref)->const SimulationSnapshot::BuildingView* {
        if (ref.gid>=world.entities->buildingSlotIndices.size()) return nullptr;
        const auto index=world.entities->buildingSlotIndices[ref.gid];
        if (index>=world.entities->buildings.size()) return nullptr;
        const auto& b=world.entities->buildings[index]; return b.identity==ref ? &b : nullptr;
    };
    const auto* selected=building(request.selectedBuilding);
    const auto* selectedUnit=unit(request.selectedUnit);
    if (selected) {
        auto& value=e.selectedBuilding;
        value.ref=selected->identity; value.desiredMaxUnitWorking=selected->desiredMaxUnitWorking;
        value.unitsFailingRequirements=selected->unitsFailingRequirements;
        value.unitsWorking=std::span<const UnitRef>(world.entities->relationships).subspan(selected->working.offset,selected->working.count);
        if (world.entityDiagnostics) for (const auto& d:world.entityDiagnostics->buildings) if (d.identity==selected->identity) {
            value.verbose=d.verbose; value.recordFailingUnits=d.recordFailingUnits;
            value.debugGradient=d.gradient; value.unitsFailingRequirements=d.failingCounts; for (size_t i=0;i<value.unitsFailingByReason.size();++i) value.unitsFailingByReason[i]=d.failingUnits[i];
            break;
        }
    }
    e.selectedUnit=selectedUnit ? selectedUnit->identity : UnitRef{};
    e.highlightUnitType=request.highlights ? request.highlights->first : 0;
    e.highlightBuildingType=request.highlights ? request.highlights->second : 0;
    if (!request.includePanels || request.localTeam < 0 || size_t(request.localTeam) >= world.teams->values.size()) return;
    const auto& local=world.teams->values.at(request.localTeam);
    auto& panels=scene.panels;
    panels.world=world;
    panels.local.record=&local;
    if (world.statistics) panels.localStats=world.statistics->teams.at(request.localTeam);
    auto& hud=panels.hud;
    hud.session=&state; hud.teams=world.teams.get(); hud.local=&local;
    Uint32 contested=0;
    for (const auto& p:state.players) {
        if (p.competing && p.teamNumber>=0 && size_t(p.teamNumber)<world.teams->values.size()) contested|=Uint32(1)<<p.teamNumber;
    }
    if (!contested) contested=~Uint32(0);
    for (const auto& t:world.teams->values) if (t.won) {hud.winningTeam=t.number;break;}
    if (hud.winningTeam>=0 || hud.state().totalPrestigeReached) {
        for (const auto& a:world.teams->values) if (a.won && !a.lost)
            for (const auto& b:world.teams->values) if (a.number!=b.number && b.won && (contested & b.mask)
                && !((a.allies & b.mask) && (b.allies & a.mask))) {
                if (contested & a.mask) hud.drawn=true;
                if (a.number==request.localTeam) hud.localDraw=true;
            }
    }
    if (selected) {
        const auto& b=*selected;auto& bp=panels.building;
        bp.record=selected;bp.valid=true;bp.ownerRecord=&world.teams->values.at(b.team);
        bp.type=&scene.buildingTypes->at(b.typeNum);
        bp.materials=b.usesTeamResources ? world.teams->values.at(b.team).materials.data() : b.localMaterials;
        int recipe=b.productionUnit;
        if (recipe<0) {
            Sint64 best=std::numeric_limits<Sint64>::max();
            for (int i=0;i<NB_UNIT_TYPE;++i) if (bp.type->semantics.production.recipes[i].enabled && b.ratio[i]>0) {
                const Sint64 proportion=(Sint64(b.percentUsed[i])<<16)/b.ratio[i];
                if (proportion<=best) {best=proportion;recipe=i;}
            }
        }
        if (recipe>=0 && size_t(recipe)<bp.type->semantics.production.recipes.size()) bp.productionDuration=bp.type->semantics.production.recipes[recipe].duration;
        const auto relationships=std::span<const UnitRef>(world.entities->relationships);
        bp.insideUnits=relationships.subspan(b.inside.offset,b.inside.count);
        bp.workingUnits=relationships.subspan(b.working.offset,b.working.count);
        bp.showLevel=bp.type->presentation.showLevel;bp.buildingHpMultiplier=world.rules->configuration->getBuildingHpMultiplier();
    }
    if (selectedUnit) {
        auto& up=panels.unit;up.record=selectedUnit;up.valid=true;
        up.ownerRecord=&world.teams->values.at(selectedUnit->team);up.unitTypes=world.catalogs->unitTypes[selectedUnit->typeNum];
        up.glassCannonScale=world.rules->configuration->getGlassCannonScale();
    }
}
}
namespace {
void prepareConnections(const SceneMap& map,SceneEntities& e,
    const std::unordered_multimap<int,const SnapshotBuilding*>& overlays,size_t first,size_t count)
{
        const auto tile=[&](int x,int y) {return int(map.coordToIndex(x,y));};
        for (size_t i=first;i<std::min(e.buildings.size(),first+count);++i) {
            const auto& b=e.buildings[i];
            if (e.type(b)->crossConnectMultiImage)
            {
                const auto connects=[&](const SnapshotBuilding* other) {
                    return other && other->gid!=b.gid &&
                        e.type(*other)->presentation.connectionGroupId==e.type(b)->presentation.connectionGroupId &&
                        (e.type(b)->presentation.connectsAcrossTeams || other->team==b.team);
                };
                const auto neighbor=[&](int x,int y) {
                    if (connects(e.building(map.getBuilding(x,y)))) return true;
                    const auto range=overlays.equal_range(tile(x,y));
                    for (auto it=range.first; it!=range.second; ++it) if (connects(it->second)) return true;
                    return false;
                };
                for (int dx=0; dx<e.type(b)->width; ++dx)
                {
                    if (!(e.connectionMasks[e.buildingIndex[b.gid]]&8) && neighbor(b.posX+dx,b.posY-1)) e.connectionMasks[e.buildingIndex[b.gid]]|=8;
                    if (!(e.connectionMasks[e.buildingIndex[b.gid]]&4) && neighbor(b.posX+dx,b.posY+e.type(b)->height)) e.connectionMasks[e.buildingIndex[b.gid]]|=4;
                }
                for (int dy=0; dy<e.type(b)->height; ++dy)
                {
                    if (!(e.connectionMasks[e.buildingIndex[b.gid]]&2) && neighbor(b.posX-1,b.posY+dy)) e.connectionMasks[e.buildingIndex[b.gid]]|=2;
                    if (!(e.connectionMasks[e.buildingIndex[b.gid]]&1) && neighbor(b.posX+e.type(b)->width,b.posY+dy)) e.connectionMasks[e.buildingIndex[b.gid]]|=1;
                }
            }

        }
}
}
namespace {
void preparePanels(const SceneInputs& input, PresentationFrame& scene, const std::array<int,SceneEntities::Teams>& buildLevels)
{
    if (!input.request.includePanels || !scene.panels.local.record) return;
    const auto& world = input.world;
    auto& panels = scene.panels;
    panels.local.maxBuildLevel = buildLevels[input.request.localTeam];
    const auto& configuration = *world.rules->configuration;
    std::vector<int> alliances, allianceOf;
    for (const auto& team : world.teams->values)
    {
        const int alliance = configuration.getAllyTeamNumber(team.number);
        const auto it = std::find(alliances.begin(), alliances.end(), alliance);
        allianceOf.push_back(int(it - alliances.begin()));
        if (it == alliances.end()) alliances.push_back(alliance);
    }
    std::vector<int> providers;
    for (size_t i=0; i<scene.buildingTypes->size(); ++i)
    {
        const auto& t = scene.buildingTypes->at(i);
        const auto& completed = t.isBuildingSite && t.nextLevel>=0 ? scene.buildingTypes->at(t.nextLevel) : t;
        if (ModelBuildingProjection::trainsWarriorCombat(completed)) providers.push_back(int(i));
    }
    std::vector<WinProbability::Slot> slots(alliances.size());
    for (const auto& t : world.teams->values)
    {
        if (!t.alive || t.lost) continue;
        auto& slot = slots[allianceOf[t.number]];
        const auto& stat = t.statistics;
        slot.alive = true; slot.units += stat.totalUnit; slot.prestige += t.prestige;
        for (int id : providers) if (size_t(id) < stat.buildingCountByVariant.size()) slot.barracks += stat.buildingCountByVariant[id];
        slot.explorers += stat.numberUnitPerType[EXPLORER]; slot.foodCritical += stat.needFoodCritical; slot.attack += stat.totalAttackPower;
    }
    const auto chances = WinProbability::permille(slots);
    panels.hud.winChances.clear();
    for (int t=0; t<scene.entities.teamCount; ++t)
    {
        const auto& team = scene.entities.teams[t];
        panels.hud.winChances.push_back({team.firstPlayerName,presentationColor(team.color),chances[allianceOf[t]],slots[allianceOf[t]].alive});
    }
    panels.aiTelemetry.clear();
    if (world.telemetry) for (const auto& source : world.telemetry->rows)
    {
        const auto& local = world.teams->values.at(input.request.localTeam);
        const auto& team = world.teams->values.at(source.team);
        if (!input.request.spectating && source.team != input.request.localTeam
            && !((local.allies & team.mask) && (team.allies & local.mask))) continue;
        SceneAITelemetry row; row.team=source.team; row.player=source.player;
        row.name=source.name; row.available=source.available;
        if (row.available)
        {
            for (size_t i=0; i<source.fields.size() && i<source.values.size(); ++i)
            {
                const auto& field = source.fields[i]; const auto& value = source.values[i];
                if (i >= AITelemetry::OrderTypes && i < AITelemetry::Specific && (!value.valid || !value.bits)) continue;
                row.values.push_back({field.name, AITelemetry::displayValue(field,value),field.unit,field.meaning,value.updated});
            }
            row.values.insert(row.values.end(), source.named.begin(), source.named.end());
        }
        panels.aiTelemetry.push_back(std::move(row));
    }
    auto& bp = panels.building;
    if (bp.valid)
    {
        const auto& type = *bp.type;
        const bool constructible = bp.state().constructionResultState == BuildingStateRecord::NO_CONSTRUCTION
            && bp.state().buildingState == BuildingStateRecord::ALIVE && !type.isBuildingSite;
        const auto hardSpace = [&](int next, bool upgrade) {
            if (upgrade && world.rules->values.upgradesDisabled) return false;
            if (next < 0) return true;
            const auto& target = scene.buildingTypes->at(next);
            if (target.isVirtual) return true;
            const int x = bp.state().posX + target.decLeft - type.decLeft, y = bp.state().posY + target.decTop - type.decTop;
            for (int dy=0; dy<target.height; ++dy) for (int dx=0; dx<target.width; ++dx)
            {
                const auto& map = scene.map;
                const auto& r = map.getResource(x+dx,y+dy);
                if (r.type != NO_RES_TYPE && map.resourceRegistry().properties(static_cast<ResourceId>(r.type)).blocksBuilding) return false;
                const auto occupant = map.getBuilding(x+dx,y+dy);
                if (occupant != 0xffff && occupant != bp.state().gid) return false;
                if (!map.terrainPropertiesAt(x+dx,y+dy).buildable) return false;
            }
            return true;
        };
        if (constructible && type.semantics.repairable && type.prevLevel>=0 && (bp.state().hp<bp.state().maxHp || bp.state().hp<type.hpMax))
        {
            bp.hardSpaceForRepair = hardSpace(type.prevLevel,false) && buildLevels[bp.owner().number]>=scene.buildingTypes->at(type.prevLevel).semantics.requiredWorkerLevel;
            const Sint64 ratio = (Sint64(bp.state().hp)<<16)/bp.state().maxHp;
            Sint32 error=0;
            for (unsigned m=0; m<MaterialCount; ++m)
            {
                const Sint64 value = ratio*type.semantics.repairCost[m];
                int whole = value>>16; error += value & 0xffff;
                if (error>=65536) {error-=65536;++whole;}
                bp.repairCost[m] = type.semantics.repairCost[m]-whole;
            }
        }
        if (constructible && type.nextLevel>=0 && world.catalogs->buildings->at(type.nextLevel).available)
            bp.hardSpaceForUpgrade = hardSpace(type.nextLevel,true) && buildLevels[bp.owner().number]>=scene.buildingTypes->at(type.nextLevel).semantics.requiredWorkerLevel;
    }
    auto& up = panels.unit;
    if (up.valid)
    {
        const auto index = world.entities->unitSlotIndices.at(up.state().gid);
        const auto& u = world.entities->units.at(index);
        up.unitHungry = !world.rules->values.hungerDisabled && u.hungry <= (u.carriedMaterial==-1 ? u.trigHungry : u.trigHungryCarrying);
        const auto& types = world.catalogs->unitTypes[u.typeNum];
        up.realArmor = u.performance[ARMOR]/up.glassCannonScale - u.fruitCount*types[u.level[ARMOR]].armorReductionPerHappyness;
        up.nextLevelThreshold = (u.experienceLevel+1)*(u.experienceLevel+1)*types[u.level[ATTACK_STRENGTH]].experiencePerLevel;
    }
}
}

size_t SceneExtractor::preparationChunks(const SceneInputs& input)
{
    return 2 + OverlayArea::chunks(input.world,OverlayArea::OverlayType(input.request.view.overlay)) + (size_t(input.world.width)*input.world.height+1023)/1024 + (input.world.entities->units.size()+255)/256 + 2*((input.world.entities->buildings.size()+255)/256);
}

void SceneExtractor::prepare(const SceneInputs& input, PresentationFrame& scene)
{
    for (size_t chunk=0; chunk<preparationChunks(input); ++chunk) while (!prepareChunk(input,scene,chunk)) {}
}

bool SceneExtractor::prepareChunk(const SceneInputs& input, PresentationFrame& scene, size_t chunk)
{
    if (chunk == 0)
    {
        // A cancelled large footprint preparation may leave many entries.
        // Retire them within the same bounded claims used to build the index.
        for (size_t budget = 1024; budget && !connectionCandidates.empty(); --budget)
            connectionCandidates.erase(connectionCandidates.begin());
        if (!connectionCandidates.empty()) return false;
        candidateBuilding = candidateCell = connectionBuilding = 0;
        // An interrupted overlay has updated its request key but has not
        // replaced the completed cache. Do not reuse that cache under the new key.
        if (pendingOverlay) { pendingOverlay.reset(); overlay.reset(); }
        buildLevels.fill(0);
        auto map=std::move(scene.map);
        auto connections=std::move(scene.entities.connectionMasks);
        scene = PresentationFrame{};
        scene.map=std::move(map);
        scene.entities.connectionMasks=std::move(connections);
        prepareSource(input,scene);
        return true;
    }
    --chunk;
    const size_t mapChunks=(size_t(input.world.width)*input.world.height+1023)/1024;
    if (chunk<mapChunks) {scene.map.prepareChunk(chunk*1024,1024);return true;}
    chunk-=mapChunks;
    const auto& units = input.world.entities->units;
    const auto& buildings = input.world.entities->buildings;
    const size_t unitChunks = (units.size()+255)/256, buildingChunks = (buildings.size()+255)/256;
    auto& e = scene.entities;
    if (chunk < unitChunks)
    {
      for (size_t i=chunk*256; i<std::min(units.size(),(chunk+1)*256); ++i)
      {
        const auto& u = units[i];
        if (u.performance[BUILD]) buildLevels[u.team]=std::max(buildLevels[u.team],int(u.constructionLevel));
        if (u.carriedMaterial >= 0 && validMaterial(u.carriedMaterial)) e.materialPresence |= 1u << u.carriedMaterial;
    }
      return true;
    }
    chunk -= unitChunks;
    if (chunk < buildingChunks)
    {
        const size_t end = std::min(buildings.size(), (chunk + 1) * 256);
        size_t budget = 1024;
        while (candidateBuilding < end && budget) {
            const auto& b = buildings[candidateBuilding];
            const auto* type = e.type(b);
            if (!type->semantics.occupiesGround && type->presentation.connectionGroupId >= 0) {
                const size_t cells = size_t(type->width) * type->height;
                while (candidateCell < cells && budget) {
                    const int dx = candidateCell % type->width, dy = candidateCell / type->width;
                    connectionCandidates.emplace(int(scene.map.coordToIndex(b.posX + dx, b.posY + dy)), &b);
                    ++candidateCell;
                    --budget;
                }
                if (candidateCell < cells) return false;
            }
            const auto* materials = e.materials(b);
            for (unsigned m = 0; m < MaterialCount; ++m)
                if (materials[m]) e.materialPresence |= 1u << m;
            ++candidateBuilding;
            candidateCell = 0;
            if (budget) --budget;
        }
        return candidateBuilding == end;
    }
    chunk -= buildingChunks;
    if (chunk<buildingChunks) {
        const size_t end = std::min(buildings.size(), (chunk + 1) * 256);
        const size_t count = std::min(size_t(8), end - connectionBuilding);
        prepareConnections(scene.map, e, connectionCandidates, connectionBuilding, count);
        connectionBuilding += count;
        return connectionBuilding == end;
    }
    chunk-=buildingChunks;
    if (chunk == 0)
    {
        for (size_t budget = 1024; budget && !connectionCandidates.empty(); --budget)
            connectionCandidates.erase(connectionCandidates.begin());
        if (!connectionCandidates.empty()) return false;
        preparePanels(input, scene, buildLevels);
        return true;
    }
    --chunk;
    const auto& request=input.request;
    const auto requested=OverlayArea::OverlayType(request.view.overlay);
    const auto count=OverlayArea::chunks(input.world,requested);
    if (chunk>=count) throw std::out_of_range("Presentation preparation chunk");
    if (chunk==0 && !pendingOverlay) {
        const Uint32 window=scene.tick ? (scene.tick-1)/25 : 0;
        if (requested==OverlayArea::None) overlay.reset();
        else if (!overlay || requested!=overlayType || window!=overlayWindow || request.localTeam!=overlayTeam
            || input.world.worldIdentity!=overlayWorld || input.world.configurationRevision!=overlayConfiguration
            || (input.world.tick==overlayTick && (input.world.observationRevision!=overlayObservation
                || input.world.mapGenerations!=overlayMapGenerations)))
            pendingOverlay=std::make_shared<OverlayArea>();
        overlayWorld=input.world.worldIdentity;overlayConfiguration=input.world.configurationRevision;
        overlayObservation=input.world.observationRevision;overlayTick=input.world.tick;
        overlayMapGenerations=input.world.mapGenerations;
        overlayType=requested;overlayWindow=window;overlayTeam=request.localTeam;
    }
    if (pendingOverlay && !pendingOverlay->computeChunk(input.world,requested,request.localTeam,input.world.session->fertilityMaximum,chunk))
        return false;
    if (chunk+1==count) {
        if (pendingOverlay) overlay=std::exchange(pendingOverlay,{});
        if (overlay || !scene.editor) scene.overlay=overlay;
    }
    return true;
}

SimulationSnapshot::Requirements SceneExtractor::requirements(const SceneRequest& request)
{
    using namespace SimulationSnapshot;
    auto required=bit(Component::Catalogs)|bit(Component::Terrain)|bit(Component::Resources)
        |bit(Component::Occupancy)|bit(Component::Areas)|bit(Component::Visibility)
        |bit(Component::Entities)|bit(Component::Teams)|bit(Component::Rules)|bit(Component::Session)|bit(Component::Effects);
    if (request.includePanels) required|=bit(Component::Statistics);
    if (request.includeHistory) required|=bit(Component::History);
    if (request.includeTelemetry) required|=bit(Component::Telemetry);
    if (!request.selectedBuilding.empty() || request.view.debugLayers) required|=bit(Component::EntityDiagnostics);
    if (request.includeScriptAreas) required|=bit(Component::Annotations);
    return required;
}

std::shared_ptr<SceneInputs> SceneExtractor::inputs(const SimulationSnapshot::Handle& world,const SceneRequest& request)
{ return std::make_shared<SceneInputs>(SceneInputs{world,request}); }
void SceneExtractor::prepare(const SimulationSnapshot::Handle& world,const SceneRequest& request,PresentationFrame& scene)
{ prepare(SceneInputs{world,request},scene); }
