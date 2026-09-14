#include "MaximaPlacementTest.h"
#include "../src/AIMaximaPlacement.h"

#include <algorithm>

CPPUNIT_TEST_SUITE_REGISTRATION(MaximaPlacementTest);

using namespace AIMaximaPlacement;

namespace
{
	BuildingProfile profile(int type,int w1,int h1,int w2,int h2,int w3,int h3)
	{
		BuildingProfile result;result.buildingType=type;
		const int widths[3]={w1,w2,w3},heights[3]={h1,h2,h3};
		for(int level=1;level<=3;++level)
		{
			BuildingLevelProfile value;value.level=level;
			value.footprint=Footprint(-widths[level-1]/2,-heights[level-1]/2,
				widths[level-1],heights[level-1]);
			value.serviceThroughput=level*5;value.capability=level;
			result.levels.push_back(value);
		}
		return result;
	}

	std::vector<BuildingProfile> profiles()
	{
		std::vector<BuildingProfile> result;
		result.push_back(profile(0,4,4,4,4,4,4));
		result.push_back(profile(1,2,2,2,2,3,3));
		result.push_back(profile(2,2,2,2,2,2,2));
		result.push_back(profile(3,4,4,6,6,6,6));
		result.push_back(profile(4,4,4,6,6,6,6));
		result.push_back(profile(5,4,4,4,4,4,4));
		result.push_back(profile(6,2,2,2,2,2,2));
		result.push_back(profile(7,2,2,2,2,2,2));
		return result;
	}

	Planner planner()
	{
		Planner result;result.configure(profiles(),1,2,6,5,7);return result;
	}

	WorldState grassWorld(int size=32)
	{
		WorldState world;world.reset(size,size);world.profiles=profiles();
		for(int y=0;y<size;++y)for(int x=0;x<size;++x)
		{world.tile(x,y).discovered=true;world.tile(x,y).grass=true;world.tile(x,y).protectedness=60;}
		return world;
	}

	DevelopmentIntent intent(int type)
	{
		DevelopmentIntent value;value.buildingType=type;value.unmetCount=1;
		value.priority=80;value.workers=2;return value;
	}

	DevelopmentLimits limits()
	{
		DevelopmentLimits value;value.newConstruction=3;return value;
	}
}

void MaximaPlacementTest::testTemplatesUseTerminalGeometry()
{
	Planner p=planner();std::string error;CPPUNIT_ASSERT(p.validateTemplates(&error));
	bool compactInn=false,expandableInn=false,hospital=false,school=false,racetrack=false;
	for(size_t i=0;i<p.templates().size();++i)
	{
		const DevelopmentTemplate& t=p.templates()[i];
		if(t.id==InnCompact){compactInn=true;CPPUNIT_ASSERT_EQUAL(size_t(4),t.slots.size());CPPUNIT_ASSERT_EQUAL(2,t.slots[0].maximumLevel);}
		if(t.id==InnExpandable){expandableInn=true;CPPUNIT_ASSERT_EQUAL(3,t.slots[0].maximumLevel);CPPUNIT_ASSERT_EQUAL(3,t.slots[0].terminalFootprint.width);}
		if(t.id==HospitalCompact){hospital=true;CPPUNIT_ASSERT_EQUAL(size_t(4),t.slots.size());CPPUNIT_ASSERT_EQUAL(3,t.slots[0].maximumLevel);}
		if(t.id==SchoolProtectedCampus){school=true;CPPUNIT_ASSERT_EQUAL(3,t.slots[0].maximumLevel);}
		if(t.buildingType==3&&t.id==StandaloneReserved){racetrack=true;CPPUNIT_ASSERT_EQUAL(6,t.slots[0].terminalFootprint.width);}
	}
	CPPUNIT_ASSERT(compactInn&&expandableInn&&hospital&&school&&racetrack);
}

void MaximaPlacementTest::testStrictAndFallbackWaterTiers()
{
	WorldState world=grassWorld();for(int x=0;x<world.width;++x){world.tile(x,0).water=true;world.tile(x,0).grass=false;}
	Planner p=planner();DevelopmentAction action;std::vector<DevelopmentIntent> intents(1,intent(2));
	CPPUNIT_ASSERT(p.selectAction(world,intents,limits(),action));
	CPPUNIT_ASSERT(!action.fallbackWaterTier);

	WorldState fallback=grassWorld(16);
	for(int y=0;y<fallback.height;++y)for(int x=0;x<fallback.width;++x)
	{
		fallback.tile(x,y).discovered=false;
		if(x==0){fallback.tile(x,y).water=true;fallback.tile(x,y).grass=false;fallback.tile(x,y).discovered=true;}
	}
	// A 2x2 parcel at x=5 plus its ring is legal; every x>=6 origin is hidden.
	for(int y=3;y<=8;++y)for(int x=4;x<=7;++x)fallback.tile(x,y).discovered=true;
	Planner q=planner();CPPUNIT_ASSERT(q.selectAction(fallback,intents,limits(),action));
	CPPUNIT_ASSERT(action.fallbackWaterTier);
}

void MaximaPlacementTest::testReservationsAndDeterministicChoice()
{
	WorldState world=grassWorld();Planner first=planner(),second=planner();
	std::vector<DevelopmentIntent> intents(1,intent(3));DevelopmentAction a,b;
	CPPUNIT_ASSERT(first.selectAction(world,intents,limits(),a));
	CPPUNIT_ASSERT(second.selectAction(world,intents,limits(),b));
	CPPUNIT_ASSERT_EQUAL(a.centerX,b.centerX);CPPUNIT_ASSERT_EQUAL(a.centerY,b.centerY);
	CPPUNIT_ASSERT(first.reserve(world,a));
	for(size_t i=0;i<a.parcelTiles.size();++i)CPPUNIT_ASSERT(first.isFootprintReserved(a.parcelTiles[i]));
	DevelopmentAction next;CPPUNIT_ASSERT(first.selectAction(world,intents,limits(),next));
	for(size_t i=0;i<next.parcelTiles.size();++i)
		CPPUNIT_ASSERT(!first.isFootprintReserved(next.parcelTiles[i]));
}

void MaximaPlacementTest::testCommittedCountsIncludeUnobservedBuilds()
{
	WorldState world=grassWorld();Planner p=planner();
	world.swimmingBuilders=2;
	WorldBuilding existing;existing.id=50;existing.buildingType=3;existing.level=1;
	existing.centerX=4;existing.centerY=4;existing.hp=existing.hpMax=100;
	world.buildings.push_back(existing);
	CPPUNIT_ASSERT_EQUAL(1,p.committedBuildingCount(world,3));

	DevelopmentIntent building=intent(3);DevelopmentLimits available=limits();
	available.newConstruction=4;DevelopmentAction action;
	CPPUNIT_ASSERT(p.selectAction(world,
		std::vector<DevelopmentIntent>(1,building),available,action));
	CPPUNIT_ASSERT(p.reserve(world,action));
	p.markIssued(action.id,100,world.tick);
	CPPUNIT_ASSERT_EQUAL(2,p.committedBuildingCount(world,3));

	WorldBuilding observed;observed.id=100;observed.buildingType=3;observed.level=1;
	observed.centerX=action.centerX;observed.centerY=action.centerY;
	observed.hp=observed.hpMax=100;observed.site=true;
	world.buildings.push_back(observed);
	// The observed site replaces, rather than adds to, the planner commitment.
	CPPUNIT_ASSERT_EQUAL(2,p.committedBuildingCount(world,3));
}

void MaximaPlacementTest::testReservationsPreserveResourcesOutsideImmediateFootprint()
{
	WorldState world=grassWorld();
	for(size_t i=0;i<world.tiles.size();++i)
	{
		world.tiles[i].clearableResource=true;
		world.tiles[i].resourceType=0;
		world.tiles[i].resourceAmount=5;
	}
	for(int y=10;y<=13;++y)for(int x=10;x<=13;++x)
	{
		world.tile(x,y).clearableResource=false;
		world.tile(x,y).resourceType=-1;
		world.tile(x,y).resourceAmount=0;
	}
	Planner p=planner();DevelopmentAction action;
	std::vector<DevelopmentIntent> intents(1,intent(3));
	CPPUNIT_ASSERT(p.selectAction(world,intents,limits(),action));
	for(int dy=0;dy<action.initialFootprint.height;++dy)
		for(int dx=0;dx<action.initialFootprint.width;++dx)
			CPPUNIT_ASSERT(!world.tile(action.centerX+action.initialFootprint.left+dx,
				action.centerY+action.initialFootprint.top+dy).clearableResource);
	bool preservedResource=false;
	for(size_t i=0;i<action.parcelTiles.size();++i)
		preservedResource=preservedResource||world.tiles[action.parcelTiles[i]].clearableResource;
	CPPUNIT_ASSERT(preservedResource);
	CPPUNIT_ASSERT(p.reserve(world,action));
	RejectionReason reason=RejectedReservation;
	CPPUNIT_ASSERT(p.revalidate(world,action,&reason,true));
}

void MaximaPlacementTest::testInnerBuildingsAvoidValuableFoodZones()
{
	WorldState world=grassWorld();Planner unpenalized=planner();
	unpenalized.mutablePolicy().foodZonePenaltyWeight=0;
	DevelopmentAction baseline;
	CPPUNIT_ASSERT(unpenalized.selectAction(world,
		std::vector<DevelopmentIntent>(1,intent(6)),limits(),baseline));
	world.tile(baseline.centerX,baseline.centerY).foodOpportunity=1000;

	Planner schoolPlanner=planner();DevelopmentAction school;
	CPPUNIT_ASSERT(schoolPlanner.selectAction(world,
		std::vector<DevelopmentIntent>(1,intent(6)),limits(),school));
	CPPUNIT_ASSERT(school.centerX!=baseline.centerX||school.centerY!=baseline.centerY);
	CPPUNIT_ASSERT_EQUAL(0,school.utility.foodZonePressure);

	// Food infrastructure is deliberately exempt from the proximity penalty;
	// projected farm loss still protects cells it would actually reserve.
	Planner innPlanner=planner();DevelopmentAction inn;
	CPPUNIT_ASSERT(innPlanner.selectAction(world,
		std::vector<DevelopmentIntent>(1,intent(1)),limits(),inn));
	CPPUNIT_ASSERT_EQUAL(0,inn.utility.foodZonePressure);
}

void MaximaPlacementTest::testRequiredSourceAndNegativeUtilityWait()
{
	WorldState world=grassWorld();Planner p=planner();DevelopmentIntent required=intent(1);
	required.requiredResourceType=1;std::vector<DevelopmentIntent> intents(1,required);DevelopmentAction action;
	CPPUNIT_ASSERT(!p.selectAction(world,intents,limits(),action));
	CPPUNIT_ASSERT_EQUAL(1,p.diagnostics().rejected[RejectedRequiredSource]);

	Planner negative=planner();PlacementPolicy& policy=negative.mutablePolicy();
	policy.unmetDemandWeight=policy.serviceGainWeight=policy.capabilityGainWeight=0;
	policy.parallelismGainWeight=policy.redundancyGainWeight=0;
	policy.roleLocationQualityWeight=policy.defendednessWeight=policy.compactnessWeight=0;
	policy.newlyReservedLandWeight=100;
	intents[0]=intent(2);CPPUNIT_ASSERT(!negative.selectAction(world,intents,limits(),action));
	CPPUNIT_ASSERT_EQUAL(1,negative.diagnostics().rejected[RejectedNegativeUtility]);
}

void MaximaPlacementTest::testLifecycleClassifiesOutcomes()
{
	WorldState world=grassWorld();Planner p=planner();std::vector<DevelopmentIntent> intents(1,intent(2));
	DevelopmentAction action;CPPUNIT_ASSERT(p.selectAction(world,intents,limits(),action));
	CPPUNIT_ASSERT(p.reserve(world,action));p.markIssued(action.id,42,0);
	world.tick=1;WorldBuilding building;building.id=42;building.buildingType=2;building.level=1;
	building.centerX=action.centerX;building.centerY=action.centerY;building.site=true;world.buildings.push_back(building);
	p.observe(world);CPPUNIT_ASSERT_EQUAL(SiteObserved,p.actions().find(action.id)->second.state);
	world.buildings.clear();world.tick=2;p.observe(world);
	CPPUNIT_ASSERT_EQUAL(DestroyedDuringConstruction,p.actions().find(action.id)->second.state);
}

void MaximaPlacementTest::testColonyPurposeDistanceCornAndBlockedIndependence()
{
	WorldState world=grassWorld(64);
	WorldBuilding home;home.id=1;home.buildingType=0;home.level=1;
	home.centerX=8;home.centerY=8;home.hp=home.hpMax=100;
	world.buildings.push_back(home);
	world.tile(40,40).resourceType=1;
	world.tile(40,40).resourceAmount=10;
	world.tile(40,40).permanentResource=true;
	DevelopmentIntent colony=intent(0);colony.purpose=ColonySeed;
	colony.requiredResourceType=1;

	Planner p=planner();p.adoptStartingBuildings(world);DevelopmentAction action;
	CPPUNIT_ASSERT(p.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),action));
	CPPUNIT_ASSERT_EQUAL(ColonySeed,action.purpose);
	CPPUNIT_ASSERT(action.utility.friendlyDistance>=24);
	CPPUNIT_ASSERT(action.utility.cornDistance<=10);
	CPPUNIT_ASSERT_EQUAL(std::min(100,
		(action.utility.friendlyDistance-24)*4),action.utility.frontierGain);
	CPPUNIT_ASSERT(p.reserve(world,action));
	CPPUNIT_ASSERT_EQUAL(1,p.activeBuildCount(0,ColonySeed));
	CPPUNIT_ASSERT_EQUAL(0,p.activeBuildCount(0,CoreCapacity));

	Planner blocked=planner();blocked.adoptStartingBuildings(world);
	blocked.mutablePolicy().colonyMinimumAnchorDistance=65;
	CPPUNIT_ASSERT(!blocked.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),action));
	CPPUNIT_ASSERT(blocked.blockedSignature(0,ColonySeed)!=0);
	CPPUNIT_ASSERT_EQUAL(uint32_t(0),blocked.blockedSignature(0,CoreCapacity));
	DevelopmentIntent core=intent(0);
	CPPUNIT_ASSERT(blocked.selectAction(world,
		std::vector<DevelopmentIntent>(1,core),limits(),action));
	CPPUNIT_ASSERT_EQUAL(CoreCapacity,action.purpose);
}

void MaximaPlacementTest::testColonyThreatAndConqueredScoring()
{
	WorldState world=grassWorld(64);
	WorldBuilding home;home.id=1;home.buildingType=0;home.level=1;
	home.centerX=8;home.centerY=8;home.hp=home.hpMax=100;
	world.buildings.push_back(home);
	world.tile(40,40).resourceType=1;
	world.tile(40,40).resourceAmount=10;
	world.tile(40,40).permanentResource=true;
	DevelopmentIntent colony=intent(0);colony.purpose=ColonySeed;
	colony.requiredResourceType=1;

	Planner baseline=planner();baseline.adoptStartingBuildings(world);
	DevelopmentAction first;
	CPPUNIT_ASSERT(baseline.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),first));
	Planner coreBefore=planner();coreBefore.adoptStartingBuildings(world);
	DevelopmentIntent core=intent(0);DevelopmentAction originalCore;
	CPPUNIT_ASSERT(coreBefore.selectAction(world,
		std::vector<DevelopmentIntent>(1,core),limits(),originalCore));
	for(size_t i=0;i<first.parcelTiles.size();++i)
		world.tiles[first.parcelTiles[i]].conqueredOpportunity=100;
	Planner coreAfter=planner();coreAfter.adoptStartingBuildings(world);
	DevelopmentAction unchangedCore;
	CPPUNIT_ASSERT(coreAfter.selectAction(world,
		std::vector<DevelopmentIntent>(1,core),limits(),unchangedCore));
	CPPUNIT_ASSERT_EQUAL(originalCore.centerX,unchangedCore.centerX);
	CPPUNIT_ASSERT_EQUAL(originalCore.centerY,unchangedCore.centerY);
	CPPUNIT_ASSERT_EQUAL(originalCore.utility.total,unchangedCore.utility.total);
	Planner preferred=planner();preferred.adoptStartingBuildings(world);
	DevelopmentAction selected;
	CPPUNIT_ASSERT(preferred.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),selected));
	CPPUNIT_ASSERT_EQUAL(100,selected.utility.conqueredGain);

	for(size_t i=0;i<world.tiles.size();++i)world.tiles[i].threat=36;
	Planner unsafe=planner();unsafe.adoptStartingBuildings(world);
	CPPUNIT_ASSERT(!unsafe.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),selected));
	CPPUNIT_ASSERT(unsafe.diagnostics().rejected[RejectedColonyThreat]>0);
}

void MaximaPlacementTest::testDisconnectedColonyRequiresSwimmingBuilders()
{
	WorldState world=grassWorld(64);
	for(size_t i=0;i<world.tiles.size();++i)
	{world.tiles[i].grass=false;world.tiles[i].water=true;}
	for(int y=0;y<=20;++y)for(int x=0;x<=20;++x)
	{world.tile(x,y).grass=true;world.tile(x,y).water=false;}
	for(int y=32;y<=60;++y)for(int x=32;x<=60;++x)
	{world.tile(x,y).grass=true;world.tile(x,y).water=false;}
	WorldBuilding home;home.id=1;home.buildingType=0;home.level=1;
	home.centerX=8;home.centerY=8;home.hp=home.hpMax=100;
	world.buildings.push_back(home);
	world.tile(45,45).resourceType=1;
	world.tile(45,45).resourceAmount=10;
	world.tile(45,45).permanentResource=true;
	DevelopmentIntent colony=intent(0);colony.purpose=ColonySeed;
	colony.requiredResourceType=1;colony.workers=4;

	Planner blocked=planner();blocked.adoptStartingBuildings(world);
	DevelopmentAction action;
	CPPUNIT_ASSERT(!blocked.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),action));
	CPPUNIT_ASSERT(blocked.diagnostics().rejected[RejectedIslandBuilders]>0);

	world.swimmingBuilders=4;
	Planner amphibious=planner();amphibious.adoptStartingBuildings(world);
	CPPUNIT_ASSERT(amphibious.selectAction(world,
		std::vector<DevelopmentIntent>(1,colony),limits(),action));
	CPPUNIT_ASSERT(action.requiresSwimmingBuilders);
	CPPUNIT_ASSERT(action.arteryTiles.empty());
}
