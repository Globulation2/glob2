// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PowerOfTwo.h"
#include "ai/observation/AIWorldView.h"
#include "CortexQueryScratch.h"
#include "Building.h"
#include "BuildingType.h"
#include "Unit.h"
#include "Team.h"
#include "Order.h"
#include "Brush.h"
#include "ai/model/BuildingProjection.h"
#include <algorithm>
#include <array>
#include <optional>
#include <vector>
#include <utility>

namespace Cortex
{
// Pure queries read the canonical captured records. Only explicitly changed
// planning fields are stored separately; no entity or world is reconstructed.
struct BuildingIntent
{
    BuildingRef target;
    std::optional<int> workers, priority, minLevel, x, y;
    std::optional<std::array<Sint32, NB_UNIT_TYPE>> ratios;
};
using PlanningIntent = std::vector<BuildingIntent>;
inline const BuildingIntent* intentFor(const PlanningIntent& intents,const AIEngine::BuildingView& b)
{
    const auto it=std::find_if(intents.begin(),intents.end(),[&](const auto& intent){return intent.target==b.identity;});
    return it==intents.end()?nullptr:&*it;
}
inline BuildingIntent& intentFor(PlanningIntent& intents,const AIEngine::BuildingView& b)
{
    if(const auto* existing=intentFor(std::as_const(intents),b))return intents[existing-intents.data()];
    intents.push_back({b.identity});return intents.back();
}
#define CORTEX_INTENT_ACCESSOR(name,member,field) \
inline int name(const PlanningIntent& intents,const AIEngine::BuildingView& b) \
{ const auto* intent=intentFor(intents,b);return intent&&intent->member?*intent->member:b.field; }
CORTEX_INTENT_ACCESSOR(plannedWorkers,workers,maxUnitWorking)
CORTEX_INTENT_ACCESSOR(plannedPriority,priority,priority)
CORTEX_INTENT_ACCESSOR(plannedMinLevel,minLevel,minLevelToFlag)
CORTEX_INTENT_ACCESSOR(plannedX,x,posX)
CORTEX_INTENT_ACCESSOR(plannedY,y,posY)
#undef CORTEX_INTENT_ACCESSOR
inline int plannedRatio(const PlanningIntent& intents,const AIEngine::BuildingView& b,int type)
{const auto* intent=intentFor(intents,b);return intent&&intent->ratios?(*intent->ratios)[type]:b.ratio[type];}
inline const BuildingType* catalogType(const AIEngine::AIWorldView& world,int index)
{return &world.catalog->at(index).resolvedType;}
inline const BuildingType* buildingType(const AIEngine::AIWorldView& world,const AIEngine::BuildingView& b)
{return catalogType(world,b.typeNum);}
inline const BuildingType& completedType(const AIEngine::AIWorldView& world,const BuildingType& type)
{return type.isBuildingSite&&type.nextLevel>=0?*catalogType(world,type.nextLevel):type;}
// Resolve a site before applying the unchanged learned-model classification.
inline int modelChannel(const AIEngine::AIWorldView& world,const BuildingType& type)
{ const auto& complete=completedType(world,type);return ModelBuildingProjection::channelCompleted(complete); }
int finishedType(const AIEngine::AIWorldView&,const std::string&);
int maxBuildLevel(const AIEngine::AIWorldView&,const AIEngine::TeamView&);
inline int warpDistMax(const AIEngine::AIWorldView& map,int x,int y,int xx,int yy)
{
    const auto distance=[](int a,int b,int period){Sint64 d=Sint64(a)-Sint64(b);if(d<0)d=-d;if(d>=period)d=dimensionRemainder(d,period);return int(d>period/2?period-d:d);};
    return std::max(distance(x,xx,map.width),distance(y,yy,map.height));
}
inline auto getResource(const AIEngine::AIWorldView& map,int x,int y){return map.resourceAt(map.tileIndex(x,y)).resource;}
inline auto getBuilding(const AIEngine::AIWorldView& map,int x,int y){return map.occupancyAt(map.tileIndex(x,y)).building;}
inline bool isResource(const AIEngine::AIWorldView& map,int x,int y){return getResource(map,x,y).type!=NO_RES_TYPE;}
inline bool isResourceTakeable(const AIEngine::AIWorldView& map,int x,int y,int resource){const auto r=getResource(map,x,y);return r.type==resource&&r.amount>0;}
inline bool isFOWDiscovered(const AIEngine::AIWorldView& map,int x,int y,Uint32 mask){return map.visibilityAt(map.tileIndex(x,y)).visible&mask;}
inline bool isMapDiscovered(const AIEngine::AIWorldView& map,int x,int y,Uint32 mask){return map.visibilityAt(map.tileIndex(x,y)).discovered&mask;}
inline bool isForbidden(const AIEngine::AIWorldView& map,int x,int y,Uint32 mask){return map.areasAt(map.tileIndex(x,y)).forbidden&mask;}
inline bool isFarmArea(const AIEngine::AIWorldView& map,int x,int y,Uint32 mask){return map.areasAt(map.tileIndex(x,y)).farm&mask;}
inline bool canPaintFarmArea(const AIEngine::AIWorldView& map,int x,int y){return map.canPaintFarmAt(map.tileIndex(x,y));}
inline const TerrainProperties& terrainPropertiesAt(const AIEngine::AIWorldView& map,int x,int y){return map.terrainPropertiesAt(map.tileIndex(x,y));}
inline bool isHardSpaceForGroundUnitAt(const AIEngine::AIWorldView& map,std::size_t index,bool swim,Uint32 mask,bool free=false)
{
 if(MapState::resourceBlocksGround(map.state(),index))return false;
 const auto& o=map.occupancyAt(index);
 if(o.building!=0xffff||(free&&o.groundUnit!=0xffff)||(map.areasAt(index).forbidden&mask))return false;
 const auto& p=map.terrainPropertiesAt(index);return p.walkable||(swim&&p.swimmable);
}
inline bool isHardSpaceForGroundUnit(const AIEngine::AIWorldView& map,int x,int y,bool swim,Uint32 mask){return isHardSpaceForGroundUnitAt(map,map.tileIndex(x,y),swim,mask);}
inline bool isFreeForGroundUnitNoForbidden(const AIEngine::AIWorldView& map,int x,int y,bool swim){return isHardSpaceForGroundUnitAt(map,map.tileIndex(x,y),swim,0,true);}
inline bool isHardSpaceForBuildingAt(const AIEngine::AIWorldView& map,std::size_t index,Uint16 ignore=0xffff,bool free=false)
{
 if(MapState::resourceBlocksBuilding(map.state(),index))return false;
 const auto& o=map.occupancyAt(index);return (o.building==0xffff||o.building==ignore)&&(!free||o.groundUnit==0xffff)&&map.terrainPropertiesAt(index).buildable;
}
inline bool isHardSpaceForBuilding(const AIEngine::AIWorldView& map,int x,int y,int width=1,int height=1,Uint16 ignore=0xffff)
{for(int dy=0;dy<height;++dy)for(int dx=0;dx<width;++dx)if(!isHardSpaceForBuildingAt(map,map.tileIndex(x+dx,y+dy),ignore))return false;return true;}
bool checkRoomForBuilding(const AIEngine::AIWorldView&,int,int,const BuildingType*,int,const PlanningIntent&);
bool permittedQueuedOrder(const AIEngine::AIWorldView&,Order&);
template<class AreaOrder> std::shared_ptr<Order> areaOrder(Uint8 team,Uint8 mode,BrushAccumulator* accumulator,const AIEngine::AIWorldView*)
{
 auto order=std::make_shared<AreaOrder>();BrushAccumulator::AreaDimensions d;accumulator->getBitmap(&order->mask,&d);
 order->teamNumber=team;order->type=mode;order->centerX=d.centerX;order->centerY=d.centerY;order->minX=d.minX;order->minY=d.minY;order->maxX=d.maxX;order->maxY=d.maxY;return order;
}
}
