// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ai/observation/AIWorldView.h"
#include "BuildingCapabilities.h"
#include "Building.h"
#include "Unit.h"
#include "Team.h"
#include "GameHeader.h"
#include "Game.h"
#include "Order.h"
#include <array>
#include <memory>
#include <vector>
#include <list>

namespace AISharedRuntime::Read
{
// Compatibility names are confined to the AI domain. Every field comes from
// owned observation data; none of these objects inherits a simulation entity.
struct World;
struct Team;
struct Unit;
struct Map;
struct Building
{
    const BuildingType* type=nullptr;
    Team* team=nullptr;
    Uint16 gid=NOGBID;
    Uint32 scriptIdentity=0, seenByMask=0;
    int posX=0,posY=0,typeNum=0,buildingState=0,constructionResultState=0;
    int hp=0,maxHp=0,maxUnitWorking=0,maxUnitWorkingFuture=0,maxUnitInside=0;
    int unitStayRange=0,priority=0,minLevelToFlag=0,minLevelToWorker=0;
    int desiredMaxUnitWorking=0,bullets=0;
    std::list<Unit*> unitsWorking,unitsInside;
    Uint8 underAttackTimer=0;
    std::array<Sint32,MAX_NB_RESOURCES> resources{};
    std::array<Sint32,NB_UNIT_TYPE> ratio{};
    std::array<bool, BUILDING_ACCESS_COUNT> locked{};
    int originType=-1;
    bool upgradeAvailable=false,hardSpaceUpgrade=false,hardSpaceRepair=false;
    int getEffectiveMaxHp() const {return maxHp;}
    int getConstructionOriginTypeNum() const {return originType;}
    int getConstructionCompletionTypeNum() const {return constructionResultState==::Building::REPAIR ? originType : type->nextLevel;}
    bool isUpgradeAvailable() const {return upgradeAvailable;}
    bool isHardSpaceForBuildingSite(int operation) const {return operation==::Building::REPAIR ? hardSpaceRepair : hardSpaceUpgrade;}
};
struct Unit
{
    Team* team=nullptr;
    Uint16 gid=NOGUID;
    Uint32 scriptIdentity=0;
    int posX=0,posY=0,typeNum=0,activity=0,movement=0;
    Uint8 underAttackTimer=0;
    int trigHungry=0,hungriness=0,medical=0,displacement=0,hp=0,carriedResource=0,destinationPurpose=0,constructionLevel=0,experienceLevel=0,attackScale=1;
    bool isDead=false;
    std::array<Sint32,NB_ABILITY> performance{},level{};
    std::array<bool,NB_ABILITY> canLearn{};
    Building* attachedBuilding=nullptr;
    int workerLevel() const {return constructionLevel;}
    int getRealAttackStrength() const {return (performance[ATTACK_STRENGTH]+experienceLevel)*attackScale;}
    bool needsTraining(const BuildingTrainingSpec& training,int ability) const {return training.enabled && canLearn[ability] && (training.unitMask&(1u<<typeNum)) && (level[ability]<training.targetLevel || (typeNum==WORKER && constructionLevel<training.constructionLevel));}
};
struct Statistics
{
    TeamStat latest;
    TeamStat* getLatestStat() {return &latest;}
    const TeamStat* getLatestStat() const {return &latest;}
    int getWorkersLevel(int level) const {return latest.workersByConstructionLevel[level];}
    int getWorkersBalance() const {return latest.isFree[WORKER]-latest.totalNeeded;}
    int getStarvingUnits() const {return latest.needFoodCritical;}
};
struct Team
{
    World* game=nullptr;
    Map* map=nullptr;
    std::list<Building*> swarms;
    int teamNumber=0,prestige=0,startPosX=0,startPosY=0;
    Uint32 me=0,allies=0,enemies=0,sharedVisionExchange=0,sharedVisionFood=0,sharedVisionOther=0;
    bool isAlive=false;
    Statistics stats;
    std::array<Building*,::Building::MAX_COUNT> myBuildings{};
    std::array<Unit*,::Unit::MAX_COUNT> myUnits{};
    int maxBuildLevel() const {int result=0;for(const auto* unit:myUnits) if(unit && unit->performance[BUILD]!=0) result=std::max(result,unit->workerLevel());return result;}
    Uint32 attackableTeams() const {return enemies;}
};
struct Catalog
{
    BuildingsTypes types;
    AIPlanning::BuildingCapabilityIndex capabilities;
    explicit Catalog(const AIEngine::AIWorldView::Catalog& values)
        : types(makeTypes(values)), capabilities(types) {}
    static BuildingsTypes makeTypes(const AIEngine::AIWorldView::Catalog& values)
    {
        std::vector<BuildingType> types;types.reserve(values.size());
        for(const auto& value:values) types.push_back(value.resolvedType);
        return BuildingsTypes::fromObservation(std::move(types));
    }
};
struct Map
{
    const AIEngine::AIWorldView* world=nullptr;
    int wMask=0,hMask=0;
    std::size_t coordToIndex(int x,int y) const {return world->tileIndex(x,y);}
    Uint64 terrainGeneration() const {return world->terrainRevision;}
    bool hasAirTerrainConstraints() const {return world->airTerrainConstraints;}
    bool hasTerrainMovementModifiers() const {return world->terrainMovementModifiers;}
    TerrainType terrainTypeAt(std::size_t i) const {return world->terrainAt(i).type;}
    const TerrainRegistry& terrainRegistry() const {return *world->terrain;}
    const TerrainProperties& terrainPropertiesAt(std::size_t i) const {return terrainRegistry().properties(terrainTypeAt(i));}
    const Fertility::GrowthCache& resourceGrowthField() const {return *world->growth;}
    bool canResourcesGrow(int x,int y) const {return world->resourceAt(coordToIndex(x,y)).mayGrow;}
    bool isFOWDiscovered(int x,int y,Uint32 team) const {return (world->visibilityAt(coordToIndex(x,y)).visible&team)!=0;}
    int warpDistMax(int x,int y,int a,int b) const {int dx=std::abs(normalizeX(x)-normalizeX(a)),dy=std::abs(normalizeY(y)-normalizeY(b));return std::max(std::min(dx,getW()-dx),std::min(dy,getH()-dy));}
    bool isHardSpaceForBuilding(int x,int y,int w=1,int h=1,Uint16 ignore=NOGBID) const {for(int dy=0;dy<h;++dy)for(int dx=0;dx<w;++dx){const auto i=coordToIndex(x+dx,y+dy);const auto r=world->resourceAt(i);const auto o=world->occupancyAt(i);if(r.resource.type!=NO_RES_TYPE || (o.building!=NOGBID && o.building!=ignore) || !terrainPropertiesAt(i).buildable)return false;}return true;}
    int getW() const {return world->width;}
    int getH() const {return world->height;}
    int normalizeX(int x) const {return world->normalizeX(x);}
    int normalizeY(int y) const {return world->normalizeY(y);}
    int warpDistSquare(int x1,int y1,int x2,int y2) const {return world->distanceSquared(x1,y1,x2,y2);}
    AIEngine::TileView getTile(int x,int y) const {return world->tile(x,y);}
    // Component-only projection for compound spatial queries. canPaintFarm stays
    // false; use canPaintFarmArea/getTile when derived farm eligibility is needed.
    AIEngine::TileView getSpatialTile(int x,int y) const
    {
        const auto i=coordToIndex(x,y);
        const auto t=world->terrainAt(i);const auto r=world->resourceAt(i);
        const auto o=world->occupancyAt(i);const auto a=world->areasAt(i);const auto v=world->visibilityAt(i);
        AIEngine::TileView result;
        result.terrain=t.type;result.legacyTerrain=t.legacy;
        result.resource=r.resource;result.fertility=r.fertility;result.resourcesMayGrow=r.mayGrow;
        result.building=o.building;result.groundUnit=o.groundUnit;result.airUnit=o.airUnit;result.immobileUnit=o.immobileUnit;
        result.forbidden=a.forbidden;result.guard=a.guard;result.clear=a.clear;result.farm=a.farm;
        result.discovered=v.discovered;result.visible=v.visible;
        return result;
    }
    auto getResource(int x,int y) const {return world->resourceAt(coordToIndex(x,y)).resource;}
    Uint16 getBuilding(int x,int y) const {return world->occupancyAt(coordToIndex(x,y)).building;}
    Uint16 getGroundUnit(int x,int y) const {return world->occupancyAt(coordToIndex(x,y)).groundUnit;}
    bool isResource(int x,int y) const {return getResource(x,y).type!=NO_RES_TYPE;}
    bool isResourceTakeable(int x,int y,int type) const {const auto r=getResource(x,y);return r.type==type && r.amount>0;}
    bool isForbidden(int x,int y,Uint32 team) const {return (world->areasAt(coordToIndex(x,y)).forbidden&team)!=0;}
    bool isGuardArea(int x,int y,Uint32 team) const {return (world->areasAt(coordToIndex(x,y)).guard&team)!=0;}
    bool isClearArea(int x,int y,Uint32 team) const {return (world->areasAt(coordToIndex(x,y)).clear&team)!=0;}
    bool isFarmArea(int x,int y,Uint32 team) const {return (world->areasAt(coordToIndex(x,y)).farm&team)!=0;}
    bool isMapDiscovered(int x,int y,Uint32 team) const {return (world->visibilityAt(coordToIndex(x,y)).discovered&team)!=0;}
    bool farmAreasEnabled() const {return world->farmAreasEnabled;}
    bool canPaintFarmArea(int x,int y) const {return world->canPaintFarmAt(coordToIndex(x,y));}
    const TerrainProperties& terrainPropertiesAt(int x,int y) const {return world->terrain->properties(world->terrainAt(coordToIndex(x,y)).type);}
};
struct MapHeader
{
    int count=0;
    int getNumberOfTeams() const {return count;}
};
struct World
{
    const AIEngine::AIWorldView& source;
    std::shared_ptr<const Catalog> catalog;
    const BuildingsTypes& buildingsTypes;
    const GameHeader& gameHeader;
    MapHeader mapHeader;
    Map map;
    Uint32 stepCounter=0;
    int totalPrestige=0;
    std::array<Team*,::Team::MAX_COUNT> teams{};
    std::vector<Team> teamValues;
    std::vector<Building> buildingValues;
    std::vector<Unit> unitValues;
    World(const AIEngine::AIWorldView& world,std::shared_ptr<const Catalog> definitions)
        : source(world),catalog(std::move(definitions)),buildingsTypes(catalog->types),gameHeader(*world.configuration),map{&world},stepCounter(world.tick)
    {
        totalPrestige=world.totalPrestige;
        map.wMask=world.width-1;map.hMask=world.height-1;
        teamValues.reserve(world.teams.size());
        for(const auto& value:world.teams) {
            Team team;team.game=this;team.map=&map;team.teamNumber=value.number;team.prestige=value.prestige;
            team.startPosX=value.startX;team.startPosY=value.startY;team.me=value.mask;
            team.allies=value.allies;team.enemies=value.enemies;team.isAlive=value.alive;
            team.sharedVisionFood=value.foodVision;team.sharedVisionExchange=value.exchangeVision;team.sharedVisionOther=value.otherVision;
            team.stats.latest=value.statistics;
            teamValues.push_back(std::move(team));
        }
        for(auto& team:teamValues) {teams[team.teamNumber]=&team;mapHeader.count=std::max(mapHeader.count,team.teamNumber+1);}
        buildingValues.reserve(world.buildings.size());
        for(const auto& value:world.buildings) {
            Building building;building.gid=value.identity.gid;building.scriptIdentity=value.identity.generation;
            building.typeNum=value.typeNum;building.type=buildingsTypes.get(value.typeNum);building.team=teams[value.team];
            building.posX=value.posX;building.posY=value.posY;building.buildingState=value.buildingState;building.constructionResultState=value.constructionResultState;
            building.hp=value.hp;building.maxHp=value.maxHp;building.maxUnitWorking=value.maxUnitWorking;building.maxUnitWorkingFuture=value.maxUnitWorkingFuture;
            building.desiredMaxUnitWorking=value.desiredMaxUnitWorking;building.bullets=value.bullets;
            building.maxUnitInside=value.maxUnitInside;building.unitStayRange=value.unitStayRange;building.priority=value.priority;
            building.minLevelToFlag=value.minLevelToFlag;building.minLevelToWorker=value.minWorkerLevelToFlag;
            building.seenByMask=value.seenByMask;building.underAttackTimer=value.underAttackTimer;std::copy_n(world.buildingResources(value).data(),MAX_NB_RESOURCES,building.resources.begin());
            std::copy_n(value.ratio,NB_UNIT_TYPE,building.ratio.begin());
            std::copy_n(value.locked,BUILDING_ACCESS_COUNT,building.locked.begin());building.originType=value.constructionOriginTypeNum;building.upgradeAvailable=value.upgradeAvailable;
            building.hardSpaceUpgrade=value.hardSpaceUpgrade;building.hardSpaceRepair=value.hardSpaceRepair;
            buildingValues.push_back(std::move(building));
        }
        for(auto& building:buildingValues) building.team->myBuildings[::Building::GIDtoID(building.gid)]=&building;
        for(const auto& value:world.teams) for(const auto& ref:value.swarms)
            if(auto* building=teams[value.number]->myBuildings[::Building::GIDtoID(ref.gid)]) teams[value.number]->swarms.push_back(building);
        unitValues.reserve(world.units.size());
        for(const auto& value:world.units) {
            Unit unit;unit.team=teams[value.team];unit.gid=value.identity.gid;unit.scriptIdentity=value.identity.generation;unit.typeNum=value.typeNum;unit.posX=value.posX;unit.posY=value.posY;
            unit.trigHungry=value.trigHungry;unit.hungriness=value.hungriness;unit.medical=value.medical;unit.displacement=value.displacement;unit.hp=value.hp;unit.isDead=value.isDead;
            unit.carriedResource=value.carriedResource;unit.destinationPurpose=value.destinationPurpose;unit.constructionLevel=value.constructionLevel;
            unit.experienceLevel=value.experienceLevel;unit.attackScale=gameHeader.getGlassCannonScale();std::copy_n(value.performance, NB_ABILITY, unit.performance.begin());std::copy_n(value.level, NB_ABILITY, unit.level.begin());std::copy_n(value.canLearn, NB_ABILITY, unit.canLearn.begin());
            unit.activity=value.activity;unit.movement=value.movement;unit.underAttackTimer=value.underAttackTimer;
            unitValues.push_back(unit);
        }
        for(auto& unit:unitValues) teams[::Unit::GIDtoTeam(unit.gid)]->myUnits[::Unit::GIDtoID(unit.gid)]=&unit;
        auto unitFor=[&](const UnitRef& ref)->Unit* {if(ref.gid==NOGUID)return nullptr;auto* unit=teams[::Unit::GIDtoTeam(ref.gid)]->myUnits[::Unit::GIDtoID(ref.gid)];return unit && unit->scriptIdentity==ref.generation ? unit : nullptr;};
        for(std::size_t i=0;i<world.buildings.size();++i){for(const auto& ref:world.workers(world.buildings[i]))buildingValues[i].unitsWorking.push_back(unitFor(ref));for(const auto& ref:world.occupants(world.buildings[i]))buildingValues[i].unitsInside.push_back(unitFor(ref));}
        for(std::size_t i=0;i<world.units.size();++i){const auto ref=world.units[i].attached;if(ref.gid!=NOGBID)unitValues[i].attachedBuilding=teams[::Building::GIDtoTeam(ref.gid)]->myBuildings[::Building::GIDtoID(ref.gid)];}
    }
    bool isBuildingTypeAvailable(int type) const {return type>=0 && std::size_t(type)<buildingsTypes.size() && buildingsTypes.get(type)->runtimeAvailable;}
    int teamsCount() const {return mapHeader.count;}
    const AIPlanning::BuildingCapabilityIndex& buildingCapabilities() const {return catalog->capabilities;}
    bool checkRoomForBuilding(int x,int y,const BuildingType* type,int team,bool checkFow=true) const
    {
        if(!type->semantics.occupiesGround) {
            for(const auto* building:teams[team]->myBuildings)
                if(building && !building->type->semantics.occupiesGround && building->posX==map.normalizeX(x) && building->posY==map.normalizeY(y)) return false;
            return true;
        }
        bool discovered=false;
        for(int dy=0;dy<type->height;++dy) for(int dx=0;dx<type->width;++dx) {
            const auto i=map.coordToIndex(x+dx,y+dy);
            const auto r=map.world->resourceAt(i);const auto o=map.world->occupancyAt(i);
            if(r.resource.type!=NO_RES_TYPE || o.building!=NOGBID || o.groundUnit!=NOGUID || !map.terrainPropertiesAt(i).buildable) return false;
            discovered|=(map.world->visibilityAt(i).discovered&teams[team]->me)!=0;
        }
        return !checkFow || discovered;
    }
};
struct Player
{
    unsigned number=0;
    World* game=nullptr;
    Team* team=nullptr;
    Map* map=nullptr;
};
inline bool usefulWithoutTraining(const World& game,int type)
{
    const auto& index=game.buildingCapabilities();
    for(unsigned value=0;value<static_cast<unsigned>(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(AIPlanning::BuildingCapabilityIndex::trainingAbility(intent)>=0
            || intent==AIPlanning::BuildingIntent::TrainConstruction) continue;
        if(index.available(type,intent,game.gameHeader)) return true;
    }
    return false;
}
// A disabled operation does not make another service of the same building
// useless. Neutral structures, such as passive barriers, remain placeable.
inline bool usefulBuilding(const World& game,int type)
{
    if(type<0 || size_t(type)>=game.buildingsTypes.size()) return false;
    const auto* placement=game.buildingsTypes.get(type);
    if(!placement->runtimeAvailable) return false;
    const int completed=placement->isBuildingSite ? placement->nextLevel : type;
    const auto& index=game.buildingCapabilities();
    bool recognized=false;
    for(unsigned value=0;value<static_cast<unsigned>(AIPlanning::BuildingIntent::Count);++value)
    {
        const auto intent=static_cast<AIPlanning::BuildingIntent>(value);
        if(!index.matches(completed,intent)) continue;
        recognized=true;
        if(index.available(completed,intent,game.gameHeader)) return true;
    }
    return !recognized;
}
inline std::shared_ptr<Order> createOrder(const World& game,int team,int x,int y,
    int type,int workers,int futureWorkers)
{
    const auto* placement=game.buildingsTypes.get(type);
    const auto* completed=placement->isBuildingSite ? game.buildingsTypes.get(placement->nextLevel) : placement;
    return std::make_shared<OrderCreate>(team,x,y,type,
        std::clamp(workers,0,placement->semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed->semantics.assignmentLimit));
}

inline std::shared_ptr<Order> constructionOrder(const World& game,const Building& building,
    int workers,int futureWorkers)
{
    const bool repair=building.type->isBuildingSite ? building.constructionResultState==::Building::REPAIR
        : building.hp<building.getEffectiveMaxHp();
    const int target=building.type->isBuildingSite ? building.typeNum
        : repair ? building.type->prevLevel : building.type->nextLevel;
    const auto* placement=target>=0 ? game.buildingsTypes.get(target) : building.type;
    // Repair restores the origin even when its site is shared with another
    // lineage whose normal forward completion is a different variant.
    const int origin=building.type->isBuildingSite ? building.getConstructionOriginTypeNum() : building.typeNum;
    const auto* completed=repair && origin>=0 ? game.buildingsTypes.get(origin)
        : placement->isBuildingSite ? game.buildingsTypes.get(placement->nextLevel) : placement;
    return std::make_shared<OrderConstruction>(building.gid,
        std::clamp(workers,0,placement->semantics.assignmentLimit),
        std::clamp(futureWorkers,0,completed->semantics.assignmentLimit));
}

// Planning gates prevent new unavailable work. This additional guard drains
// orders restored from older saves, whose controllers did not have those gates.
// It carries no saved state and leaves the standard queue untouched.
inline bool permittedQueuedOrder(World& game, Order& order)
{
    const auto& rules=game.gameHeader;
    if(order.getOrderType()==ORDER_CREATE)
    {
        const auto& create=static_cast<const OrderCreate&>(order);
        return usefulBuilding(game,create.typeNum);
    }
    if(rules.isUnitUpgradesDisabled() && order.getOrderType()==ORDER_CONSTRUCTION)
    {
        const auto gid=static_cast<const OrderConstruction&>(order).gid;
        if(gid>=::Building::MAX_COUNT*::Team::MAX_COUNT) return false;
        auto* team=game.teams[::Building::GIDtoTeam(gid)];
        auto* building=team ? team->myBuildings[::Building::GIDtoID(gid)] : nullptr;
        return building && (building->type->isBuildingSite
            || (building->type->semantics.repairable && building->hp<building->getEffectiveMaxHp()));
    }
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MODIFY_SWARM)
        return static_cast<const OrderModifySwarm&>(order).ratio[WARRIOR]==0;
    if(rules.isPeacefulModeEnabled() && order.getOrderType()==ORDER_MOVE_FLAG)
    {
        const auto gid=static_cast<const OrderMoveFlag&>(order).gid;
        if(gid>=::Building::MAX_COUNT*::Team::MAX_COUNT) return false;
        auto* team=game.teams[::Building::GIDtoTeam(gid)];
        auto* building=team ? team->myBuildings[::Building::GIDtoID(gid)] : nullptr;
        return !building || !building->type->zonable[WARRIOR]
            || building->type->zonable[WORKER] || building->type->zonable[EXPLORER];
    }
    return true;
}
inline std::shared_ptr<Order> missingProductionOrder(World& game, Team& team,
    const std::array<int, NB_UNIT_TYPE>& desired, int workers, int futureWorkers,
    const std::vector<int>& pendingPlacements)
{
    const auto& index = game.buildingCapabilities();
    unsigned required = 0, provided = 0;
    for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
        if (desired[unit] > 0 && AIPlanning::BuildingCapabilityIndex::allowed(static_cast<AIPlanning::BuildingIntent>(unit), game.gameHeader))
            required |= 1u << unit;
    if (!required) return {};

    Building* anchor = nullptr;
    auto includeProvider = [&](int type) {
        if (type < 0 || std::size_t(type) >= game.buildingsTypes.size()) return;
        const auto* descriptor = game.buildingsTypes.get(type);
        if (descriptor->isBuildingSite) type = descriptor->nextLevel;
        provided |= unsigned(index.intentMask(type)) & ((1u<<NB_UNIT_TYPE)-1);
    };
    for (int id = 0; id < ::Building::MAX_COUNT; ++id)
    {
        auto* building = team.myBuildings[id];
        if (!building || building->buildingState != ::Building::ALIVE) continue;
        includeProvider(building->type->isBuildingSite
            ? building->getConstructionCompletionTypeNum() : building->typeNum);
        if (!anchor || (index.intentMask(building->typeNum) & ((1u<<NB_UNIT_TYPE)-1))) anchor = building;
        if ((provided & required) == required) return {};
    }
    for (int type : pendingPlacements) includeProvider(type);
    if ((provided & required) == required || !anchor) return {};

    for (int unit = 0; unit < NB_UNIT_TYPE; ++unit)
    {
        if (!(required & (1u << unit)) || (provided & (1u << unit))) continue;
        const auto intent = static_cast<AIPlanning::BuildingIntent>(unit);
        for (const auto& candidate : index.placementsByCost(intent))
        {
            const auto* type = game.buildingsTypes.get(candidate.placementType);
            int eligible = 0;
            for (int level = type->semantics.requiredWorkerLevel; level < NB_UNIT_LEVELS; ++level)
                eligible += team.stats.getWorkersLevel(level);
            if (!index.available(candidate, intent, game.gameHeader) || eligible == 0) continue;
            // Try each provider in cached cost/ID order. An obstructed large
            // footprint must not hide a smaller usable alternative.
            for (int radius = 1; radius <= 32; ++radius)
                for (int dx = -radius; dx <= radius; ++dx)
                    for (int dy = -radius; dy <= radius; ++dy)
                    {
                        if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                        const int x = game.map.normalizeX(anchor->posX + dx);
                        const int y = game.map.normalizeY(anchor->posY + dy);
                        if (!game.map.isMapDiscovered(x, y, team.allies)
                            || !game.checkRoomForBuilding(x, y, type, team.teamNumber)) continue;
                        return createOrder(game, team.teamNumber, x, y,
                            candidate.placementType, workers, futureWorkers);
                    }
        }
    }
    return {};
}
} // namespace AISharedRuntime::Read
