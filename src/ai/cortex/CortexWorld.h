// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ai/observation/AIWorldView.h"
#include "CortexQueryScratch.h"
#include "Building.h"
#include "BuildingType.h"
#include "Unit.h"
#include "Team.h"
#include "Order.h"
#include "Brush.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>
#include <iosfwd>

namespace Cortex
{
// A decision-scoped adapter, not simulation objects. All backing records are
// immutable snapshot values; local assignments are speculative intent only.
struct World;
struct WorldTeam;
struct WorldUnit
{
    static constexpr int MAX_COUNT = ::Unit::MAX_COUNT;
    static constexpr auto ACT_RANDOM = ::Unit::ACT_RANDOM;
    static constexpr auto MED_FREE = ::Unit::MED_FREE;
    static constexpr auto MED_HUNGRY = ::Unit::MED_HUNGRY;
    static constexpr auto MED_DAMAGED = ::Unit::MED_DAMAGED;
    static int GIDtoTeam(Uint16 gid) { return ::Unit::GIDtoTeam(gid); }
    static int GIDtoID(Uint16 gid) { return ::Unit::GIDtoID(gid); }
    int typeNum, posX, posY, hp, hungry, activity, medical, constructionLevel;
    Uint8 underAttackTimer;
    std::array<Sint32, NB_ABILITY> level, performance;
};
struct WorldBuilding
{
    static constexpr int MAX_COUNT = ::Building::MAX_COUNT;
    static constexpr auto ALIVE = ::Building::ALIVE;
    static constexpr auto DEAD = ::Building::DEAD;
    static constexpr auto NO_CONSTRUCTION = ::Building::NO_CONSTRUCTION;
    static constexpr auto UPGRADE = ::Building::UPGRADE;
    static constexpr auto REPAIR = ::Building::REPAIR;
    static int GIDtoTeam(Uint16 gid) { return ::Building::GIDtoTeam(gid); }
    static int GIDtoID(Uint16 gid) { return ::Building::GIDtoID(gid); }
    const AIEngine::BuildingView* source;
    WorldTeam* owner = nullptr;
    const BuildingType* type = nullptr;
    Uint16 gid;
    int typeNum, posX, posY, buildingState, constructionResultState, hp, maxUnitInside;
    int maxUnitWorking, priority, unitStayRange, minLevelToFlag;
    Uint32 seenByMask;
    Uint8 underAttackTimer;
    std::array<Sint32, MAX_NB_RESOURCES> resources;
    std::array<Sint32, NB_UNIT_TYPE> ratio;
    std::vector<WorldUnit*> unitsInside, unitsWorking;
    int getEffectiveMaxHp() const { return source->maxHp; }
    bool isUpgradeAvailable() const { return source->upgradeAvailable; }
    bool isHardSpaceForBuildingSite(int state) const
    { return state == UPGRADE ? source->hardSpaceUpgrade : source->hardSpaceRepair; }
    // Existing action code updates its decision-local intent for deduplication.
    // These never mutate simulation objects or derived simulation caches.
    void update() {}
    void updateCallLists() {}
};
struct WorldStats
{
    const AIEngine::TeamView* source;
    const TeamStat* getLatestStat() const { return &source->statistics; }
    int getStarvingUnits() const { return source->starving; }
};
struct WorldTeam
{
    static constexpr int MAX_COUNT = ::Team::MAX_COUNT;
    World* game;
    int teamNumber, prestige, startPosX, startPosY;
    Uint32 me, allies, enemies;
    bool isAlive;
    WorldStats stats;
    std::array<WorldBuilding*, ::Building::MAX_COUNT> myBuildings{};
    std::array<WorldUnit*, ::Unit::MAX_COUNT> myUnits{};
    std::vector<WorldBuilding*> virtualBuildings;
    int maxBuildLevel() const;
    Uint32 attackableTeams() const { return enemies; }
};
struct WorldMap
{
    const AIEngine::AIWorldView& source;
    QueryScratch* borrowedScratch = nullptr;
    QueryScratch localScratch;
    QueryScratch& queryScratch() { return borrowedScratch ? *borrowedScratch : localScratch; }
    int getW() const { return source.width; }
    int getH() const { return source.height; }
    int normalizeX(int x) const { return source.normalizeX(x); }
    int normalizeY(int y) const { return source.normalizeY(y); }
    unsigned coordToIndex(int x, int y) const { return source.tileIndex(x,y); }
    int warpDistMax(int x, int y, int xx, int yy) const
    {
        const auto distance=[](int a,int b,int period) {
            // Reduce the difference once, rather than normalizing both ends.
            // Widen subtraction so extreme wrapped inputs cannot overflow abs.
            Sint64 delta=Sint64(a)-Sint64(b);
            if(delta<0)delta=-delta;
            if(delta>=period)delta%=period;
            if(delta>period/2)delta=period-delta;
            return int(delta);
        };
        return std::max(distance(x,xx,getW()),distance(y,yy,getH()));
    }
    auto getResource(int x,int y) const { return source.resourceAt(source.tileIndex(x,y)).resource; }
    auto getBuilding(int x,int y) const { return source.occupancyAt(source.tileIndex(x,y)).building; }
    bool isResource(int x,int y) const { return getResource(x,y).type!=NO_RES_TYPE; }
    bool isResourceTakeable(int x,int y,int resource) const
    { const auto r=getResource(x,y); return r.type==resource && r.amount>0; }
    bool isFOWDiscovered(int x,int y,Uint32 mask) const { return source.visibilityAt(source.tileIndex(x,y)).visible & mask; }
    bool isMapDiscovered(int x,int y,Uint32 mask) const { return source.visibilityAt(source.tileIndex(x,y)).discovered & mask; }
    bool isForbidden(int x,int y,Uint32 mask) const { return source.areasAt(source.tileIndex(x,y)).forbidden & mask; }
    bool isFarmArea(int x,int y,Uint32 mask) const { return source.areasAt(source.tileIndex(x,y)).farm & mask; }
    bool canPaintFarmArea(int x,int y) const { return source.canPaintFarmAt(source.tileIndex(x,y)); }
    bool farmAreasEnabled() const { return source.farmAreasEnabled; }
    const TerrainProperties& terrainPropertiesAt(int x,int y) const
    { return source.terrain->properties(source.terrainAt(source.tileIndex(x,y)).type); }
    bool isHardSpaceForGroundUnitAt(std::size_t index,bool swim,Uint32 mask,bool requireFree=false) const
    {
        if(source.resourceAt(index).resource.type!=NO_RES_TYPE) return false;
        const auto occupancy=source.occupancyAt(index);
        if(occupancy.building!=0xffff || (requireFree&&occupancy.groundUnit!=0xffff)
            || (source.areasAt(index).forbidden&mask)) return false;
        const auto& p=source.terrain->properties(source.terrainAt(index).type);
        return p.walkable || (swim&&p.swimmable);
    }
    bool isHardSpaceForGroundUnit(int x,int y,bool swim,Uint32 mask) const
    { return isHardSpaceForGroundUnitAt(source.tileIndex(x,y),swim,mask); }
    bool isFreeForGroundUnitNoForbidden(int x,int y,bool swim) const
    {
        const auto index=source.tileIndex(x,y);
        return isHardSpaceForGroundUnitAt(index,swim,0,true);
    }
    bool isHardSpaceForBuildingAt(std::size_t index,Uint16 ignore=0xffff,bool requireFree=false) const
    {
        if(source.resourceAt(index).resource.type!=NO_RES_TYPE) return false;
        const auto occupancy=source.occupancyAt(index);
        return (occupancy.building==0xffff || occupancy.building==ignore)
            && (!requireFree || occupancy.groundUnit==0xffff)
            && source.terrain->properties(source.terrainAt(index).type).buildable;
    }
    bool isHardSpaceForBuilding(int x,int y,int width=1,int height=1,Uint16 ignore=0xffff) const
    {
        for(int dy=0;dy<height;++dy)for(int dx=0;dx<width;++dx)
            if(!isHardSpaceForBuildingAt(source.tileIndex(x+dx,y+dy),ignore))return false;
        return true;
    }
};
struct WorldCatalog
{
    const AIEngine::AIWorldView::Catalog& source;
    std::size_t size() const { return source.size(); }
    int getFinishedTypeNum(const std::string& key) const
    {
        for(std::size_t i=0;i<size();++i)if(source[i].key==key)
        { return source[i].site ? source[i].next : int(i); }
        return -1;
    }
    const BuildingType* get(int index) const { return &source.at(index).resolvedType; }
};
struct WorldRules
{
    const AIEngine::RuleView& source;
    bool isUnitUpgradesDisabled() const { return source.upgradesDisabled; }
    bool isHungerDisabled() const { return source.hungerDisabled; }
    bool isPeacefulModeEnabled() const { return source.peaceful; }
    bool isResourceGrowthDisabled() const { return source.resourceGrowthDisabled; }
};
struct World
{
    const AIEngine::AIWorldView& source;
    WorldMap map;
    WorldCatalog buildingsTypes;
    WorldRules gameHeader;
    Uint32 stepCounter;
    int totalPrestige;
    const decltype(AIEngine::AIWorldView::buildProjects)& buildProjects;
    std::vector<WorldTeam*> teams;
    std::vector<WorldTeam> teamStorage;
    std::vector<WorldBuilding> buildingStorage;
    std::vector<WorldUnit> unitStorage;
    // Borrow controller-owned capacity for repeated placement scans. Standalone
    // owner/test facades use invocation-local scratch. Neither retains a view.
    explicit World(const AIEngine::AIWorldView& view, QueryScratch* scratch = nullptr);
    std::vector<unsigned char>& placementProximityScratch()
    { return map.queryScratch().proximity; }
    int teamsCount() const { return teams.size(); }
    bool checkRoomForBuilding(int x,int y,const BuildingType* type,int team) const;
};
bool permittedQueuedOrder(const World&, Order&);
template<class AreaOrder> std::shared_ptr<Order> areaOrder(Uint8 team, Uint8 mode, BrushAccumulator* accumulator, const WorldMap*)
{
    auto order = std::make_shared<AreaOrder>();
    BrushAccumulator::AreaDimensions dimensions;
    accumulator->getBitmap(&order->mask, &dimensions);
    order->teamNumber = team; order->type = mode;
    order->centerX = dimensions.centerX; order->centerY = dimensions.centerY;
    order->minX = dimensions.minX; order->minY = dimensions.minY;
    order->maxX = dimensions.maxX; order->maxY = dimensions.maxY;
    return order;
}
struct WorldPlayer { World* game; WorldTeam* team; int number; std::ostream* diagnostics = nullptr; };
}
