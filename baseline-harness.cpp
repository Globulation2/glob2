
#include <array>
#include <list>
#include <cassert>
#include <iostream>
#include "src/common/Integrity.h"
#include "src/team/LiveSlotList.h"
struct Unit { static constexpr int MAX_COUNT=1024; bool valid=true; int calls=0; bool integrity(){++calls;return valid;} };
struct Type { bool isVirtual=true; };
struct Runtime { bool attractsRole(int)const{return true;} };
struct Building { static constexpr int MAX_COUNT=1024; bool valid=true; int calls=0; int gid=0; Type* type=nullptr; Runtime* runtime=nullptr; bool integrity(){++calls;return valid;} static int GIDtoID(int id){return id;} };
struct Team {
 int noMoreBuildingSitesCountdown=0; static constexpr int noMoreBuildingSitesCountdownMax=200;
 std::array<Unit*,1024> myUnits{};std::array<Building*,1024> myBuildings{};
 LiveSlotList<Unit> liveUnits;LiveSlotList<Building> liveBuildings;
 std::list<Building*> virtualBuildings,clearingFlags;
 bool integrity();
 void rebuild(){liveUnits.rebuild(myUnits.data(),1024);liveBuildings.rebuild(myBuildings.data(),1024);}
};
bool Team::integrity(void)
{
	checkInvariant(noMoreBuildingSitesCountdown<=noMoreBuildingSitesCountdownMax);
	// The live lists must name exactly the occupied slots, in slot order. This
	// runs every tick, so it rides along the existing slot sweeps.
	std::size_t liveBuilding = 0;
	for (int id=0; id<Building::MAX_COUNT; id++)
	{
		Building *b=myBuildings[id];
		if (b)
		{
			checkInvariant(liveBuilding < liveBuildings.size() && liveBuildings.slots()[liveBuilding] == id && liveBuildings.entries()[liveBuilding] == b);
			++liveBuilding;
			checkInvariant(b->integrity());
		}
	}
	checkInvariant(liveBuilding == liveBuildings.size());
	for (std::list<Building *>::iterator it=virtualBuildings.begin(); it!=virtualBuildings.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant((*it)->type);
		checkInvariant((*it)->type->isVirtual);
		checkInvariant(myBuildings[Building::GIDtoID((*it)->gid)]);
	}
	for (std::list<Building *>::iterator it=clearingFlags.begin(); it!=clearingFlags.end(); ++it)
	{
		checkInvariant(*it);
		checkInvariant((*it)->type);
		checkInvariant((*it)->runtime->attractsRole(0));
		checkInvariant(myBuildings[Building::GIDtoID((*it)->gid)]);
	}

	std::size_t liveUnit = 0;
	for (int i=0; i<Unit::MAX_COUNT; i++)
	{
		Unit *u=myUnits[i];
		if (u)
		{
			checkInvariant(liveUnit < liveUnits.size() && liveUnits.slots()[liveUnit] == i && liveUnits.entries()[liveUnit] == u);
			++liveUnit;
			checkInvariant(u->integrity());
		}
	}
	checkInvariant(liveUnit == liveUnits.size());
	return true;
}
struct Fixture {
 Team t; Unit a,b;Building x,y;
 Fixture(){ t.myUnits[0]=&a;t.myUnits[3]=&b;t.myBuildings[0]=&x;t.myBuildings[3]=&y;t.rebuild();assert(t.integrity()); }
};
int main(){
 int passed=0;
 {Team t;assert(t.integrity());++passed;}
 {Fixture f;f.t.myUnits[1023]=&f.b;f.t.myUnits[3]=nullptr;f.t.myBuildings[1023]=&f.y;f.t.myBuildings[3]=nullptr;f.t.rebuild();assert(f.t.integrity());++passed;}
 {Team t;std::array<Unit,1024> u;std::array<Building,1024>b;for(int i=0;i<1024;++i){t.myUnits[i]=&u[i];t.myBuildings[i]=&b[i];}t.rebuild();assert(t.integrity());for(int i=0;i<1024;++i){assert(u[i].calls==1);assert(b[i].calls==1);}++passed;}
 for(int k=0;k<8;++k){Fixture f;switch(k){
 case 0:f.t.liveUnits.clear();break;
 case 1:f.t.liveUnits.detach(0);f.t.liveUnits.attach(1,&f.a);break;
 case 2:f.t.liveUnits.attach(0,&f.b);break;
 case 3:f.t.liveUnits.attach(1023,&f.a);break;
 case 4:f.t.liveBuildings.clear();break;
 case 5:f.t.liveBuildings.detach(0);f.t.liveBuildings.attach(1,&f.x);break;
 case 6:f.t.liveBuildings.attach(0,&f.y);break;
 case 7:f.t.liveBuildings.attach(1023,&f.x);break;
 }assert(!f.t.integrity());f.t.rebuild();assert(f.t.integrity());++passed;}
 {Fixture f;f.a.valid=false;assert(!f.t.integrity());++passed;}
 {Fixture f;f.x.valid=false;assert(!f.t.integrity());++passed;}
 std::cout<<passed<<" focused integrity scenarios passed\n";
}
