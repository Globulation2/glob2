// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuildingDemands.h"
#include "Game.h"
#include <iterator>

namespace AISharedRuntime
{
int importLegacyBuildingId(Game& game,int family,int level,bool site)
{
 constexpr const char* names[]={"swarm","inn","hospital","racetrack","swimmingpool","barracks","school","defencetower","explorationflag","warflag","clearingflag","stonewall","market"};
 if(family<0 || family>=int(std::size(names))) return -1;
 int id=game.buildingsTypes.getTypeNum(names[family],level,site);
 if(id<0 && site) id=game.buildingsTypes.getTypeNum(names[family],level,false);
 return id;
}
}
