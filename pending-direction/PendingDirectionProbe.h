// Private read-only diagnostic. Never published to the production PR.
#pragma once
#include "SeedCells.h"
#include "SnapshotGradient.h"
#include "Team.h"
#include <fstream>
#include <map>
#include <tuple>
namespace pending_direction {
struct Oracle {
 std::array<Uint64,5> generations{};
 std::vector<Uint16> values;
 Uint32 tick=0,epoch=0,identity=0;
};
inline void sample(Map& map,Building& b,int swim,int x,int y,BuildingRoute route,int kind){
 const char* path=std::getenv("GLOB2_PENDING_DIRECTIONS");if(!path)return;
 const auto gid=map.getGroundUnit(x,y);
 if(gid==NOGUID)return;
 Unit* u=map.game->teams[Unit::GIDtoTeam(gid)]->myUnits[Unit::GIDtoID(gid)];
 if(!u || u->swimClass()!=swim || u->posX!=x || u->posY!=y)return;
 static std::ofstream out(path);
 static bool header=false;
 if(!header){out<<"tick,kind,unit,unit_identity,building,building_identity,route,swim,medical,activity,assigned,x,y,target_x,target_y,dx,dy,variant,here,next,legal,terrain_legal,occupied,best_legal_score,chosen_score,at_goal,reachable,oracle_id\n";header=true;}
 static std::map<std::pair<unsigned,int>,Oracle> oracles;
 static unsigned oracleId=0;
 static Uint32 lastTick=~Uint32(0);
 if(lastTick!=map.game->stepCounter){oracles.clear();lastTick=map.game->stepCounter;}
 const int slot=b.routeSlot(swim,route);
 auto& oracle=oracles[{b.gid,slot}];
 auto generations=map.snapshotGenerations();generations[4]=0;
 if(oracle.values.empty() || oracle.generations!=generations || oracle.epoch!=b.refreshEpoch[slot] || oracle.identity!=b.scriptIdentity){
  oracle.generations=generations;oracle.epoch=b.refreshEpoch[slot];oracle.identity=b.scriptIdentity;
  oracle.values.resize(map.getW()*map.getH());
  const auto view=map.stateView();const auto seed=gradient_preparation::captureBuildingSeed(b,swim,route);
  gradient_preparation::buildingCells(view,seed,oracle.values.data(),[&](auto fn){fn(0,oracle.values.size());});
  BuildingGradientSearch search;search.begin(map,oracle.values.data(),swim);search.finish();++oracleId;
 }
 const auto& values=oracle.values;
 const auto here=values[map.coordToIndex(x,y)];
 const auto actual=b.resolveRoute(route);
 const int tx=actual==BuildingRoute::Footprint?b.getMidX():b.posX;
 const int ty=actual==BuildingRoute::Footprint?b.getMidY():b.posY;
 int ldx=tx-x,ldy=ty-y;
 if(ldx>map.getW()/2)ldx-=map.getW();else if(ldx<-map.getW()/2)ldx+=map.getW();
 if(ldy>map.getH()/2)ldy-=map.getH();else if(ldy<-map.getH()/2)ldy+=map.getH();
 const auto sign=[](int v){return (v>0)-(v<0);};
 int dx=sign(ldx),dy=sign(ldy);
 if(100*std::abs(ldx)>241*std::abs(ldy))dy=0;
 else if(100*std::abs(ldy)>241*std::abs(ldx))dx=0;
 const auto terrainLegal=[&](int a,int c){const auto& p=map.terrainPropertiesAt(x+a,y+c);return swim==WATER_ONLY_CLASS?p.swimmable:(p.walkable || (swim>0 && p.swimmable));};
 const auto legal=[&](int a,int c){return (a||c) && terrainLegal(a,c) && map.isFreeForGroundUnit(x+a,y+c,swim>0,u->owner->me);};
 int best=-1;
 for(int d=0;d<8;++d){int a,c;Unit::dxDyFromDirection(d,&a,&c);const auto i=map.coordToIndex(x+a,y+c);if(legal(a,c) && values[i]>here && values[i]>GRADIENT_UNREACHABLE)best=std::max(best,int(values[i])-map.stepCost(a,c,i,swim));}
 for(int variant=0;variant<2;++variant){
  int a=dx,c=dy;
  if(variant && (a||c) && !legal(a,c)){
   const int direction=Unit::directionFromDxDy(a,c);bool found=false;
   for(int off:{1,7,2,6,3,5,4}){Unit::dxDyFromDirection((direction+off)&7,&a,&c);if(legal(a,c)){found=true;break;}}
   if(!found)a=c=0;
  }
  const auto i=map.coordToIndex(x+a,y+c);const auto next=values[i];
  const bool free=legal(a,c),terrain=terrainLegal(a,c);
  const bool occupied=(a||c) && map.getGroundUnit(x+a,y+c)!=NOGUID;
  const int score=(a||c)&&next>GRADIENT_UNREACHABLE?int(next)-map.stepCost(a,c,i,swim):-1;
  out<<map.game->stepCounter<<','<<kind<<','<<u->gid<<','<<u->scriptIdentity<<','<<b.gid<<','<<b.scriptIdentity<<','<<int(actual)<<','<<swim<<','<<int(u->medical)<<','<<int(u->activity)<<','<<(u->targetBuilding==&b)<<','<<x<<','<<y<<','<<tx<<','<<ty<<','<<a<<','<<c<<','<<variant<<','<<here<<','<<next<<','<<free<<','<<terrain<<','<<occupied<<','<<best<<','<<score<<','<<(here==GRADIENT_AT_GOAL)<<','<<(here>GRADIENT_UNREACHABLE)<<','<<oracleId<<'\n';
 }
}
}
