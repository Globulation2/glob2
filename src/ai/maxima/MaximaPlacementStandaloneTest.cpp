#include "Glob2Test.h"
#include "AIMaximaContinuation.h"
#include <vector>
#include <string>
#include <utility>
#include <cmath>
#include "../../src/ai/maxima/AIMaximaPlacement.h"
#include "Version.h"
#include "FileFormatVersions.h"
#include <BinaryStream.h>
#include <StreamBackend.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>

namespace
{
using namespace AIMaximaPlacement;

static BuildingProfile makeProfile(int type,int initial,int terminal)
{
	BuildingProfile result;result.buildingType=type;
	for(int level=1;level<=3;++level)
	{
		BuildingLevelProfile value;value.level=level;
		const int size=level==3?terminal:initial;
		value.footprint=Footprint(-size/2,-size/2,size,size);
		value.serviceThroughput=level*5;value.capability=level;
		result.levels.push_back(value);
	}
	return result;
}

static std::vector<BuildingProfile> makeProfiles()
{
	std::vector<BuildingProfile> result;
	result.push_back(makeProfile(0,4,4));result.push_back(makeProfile(1,2,3));
	result.push_back(makeProfile(2,2,2));result.push_back(makeProfile(3,4,6));
	result.push_back(makeProfile(4,4,6));result.push_back(makeProfile(5,4,4));
	result.push_back(makeProfile(6,2,2));result.push_back(makeProfile(7,2,2));
	return result;
}

static WorldState makeWorld()
{
	WorldState world;world.reset(32,32);world.profiles=makeProfiles();
	for(int y=0;y<world.height;++y)for(int x=0;x<world.width;++x)
	{world.tile(x,y).discovered=true;world.tile(x,y).buildable=true;world.tile(x,y).protectedness=60;}
	for(int x=0;x<world.width;++x){world.tile(x,0).swimmable=true;world.tile(x,0).walkable=false;world.tile(x,0).fertilitySource=true;world.tile(x,0).buildable=false;}
	return world;
}

static void occupy(WorldState& world,const DevelopmentAction& action)
{
	for(int dy=0;dy<action.initialFootprint.height;++dy)
		for(int dx=0;dx<action.initialFootprint.width;++dx)
		{
			WorldTile& tile=world.tile(action.centerX+action.initialFootprint.left+dx,
				action.centerY+action.initialFootprint.top+dy);
			tile.occupied=true;tile.ownOccupied=true;
		}
}

static void upgradesUsuallyBeatNewConstruction()
{
	WorldState world=makeWorld();
	WorldBuilding building;building.id=10;building.buildingType=3;building.level=1;
	building.centerX=10;building.centerY=12;building.hp=building.hpMax=100;
	world.buildings.push_back(building);
	DevelopmentAction footprint;footprint.centerX=10;footprint.centerY=12;
	footprint.initialFootprint=world.profile(3)->atLevel(1)->footprint;occupy(world,footprint);
	DevelopmentLimits limits;limits.allowUpgrades=true;limits.level1Upgrades=1;
	limits.newConstruction=1;limits.upgradePriorities[{3,1}]=80;
	DevelopmentIntent build;build.buildingType=7;build.unmetCount=1;build.priority=100;
	for(bool emergency:{false,true})
	{
		build.emergency=emergency;
		Planner planner;planner.configure(world.profiles,1,2,6,5,7);
		// Demand dominates this fixture: a worthwhile upgrade normally beats
		// a somewhat stronger expansion bid, but not emergency construction.
		planner.mutablePolicy().unmetDemandWeight=100;
		planner.adoptStartingBuildings(world);
		Planner incremental=planner;
		DevelopmentAction selected,chunked;
		REQUIRE(planner.selectAction(world,{build},limits,selected));
		REQUIRE((selected.type==UpgradeBuilding)==!emergency);
		SelectionProgress progress;
		do {progress=incremental.selectActionIncremental(world,{build},limits,chunked,
			world.computeSignature(),128);}while(progress==SelectionPending);
		REQUIRE(progress==SelectionFound);
		REQUIRE((chunked.type==selected.type&&chunked.utility.total==selected.utility.total));
	}
}

static void fortificationIsLastResort()
{
	WorldState world=makeWorld();
	DevelopmentIntent tower;tower.buildingType=7;tower.purpose=Fortification;
	tower.unmetCount=1;tower.priority=100;
	DevelopmentIntent ordinary;ordinary.buildingType=2;
	ordinary.unmetCount=1;ordinary.priority=1;
	DevelopmentLimits limits;limits.newConstruction=1;
	Planner planner;planner.configure(world.profiles,1,2,6,5,7);
	planner.mutablePolicy().unmetDemandWeight=100;
	DevelopmentAction selected;
	REQUIRE(planner.selectAction(world,{tower,ordinary},limits,selected));
	REQUIRE(selected.buildingType==2);
	// Adding a fallback must leave ordinary construction's issue tick unchanged.
	int expectedPasses=0;
	for(bool fallback:{false,true})
	{
		Planner incremental=planner;SelectionProgress progress;int passes=0;
		const std::vector<DevelopmentIntent> intents=fallback
			? std::vector<DevelopmentIntent>{tower,ordinary}
			: std::vector<DevelopmentIntent>{ordinary};
		do {++passes;progress=incremental.selectActionIncremental(world,intents,limits,
			selected,world.computeSignature(),128);}while(progress==SelectionPending);
		REQUIRE((progress==SelectionFound && selected.buildingType==2));
		if(fallback)REQUIRE(passes==expectedPasses);else expectedPasses=passes;
	}
	REQUIRE(planner.selectAction(world,{tower},limits,selected));
	REQUIRE(selected.purpose==Fortification);
	REQUIRE(planner.reserve(world,selected));
	// The existing integer purpose field carries pending fortification through saves.
	auto backend=new GAGCore::MemoryStreamBackend;
	auto output=new GAGCore::BinaryOutputStream(backend);
	planner.save(output);backend->seekFromStart(0);
	auto inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
	GAGCore::BinaryInputStream input(inputBackend);Planner restored;
	restored.configure(world.profiles,1,2,6,5,7);
	REQUIRE(restored.load(&input,VERSION_MINOR));
	REQUIRE(restored.actions().at(selected.id).purpose==Fortification);

	// Even an upgrade or repair with a lower score beats a spare-labour tower.
	for(bool damaged:{false,true})
	{
		WorldState developed=makeWorld();
		WorldBuilding b;b.id=10;b.buildingType=3;b.level=1;
		b.centerX=10;b.centerY=12;b.hpMax=100;b.hp=damaged?80:100;
		developed.buildings.push_back(b);
		DevelopmentAction footprint;footprint.centerX=10;footprint.centerY=12;
		footprint.initialFootprint=developed.profile(3)->atLevel(1)->footprint;
		occupy(developed,footprint);
		Planner next;next.configure(developed.profiles,1,2,6,5,7);
		next.mutablePolicy().unmetDemandWeight=100;
		next.adoptStartingBuildings(developed);
		limits.allowUpgrades=true;limits.allowRepairs=true;limits.level1Upgrades=1;
		limits.upgradePriorities[{3,1}]=1;
		REQUIRE(next.selectAction(developed,{tower},limits,selected));
		REQUIRE(selected.type==(damaged?RepairBuilding:UpgradeBuilding));
		int ordinaryPasses=0;
		for(bool fallback:{false,true})
		{
			Planner incremental=next;SelectionProgress progress;int passes=0;
			const std::vector<DevelopmentIntent> intents=fallback
				? std::vector<DevelopmentIntent>{tower}:std::vector<DevelopmentIntent>{};
			do {++passes;progress=incremental.selectActionIncremental(developed,intents,
				limits,selected,developed.computeSignature(),128);
			}while(progress==SelectionPending);
			REQUIRE(progress==SelectionFound);
			REQUIRE(selected.type==(damaged?RepairBuilding:UpgradeBuilding));
			if(fallback)REQUIRE(passes==ordinaryPasses);else ordinaryPasses=passes;
		}
	}
}

static void placementReviewRegressions()
{
	{
		WorldState world=makeWorld();
		WorldBuilding building;building.id=10;building.buildingType=1;building.level=1;
		building.centerX=10;building.centerY=12;building.hp=building.hpMax=100;
		world.buildings.push_back(building);
		DevelopmentAction footprint;footprint.centerX=10;footprint.centerY=12;
		footprint.initialFootprint=world.profile(1)->atLevel(1)->footprint;occupy(world,footprint);
		world.tile(11,12).resourceType=0;world.tile(11,12).clearableResource=true;
		Planner planner;planner.configure(world.profiles,1,2,6,5,7);planner.adoptStartingBuildings(world);
		REQUIRE(planner.standaloneContracts().at(0).maximumLevel==2);
		DevelopmentLimits limits;limits.allowUpgrades=true;limits.level1Upgrades=1;
		DevelopmentAction upgrade;REQUIRE(planner.selectAction(world,{},limits,upgrade));
		REQUIRE(upgrade.targetLevel==2);
		const int reservation=planner.standaloneContracts()[0].reservationId;
		const uint32_t revision=planner.spatialRevision();
		planner.adoptStartingBuildings(world);REQUIRE(planner.spatialRevision()==revision);
		world.tile(11,12).resourceType=-1;world.tile(11,12).clearableResource=false;
		planner.adoptStartingBuildings(world);
		REQUIRE(planner.standaloneContracts().at(0).maximumLevel==3);
		REQUIRE(planner.standaloneContracts().at(0).reservationId==reservation);
		for(size_t index=0;index<world.tiles.size();++index)
			REQUIRE(!(planner.isFootprintReserved(index)&&planner.isCirculationReserved(index)));
	}
	{
		WorldState world=makeWorld();
		// Real racetracks grow from 4x4 to 6x6 on their first upgrade.
		world.profiles[3].levels[1].footprint=world.profiles[3].levels[2].footprint;
		Planner planner;planner.configure(world.profiles,1,2,6,5,7);
		DevelopmentIntent intent;intent.buildingType=3;intent.unmetCount=1;intent.priority=100;
		DevelopmentLimits limits;limits.newConstruction=1;DevelopmentAction build;
		REQUIRE(planner.selectAction(world,{intent},limits,build));REQUIRE(planner.reserve(world,build));
		planner.markIssued(build.id,20,0);
		WorldBuilding building;building.id=20;building.buildingType=3;building.level=1;
		building.centerX=build.centerX;building.centerY=build.centerY;building.hp=building.hpMax=100;
		world.buildings.push_back(building);occupy(world,build);planner.observe(world);
		for(int index:build.parcelTiles)if(!world.tiles[index].occupied)
		{world.tiles[index].resourceType=0;world.tiles[index].clearableResource=true;}
		limits.newConstruction=0;limits.allowUpgrades=true;limits.level1Upgrades=1;
		DevelopmentAction upgrade;REQUIRE(planner.selectAction(world,{},limits,upgrade));
		RejectionReason reason;
		REQUIRE(planner.revalidateSelection(world,{},limits,upgrade,&reason));
		REQUIRE(planner.reserve(world,upgrade));
		REQUIRE((!planner.revalidate(world,upgrade,&reason,true)&&reason==RejectedClearableResource));
		REQUIRE(planner.reservations().at(upgrade.reservationId).footprintTiles.size()==36);
		limits.activeLevel1Upgrades=1;
		REQUIRE(planner.revalidateSelection(world,{},limits,upgrade,&reason));
		limits.upgradePriorities[{3,1}]=0;
		REQUIRE((!planner.revalidateSelection(world,{},limits,upgrade,&reason)&&reason==RejectedAuthorization));
		limits.upgradePriorities[{3,1}]=100;
		// A pending clearing contract survives the existing save format.
		auto backend=new GAGCore::MemoryStreamBackend;
		auto output=new GAGCore::BinaryOutputStream(backend);planner.save(output);backend->seekFromStart(0);
		auto inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
		GAGCore::BinaryInputStream input(inputBackend);Planner restored;
		restored.configure(world.profiles,1,2,6,5,7);REQUIRE(restored.load(&input,VERSION_MINOR));
		REQUIRE(restored.actions().at(upgrade.id).state==ParcelReserved);
		for(int index:build.parcelTiles){world.tiles[index].resourceType=-1;world.tiles[index].clearableResource=false;}
		REQUIRE(restored.revalidateSelection(world,{},limits,upgrade,&reason));
		REQUIRE(restored.revalidate(world,upgrade,&reason,true));
		restored.markInvalidated(upgrade.id,UpgradeBlocked,world.computeSignature());
		REQUIRE(restored.reservations().size()==1);
		REQUIRE(restored.reservations().count(build.reservationId));
	}
	for(bool timeout:{false,true})
	{
		WorldState world=makeWorld();Planner planner;planner.configure(world.profiles,1,2,6,5,7);
		DevelopmentIntent intent;intent.buildingType=2;intent.unmetCount=4;intent.priority=100;
		DevelopmentLimits limits;limits.newConstruction=4;DevelopmentAction first,second;
		REQUIRE(planner.selectAction(world,{intent},limits,first));REQUIRE(first.templateId==HospitalCompact);
		REQUIRE(planner.reserve(world,first));planner.markIssued(first.id,10,0);
		REQUIRE(planner.selectAction(world,{intent},limits,second));REQUIRE(second.campusId==first.campusId);
		REQUIRE(planner.reserve(world,second));planner.markIssued(second.id,11,1);
		WorldBuilding building;building.id=11;building.buildingType=2;building.level=1;
		building.centerX=second.centerX;building.centerY=second.centerY;building.hp=building.hpMax=100;
		world.buildings.push_back(building);occupy(world,second);world.tick=2;planner.observe(world);
		if(timeout){world.tick=301;planner.observe(world);}
		else planner.markInvalidated(first.id,EngineRejected,world.computeSignature());
		REQUIRE(planner.campuses().size()==1);
		REQUIRE(planner.reservations().count(first.reservationId));
		limits.newConstruction=0;limits.level1Upgrades=1;limits.allowUpgrades=true;
		DevelopmentAction upgrade;REQUIRE(planner.selectAction(world,{},limits,upgrade));
		REQUIRE(upgrade.buildingId==11);
		// The shared contract is finally released when its last survivor dies.
		world.buildings.clear();for(auto& tile:world.tiles){tile.occupied=false;tile.ownOccupied=false;}
		planner.observe(world);REQUIRE((planner.campuses().empty()&&planner.reservations().empty()));
	}
}

// Food buildings are placed only where protected farm capacity can back them,
// except for the first one, which has nothing to be measured against.
static void foodLedgerPlacementRegression()
{
	WorldState world=makeWorld();
	Planner planner;planner.configure(world.profiles,1,2,6,5,7,0);
	PlacementPolicy& policy=planner.mutablePolicy();
	policy.foodLedgerEnabled=true;policy.foodSupplyRadius=6;
	policy.foodMarginPercent=100;policy.foodSwarmDemand=100;
	policy.foodInnDemand[0]=100;policy.foodInnDemand[1]=200;policy.foodInnDemand[2]=300;
	DevelopmentIntent intent;intent.buildingType=1;intent.unmetCount=2;intent.priority=100;
	DevelopmentLimits limits;limits.newConstruction=4;

	// The first inn bootstraps the settlement even with no protected wheat.
	DevelopmentAction first;
	REQUIRE(planner.selectAction(world,{intent},limits,first));
	REQUIRE(planner.reserve(world,first));
	planner.markIssued(first.id,30,0);

	// The second has to be backed by capacity, and there is none.
	DevelopmentAction second;
	REQUIRE(!planner.selectAction(world,{intent},limits,second));
	REQUIRE(planner.diagnostics().rejected[RejectedFoodCapacity]>0);

	// A protected farm appears, and the same request now succeeds beside it.
	const int farmX=24,farmY=24;
	for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		world.tile(farmX+dx,farmY+dy).protectedYield=80;
	DevelopmentAction backed;
	REQUIRE(planner.selectAction(world,{intent},limits,backed));
	// Judge the site by the capacity it can actually harvest rather than by a
	// distance proxy: harvesting routes are eight-neighbour steps and wrap, so a
	// site that looks distant in Manhattan terms can still work the same farm.
	REQUIRE(planner.reserve(world,backed));
	const AIMaximaFoodLedger::Result& after=planner.evaluateFoodLedger(world);
	const AIMaximaFoodLedger::ConsumerResult* placed=after.consumer(-(backed.id+1));
	REQUIRE((placed&&placed->claimed>0));
}

static void configureRelocation(PlacementPolicy& policy)
{
	policy.foodLedgerEnabled=true;policy.foodSupplyRadius=6;
	policy.foodUnreachablePenaltyTiles=8;policy.foodMarginPercent=100;
	policy.foodSwarmDemand=100;
	policy.foodInnDemand[0]=100;policy.foodInnDemand[1]=200;policy.foodInnDemand[2]=300;
	policy.relocationEnabled=true;policy.relocationMinGainTiles=3;
	policy.relocationMinCoverageGainPercent=25;
	policy.relocationPaybackHorizonTicks=15000;policy.relocationCostMarginPercent=125;
	policy.carrierTicksPerTile=23;policy.carrierFixedTicksPerTrip=105;
	policy.builderTicksPerStep=32;
	policy.relocationDistanceRealisationPercent=155;
}

// A stranded inn is rebuilt beside the farm it cannot reach: the replacement is
// sited with the old inn removed from the ledger, priced against its hauled
// materials, and the pair counts as one commitment until the old one is gone.
static void relocationAppraisalRegression()
{
	WorldState world=makeWorld();
	for(auto& profile:world.profiles)if(profile.buildingType==1)
		for(auto& level:profile.levels)level.constructionResources[0]=3;
	// Wood to rebuild from, and a protected farm the old inn cannot reach.
	world.tile(20,20).resourceType=0;world.tile(20,20).resourceAmount=5;
	world.tile(20,20).clearableResource=true;
	const int farmX=24,farmY=24;
	for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		world.tile(farmX+dx,farmY+dy).protectedYield=80;
	WorldBuilding old;old.id=10;old.buildingType=1;old.level=1;
	old.centerX=6;old.centerY=20;old.hp=old.hpMax=100;
	world.buildings.push_back(old);
	DevelopmentAction footprint;footprint.centerX=6;footprint.centerY=20;
	footprint.initialFootprint=world.profile(1)->atLevel(1)->footprint;occupy(world,footprint);

	Planner planner;planner.configure(world.profiles,1,2,6,5,7,0);
	configureRelocation(planner.mutablePolicy());
	planner.adoptStartingBuildings(world);
	const AIMaximaFoodLedger::ConsumerResult* stranded=
		planner.evaluateFoodLedger(world).consumer(10);
	REQUIRE((stranded&&stranded->coveragePercent==0&&stranded->quality==1400));

	DevelopmentIntent intent;intent.buildingType=1;intent.purpose=Relocation;
	intent.replacesBuildingId=10;intent.unmetCount=1;intent.priority=100;
	DevelopmentLimits limits;limits.newConstruction=4;
	DevelopmentAction selected;
	REQUIRE(planner.selectAction(world,{intent},limits,selected));
	REQUIRE((selected.purpose==Relocation&&selected.replacesBuildingId==10));
	// Harvesting routes are eight-neighbour steps: the site must be within the
	// supply radius of the farm in Chebyshev terms, plus its own footprint.
	const int gapX=std::min(std::abs(selected.centerX-farmX),world.width-std::abs(selected.centerX-farmX));
	const int gapY=std::min(std::abs(selected.centerY-farmY),world.height-std::abs(selected.centerY-farmY));
	REQUIRE(std::max(gapX,gapY)<=9);
	const Planner::RelocationAppraisal appraisal=
		planner.appraiseRelocation(world,selected,10);
	REQUIRE(appraisal.viable);
	REQUIRE((appraisal.oldCoverage==0&&appraisal.newCoverage==100));
	REQUIRE(appraisal.newQuality<appraisal.oldQuality);
	// Three hauled units, each a trip plus a build step, times the margin.
	REQUIRE((appraisal.cost>0&&appraisal.cost<3*(105+2*23*32+32)*125/100));
	REQUIRE((appraisal.paybackTicks>0&&appraisal.paybackTicks<=15000));
	REQUIRE(planner.committedBuildingCount(world,1)==1);
	REQUIRE(planner.reserve(world,selected));
	REQUIRE(planner.committedBuildingCount(world,1)==1);

	// The same request is refused when relocation is switched off.
	Planner refusing;refusing.configure(world.profiles,1,2,6,5,7,0);
	configureRelocation(refusing.mutablePolicy());
	refusing.mutablePolicy().relocationEnabled=false;
	refusing.adoptStartingBuildings(world);
	DevelopmentAction refused;
	REQUIRE(!refusing.selectAction(world,{intent},limits,refused));
	REQUIRE(refusing.diagnostics().rejected[RejectedNegativeUtility]>0);
	REQUIRE((refusing.relocationRefused(10)&&!planner.relocationRefused(10)));
	refusing.clearRelocationRefusal(10);
	REQUIRE(!refusing.relocationRefused(10));

	// A covered inn beside a small farm: only with the old inn removed from the
	// ledger is there capacity for its replacement, so the exclusion is what
	// lets the request be sited at all.
	WorldState tight=makeWorld();
	tight.tile(farmX,farmY).protectedYield=80;tight.tile(farmX+1,farmY).protectedYield=70;
	WorldBuilding covered;covered.id=11;covered.buildingType=1;covered.level=1;
	covered.centerX=farmX-6;covered.centerY=farmY;covered.hp=covered.hpMax=100;
	tight.buildings.push_back(covered);
	DevelopmentAction coveredFootprint;coveredFootprint.centerX=covered.centerX;
	coveredFootprint.centerY=covered.centerY;
	coveredFootprint.initialFootprint=tight.profile(1)->atLevel(1)->footprint;
	occupy(tight,coveredFootprint);
	Planner tightPlanner;tightPlanner.configure(tight.profiles,1,2,6,5,7,0);
	configureRelocation(tightPlanner.mutablePolicy());
	tightPlanner.adoptStartingBuildings(tight);
	const AIMaximaFoodLedger::Result& tightLedger=tightPlanner.evaluateFoodLedger(tight);
	REQUIRE((tightLedger.consumer(11)->coveragePercent==100&&tightLedger.totalResidual==50));
	DevelopmentIntent plain;plain.buildingType=1;plain.unmetCount=1;plain.priority=100;
	DevelopmentAction unsited;
	REQUIRE(!tightPlanner.selectAction(tight,{plain},limits,unsited));
	REQUIRE(tightPlanner.diagnostics().rejected[RejectedFoodCapacity]>0);
	DevelopmentIntent replace=plain;replace.purpose=Relocation;replace.replacesBuildingId=11;
	DevelopmentAction sited;
	REQUIRE(tightPlanner.selectAction(tight,{replace},limits,sited));
	REQUIRE(sited.replacesBuildingId==11);
	REQUIRE(tightPlanner.appraiseRelocation(tight,sited,11).newQuality
		<tightPlanner.appraiseRelocation(tight,sited,11).oldQuality);

	// A covered swarm is priced like an inn: a shorter route is a saving on
	// every unit that keeps flowing, and the realisation knob scales it.
	WorldState swarms=makeWorld();
	for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		swarms.tile(farmX+dx,farmY+dy).protectedYield=80;
	WorldBuilding swarm;swarm.id=20;swarm.buildingType=0;swarm.level=1;
	swarm.centerX=19;swarm.centerY=24;swarm.hp=swarm.hpMax=100;
	swarms.buildings.push_back(swarm);
	Planner swarmPlanner;swarmPlanner.configure(swarms.profiles,1,2,6,5,7,0);
	configureRelocation(swarmPlanner.mutablePolicy());
	swarmPlanner.adoptStartingBuildings(swarms);
	REQUIRE(swarmPlanner.evaluateFoodLedger(swarms).consumer(20)->coveragePercent==100);
	DevelopmentAction closer;closer.buildingType=0;closer.purpose=Relocation;
	closer.replacesBuildingId=20;closer.centerX=27;closer.centerY=24;
	closer.initialFootprint=swarms.profile(0)->atLevel(1)->footprint;
	const Planner::RelocationAppraisal moved=
		swarmPlanner.appraiseRelocation(swarms,closer,20);
	REQUIRE(moved.newQuality<moved.oldQuality);
	REQUIRE((moved.oldCoverage==100&&moved.newCoverage==100));
	REQUIRE((moved.savingPerTick>0&&moved.viable));
	swarmPlanner.mutablePolicy().relocationDistanceRealisationPercent=0;
	const Planner::RelocationAppraisal unpriced=
		swarmPlanner.appraiseRelocation(swarms,closer,20);
	REQUIRE((unpriced.savingPerTick==0&&!unpriced.viable));
}

// A checkpoint taken during a one-cell search must preserve both its winner
// and the exact number of remaining slices (order timing is game behavior).
static void placementContinuationRegression()
{
    for(int boundary:{1,7,83})
    {
        WorldState world=makeWorld();
        world.tiles[world.index(7,7)].woodReserve=true;
        world.tiles[world.index(8,7)].woodReserve=true;
        Planner uninterrupted,restored;
        uninterrupted.configure(makeProfiles(),1,2,6,5,7);
        restored.configure(makeProfiles(),1,2,6,5,7);
        DevelopmentIntent intent;intent.buildingType=3;
        intent.unmetCount=2;intent.priority=80;intent.workers=2;
        DevelopmentLimits limits;limits.newConstruction=2;
        DevelopmentAction expected,actual;
        for(int step=0;step<boundary;++step)
            REQUIRE(uninterrupted.selectActionIncremental(world,{intent},limits,
                expected,world.computeSignature(),1)==SelectionPending);
        auto backend=new GAGCore::MemoryStreamBackend;
        auto output=new GAGCore::BinaryOutputStream(backend);
        uninterrupted.save(output);
        uninterrupted.saveExecutionState(output);
        backend->seekFromStart(0);
        auto copy=new GAGCore::MemoryStreamBackend(*backend);
        delete output;
        GAGCore::BinaryInputStream input(copy);
        REQUIRE(restored.load(&input,VERSION_MINOR));
        // The execution state was just written by the current writer, so it is
        // read back at the current format rather than the older planner one.
        restored.loadExecutionState(&input,VERSION_MINOR);
        REQUIRE(restored.selectionWorld().tiles[world.index(7,7)].woodReserve);
        REQUIRE(restored.selectionWorld().tiles[world.index(8,7)].woodReserve);
        SelectionProgress progress=SelectionPending;
        for(int step=0;step<10000&&progress==SelectionPending;++step)
        {
            progress=uninterrupted.selectActionIncremental(world,{intent},limits,
                expected,world.computeSignature(),1);
            REQUIRE(restored.selectActionIncremental(world,{intent},limits,
                actual,world.computeSignature(),1)==progress);
        }
        REQUIRE(progress==SelectionFound);
        REQUIRE((actual.type==expected.type&&actual.buildingType==expected.buildingType));
        REQUIRE((actual.centerX==expected.centerX&&actual.centerY==expected.centerY));
        REQUIRE(actual.utility.total==expected.utility.total);
        REQUIRE(actual.parcelTiles==expected.parcelTiles);
        REQUIRE(actual.accessTiles==expected.accessTiles);
        REQUIRE(actual.arteryTiles==expected.arteryTiles);
    }
}

TEST_CASE("Frozen wood reservations survive saves while older planner layouts remain readable" *
          doctest::test_suite("Maxima.Placement"))
{
    WorldState world=makeWorld();
    world.tiles[world.index(7,7)].woodReserve=true;
    Planner original,restored,legacy;
    for(Planner* planner:{&original,&restored,&legacy})
        planner->configure(makeProfiles(),1,2,6,5,7);
    DevelopmentIntent intent;intent.buildingType=3;intent.unmetCount=2;
    DevelopmentLimits limits;limits.newConstruction=2;
    DevelopmentAction selected;
    REQUIRE(original.selectActionIncremental(world,{intent},limits,selected,
        world.computeSignature(),1)==SelectionPending);
    auto* memory=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream output(memory);
    original.save(&output);
    original.saveExecutionState(&output);
    const auto bytes=memory->takeContents();
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(std::string(bytes)));
    REQUIRE(restored.load(&input,VERSION_MINOR));
    restored.loadExecutionState(&input,VERSION_MINOR);
    REQUIRE(restored.selectionWorld().tiles[world.index(7,7)].woodReserve);
    CHECK(input.isEndOfStream());

    // The pre-137 body is unchanged. Determine its end using the old reader,
    // then prove it also loads when the new extension is absent entirely.
    GAGCore::BinaryInputStream oldBody(new GAGCore::MemoryStreamBackend(std::string(bytes)));
    REQUIRE(legacy.load(&oldBody,FILE_FORMAT_VERSION_PLACEMENT_WOOD_RESERVES-1));
    legacy.loadExecutionState(&oldBody,FILE_FORMAT_VERSION_PLACEMENT_WOOD_RESERVES-1);
    const auto oldSize=oldBody.getPosition();
    REQUIRE(oldSize<bytes.size());
    GAGCore::BinaryInputStream oldInput(new GAGCore::MemoryStreamBackend(bytes.substr(0,oldSize)));
    REQUIRE(restored.load(&oldInput,FILE_FORMAT_VERSION_PLACEMENT_WOOD_RESERVES-1));
    restored.loadExecutionState(&oldInput,FILE_FORMAT_VERSION_PLACEMENT_WOOD_RESERVES-1);
    CHECK_FALSE(restored.selectionWorld().tiles[world.index(7,7)].woodReserve);
    CHECK(oldInput.isEndOfStream());
}

static void adjoiningBarracksRegression()
{
	for(bool wrapped:{false,true})
	{
		WorldState world=makeWorld();Planner planner;
		planner.configure(world.profiles,1,2,6,5,7);
		WorldBuilding older;older.id=20;older.buildingType=5;older.level=1;
		older.centerX=wrapped?30:14;older.centerY=10;older.hp=older.hpMax=100;
		world.buildings.push_back(older);
		DevelopmentAction footprint;footprint.centerX=older.centerX;footprint.centerY=10;
		footprint.initialFootprint=world.profile(5)->atLevel(1)->footprint;
		occupy(world,footprint);
		if(wrapped)
		{
			for(auto& tile:world.tiles)tile.discovered=false;
			for(int y=7;y<=12;++y)for(int x=27;x<=36;++x)
				world.tile(world.normalizeX(x),y).discovered=true;
		}
		planner.adoptStartingBuildings(world);
		DevelopmentIntent intent;intent.buildingType=5;intent.priority=100;intent.unmetCount=1;
		DevelopmentLimits limits;limits.newConstruction=1;
		DevelopmentAction action;REQUIRE(planner.selectAction(world,{intent},limits,action));
		REQUIRE((action.type==BuildStandalone&&action.parcelTiles.size()==16));
		REQUIRE((action.accessTiles.size()==12&&action.utility.compactness==12));
		REQUIRE(world.wrappedManhattan(action.centerX,action.centerY,older.centerX,10)==4);
		if(wrapped)REQUIRE((action.centerX==2&&action.centerY==10));
		if(!wrapped)
		{
			// Compactness must not force a new barracks into an exposed site.
			WorldState exposed=world;
			for(int y=4;y<=16;++y)for(int x=8;x<=20;++x)
			{exposed.tile(x,y).threat=100;exposed.tile(x,y).protectedness=0;}
			Planner alternative=planner;DevelopmentAction safer;
			REQUIRE(alternative.selectAction(exposed,{intent},limits,safer));
			REQUIRE((safer.accessTiles.size()==16&&safer.utility.compactness==0));
		}
		// Incremental selection must make the same choice.
		Planner incremental=planner;DevelopmentAction chunked;SelectionProgress progress;
		do {progress=incremental.selectActionIncremental(world,{intent},limits,chunked,
			world.computeSignature(),128);}while(progress==SelectionPending);
		REQUIRE((progress==SelectionFound&&chunked.centerX==action.centerX
			&&chunked.centerY==action.centerY&&chunked.utility.total==action.utility.total));
		// Losing access after selection must veto the shared-edge conversion.
		WorldState blocked=world;
		for(int index:planner.reservations().begin()->second.circulationTiles)
			if(std::find(action.parcelTiles.begin(),action.parcelTiles.end(),index)==action.parcelTiles.end())
			{blocked.tiles[index].occupied=true;break;}
		REQUIRE(!planner.revalidate(blocked,action));
		REQUIRE(planner.reserve(world,action));
		REQUIRE(planner.campuses().empty());
		REQUIRE(planner.reservations().size()==2);
		int reserved=0;
		for(size_t index=0;index<planner.footprintReferences().size();++index)
		{
			reserved+=planner.footprintReferences()[index]!=0;
			REQUIRE(!(planner.footprintReferences()[index]&&planner.circulationReferences()[index]));
		}
		REQUIRE(reserved==32); // Exactly two actual barracks; no future slots.
		auto backend=new GAGCore::MemoryStreamBackend;
		auto output=new GAGCore::BinaryOutputStream(backend);planner.save(output);
		backend->seekFromStart(0);
		auto inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
		GAGCore::BinaryInputStream input(inputBackend);Planner restored;
		restored.configure(world.profiles,1,2,6,5,7);REQUIRE(restored.load(&input,VERSION_MINOR));
		REQUIRE(restored.revalidate(world,action,NULL,true));
		REQUIRE(restored.footprintReferences()==planner.footprintReferences());
		REQUIRE(restored.circulationReferences()==planner.circulationReferences());
		restored.markIssued(action.id,21,0);WorldBuilding newer=older;newer.id=21;
		newer.centerX=action.centerX;newer.centerY=action.centerY;
		world.buildings.push_back(newer);occupy(world,action);restored.observe(world);
		for(auto& building:world.buildings)
		{
			DevelopmentAction upgrade;upgrade.type=UpgradeBuilding;upgrade.buildingType=5;
			upgrade.buildingId=building.id;upgrade.centerX=building.centerX;upgrade.centerY=building.centerY;
			upgrade.fromLevel=1;upgrade.targetLevel=2;REQUIRE(restored.revalidate(world,upgrade));
			building.level=2;upgrade.fromLevel=2;upgrade.targetLevel=3;
			REQUIRE(restored.revalidate(world,upgrade));
		}
	}
}

static void mixedCampusRegression()
{
	for(int founder:{1,2,6})
	{
		WorldState world=makeWorld();Planner planner;
		planner.configure(world.profiles,1,2,6,5,7);
		DevelopmentLimits limits;limits.newConstruction=4;
		int campusId=-1;
		std::vector<int> types={founder,7};
		for(int type:{1,2,6})if(type!=founder)types.push_back(type);
		for(int member=0;member<4;++member)
		{
			DevelopmentIntent intent;intent.buildingType=types[member];
			intent.unmetCount=4;intent.priority=100;
			DevelopmentAction action;
			REQUIRE(planner.selectAction(world,{intent},limits,action));
			REQUIRE(action.type==BuildCampusMember);
			if(member)REQUIRE(action.campusId==campusId);
			REQUIRE(planner.reserve(world,action));campusId=action.campusId;
			REQUIRE(planner.revalidate(world,action,NULL,true));
			planner.markIssued(action.id,100+member,member);
			WorldBuilding building;building.id=100+member;
			building.buildingType=types[member];building.level=1;
			building.centerX=action.centerX;building.centerY=action.centerY;
			building.hp=building.hpMax=100;
			world.buildings.push_back(building);occupy(world,action);
			world.tick=member+1;planner.observe(world);
			// A partially filled mixed campus must retain both its free slots
			// and each occupant's upgrade contract across a save.
			auto backend=new GAGCore::MemoryStreamBackend;
			auto output=new GAGCore::BinaryOutputStream(backend);
			planner.save(output);backend->seekFromStart(0);
			auto inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
			GAGCore::BinaryInputStream input(inputBackend);
			Planner restored;restored.configure(world.profiles,1,2,6,5,7);
			REQUIRE(restored.load(&input,VERSION_MINOR));planner=restored;
		}
		REQUIRE(planner.campuses().size()==1);
		for(WorldBuilding& building:world.buildings)
		{
			DevelopmentAction upgrade;upgrade.type=UpgradeBuilding;
			upgrade.buildingId=building.id;upgrade.buildingType=building.buildingType;
			upgrade.centerX=building.centerX;upgrade.centerY=building.centerY;
			upgrade.fromLevel=1;upgrade.targetLevel=2;
			REQUIRE(planner.revalidate(world,upgrade));
			building.level=2;upgrade.fromLevel=2;upgrade.targetLevel=3;
			REQUIRE(planner.revalidate(world,upgrade)==(building.buildingType!=1));
		}
		// The founding member dying must not release surviving mixed members.
		world.buildings.erase(world.buildings.begin());planner.observe(world);
		REQUIRE(planner.campuses().size()==1);
		world.buildings.clear();planner.observe(world);
		REQUIRE((planner.campuses().empty()&&planner.reservations().empty()));
	}
}

static void woodReserveBlocksNewPlans()
{
	for(bool access:{false,true})
	{
		WorldState world=makeWorld();
		Planner planner;planner.configure(world.profiles,1,2,6,5,7);
		DevelopmentIntent intent;intent.buildingType=7;intent.unmetCount=1;intent.priority=100;
		DevelopmentLimits limits;limits.newConstruction=1;
		DevelopmentAction action;
		REQUIRE(planner.selectAction(world,{intent},limits,action));
		const auto& tiles=access?action.accessTiles:action.parcelTiles;
		REQUIRE(!tiles.empty());
		const int reserved=tiles.front();
		const uint32_t before=world.computeSignature();
		world.tiles[reserved].woodReserve=true;
		REQUIRE(world.computeSignature()!=before);
		REQUIRE(!planner.revalidate(world,action));
		DevelopmentAction alternative;
		REQUIRE(planner.selectAction(world,{intent},limits,alternative));
		for(const auto* selected:{&alternative.parcelTiles,&alternative.accessTiles,&alternative.arteryTiles})
			REQUIRE(std::find(selected->begin(),selected->end(),reserved)==selected->end());
	}
}

static void coordinateWrappingMatchesModulo()
{
	WorldState world;
	REQUIRE((world.normalizeX(INT_MIN)==0 && world.normalizeY(INT_MAX)==0));
	for(int dimension : {1,2,3,7,16,31,256,512,1024})
	{
		world.width=dimension;
		world.height=dimension;
		for(int coordinate=-3*dimension;coordinate<=3*dimension;++coordinate)
		{
			const int expected=(coordinate%dimension+dimension)%dimension;
			REQUIRE(world.normalizeX(coordinate)==expected);
			REQUIRE(world.normalizeY(coordinate)==expected);
		}
		for(int coordinate : {INT_MIN,INT_MIN+1,INT_MAX-1,INT_MAX})
		{
			const int expected=(static_cast<long long>(coordinate)%dimension+dimension)%dimension;
			REQUIRE(world.normalizeX(coordinate)==expected);
			REQUIRE(world.normalizeY(coordinate)==expected);
		}
	}
}
}

TEST_SUITE("Maxima.Placement")
{
	TEST_CASE("templates; parcels; appraisal and continuation")
	{
		coordinateWrappingMatchesModulo();
		woodReserveBlocksNewPlans();
		adjoiningBarracksRegression();
		mixedCampusRegression();
		placementReviewRegressions();
		upgradesUsuallyBeatNewConstruction();
		fortificationIsLastResort();
		foodLedgerPlacementRegression();
		relocationAppraisalRegression();
		placementContinuationRegression();
		Planner planner;planner.configure(makeProfiles(),1,2,6,5,7);
		std::string error;REQUIRE(planner.validateTemplates(&error));
		bool compact=false,expandable=false,large=false;
		for(size_t i=0;i<planner.templates().size();++i)
		{
			const DevelopmentTemplate& value=planner.templates()[i];
			if(value.id==InnCompact){compact=true;REQUIRE(value.slots.size()==4);REQUIRE(value.slots[0].maximumLevel==2);}
			if(value.id==InnExpandable){expandable=true;REQUIRE(value.slots[0].terminalFootprint.width==3);}
			if(value.buildingType==3){large=true;REQUIRE(value.slots[0].terminalFootprint.width==6);}
		}
		REQUIRE((compact&&expandable&&large));
		// When the strict water-distance tier has no visible parcel, select the
		// legal fallback parcel instead of treating the intent as blocked.
		{
			WorldState fallback;fallback.reset(16,16);fallback.profiles=makeProfiles();
			for(int y=0;y<16;++y)for(int x=0;x<16;++x)
			{
				fallback.tile(x,y).buildable=x!=0;
				fallback.tile(x,y).swimmable=x==0;fallback.tile(x,y).walkable=x!=0;fallback.tile(x,y).fertilitySource=x==0;
				fallback.tile(x,y).discovered=x==0;
			}
			for(int y=3;y<=8;++y)for(int x=4;x<=7;++x)
				fallback.tile(x,y).discovered=true;
			Planner fallbackPlanner;fallbackPlanner.configure(makeProfiles(),1,2,6,5,7);
			DevelopmentIntent small;small.buildingType=2;small.unmetCount=1;
			small.priority=80;small.workers=2;
			DevelopmentLimits quota;quota.newConstruction=2;
			DevelopmentAction action;
			REQUIRE(fallbackPlanner.selectAction(fallback,{small},quota,action));
			REQUIRE(action.fallbackWaterTier);
		}
		WorldState world=makeWorld();DevelopmentIntent intent;intent.buildingType=3;
		intent.unmetCount=2;intent.priority=80;intent.workers=2;
		// A full construction quota is not a spatial failure. Exercise both selector
		// entry points, then restore capacity without changing map occupancy.
		for(bool incremental:{false,true})
		{
			Planner retry;retry.configure(makeProfiles(),1,2,6,5,7);
			DevelopmentLimits available;available.newConstruction=1;
			DevelopmentAction selected;
			auto select=[&]()->bool
			{
				if(!incremental)return retry.selectAction(world,{intent},available,selected);
				SelectionProgress progress=SelectionPending;
				for(int step=0;step<5000&&progress==SelectionPending;++step)
					progress=retry.selectActionIncremental(world,{intent},available,
						selected,world.computeSignature(),1024);
				REQUIRE(progress!=SelectionPending);return progress==SelectionFound;
			};
			available.activeNewConstruction=1;REQUIRE(!select());
			available.activeNewConstruction=0;REQUIRE(select());
			REQUIRE(selected.utility.capabilityGain==10);
		}
		// Incremental selection snapshots the same inputs and must retain the exact
		// winner produced by the monolithic reference path.
		{
			Planner reference,sliced;
			reference.configure(makeProfiles(),1,2,6,5,7);
			sliced.configure(makeProfiles(),1,2,6,5,7);
			DevelopmentIntent alternative;alternative.buildingType=1;
			alternative.unmetCount=1;alternative.priority=70;alternative.workers=2;
			std::vector<DevelopmentIntent> intents;intents.push_back(intent);
			intents.push_back(alternative);
			DevelopmentLimits selectionLimits;selectionLimits.newConstruction=2;
			DevelopmentAction expected,actual;
			REQUIRE(reference.selectAction(world,intents,selectionLimits,expected));
			SelectionProgress progress=SelectionPending;
			for(int step=0;step<5000&&progress==SelectionPending;++step)
				progress=sliced.selectActionIncremental(world,intents,
					selectionLimits,actual,world.computeSignature(),1);
			REQUIRE(progress==SelectionFound);
			REQUIRE((actual.id==expected.id&&actual.type==expected.type));
			REQUIRE(actual.buildingType==expected.buildingType);
			REQUIRE((actual.centerX==expected.centerX&&actual.centerY==expected.centerY));
			REQUIRE(actual.utility.total==expected.utility.total);
			REQUIRE(actual.parcelTiles==expected.parcelTiles);
			REQUIRE(actual.accessTiles==expected.accessTiles);
			REQUIRE(actual.arteryTiles==expected.arteryTiles);
		}
		DevelopmentLimits limits;limits.newConstruction=2;DevelopmentAction first;
		REQUIRE(planner.selectAction(world,std::vector<DevelopmentIntent>(1,intent),limits,first));
		REQUIRE(!first.fallbackWaterTier);REQUIRE(planner.reserve(world,first));
		for(size_t i=0;i<planner.footprintReferences().size();++i)
			REQUIRE(!(planner.footprintReferences()[i]&&planner.circulationReferences()[i]));
		DevelopmentAction second;
		REQUIRE(planner.selectAction(world,std::vector<DevelopmentIntent>(1,intent),limits,second));
		for(size_t i=0;i<second.parcelTiles.size();++i)
			REQUIRE(!planner.isFootprintReserved(second.parcelTiles[i]));

		// Live counts and not-yet-observed build actions form one commitment count.
		// This is what prevents a one-building deficit from consuming every free site.
		WorldState commitmentWorld=makeWorld();Planner commitments;
		commitments.configure(makeProfiles(),1,2,6,5,7);
		commitmentWorld.swimmingBuilders=2;
		WorldBuilding existing;existing.id=50;existing.buildingType=3;existing.level=1;
		existing.centerX=4;existing.centerY=4;existing.hp=existing.hpMax=100;
		commitmentWorld.buildings.push_back(existing);
		REQUIRE(commitments.committedBuildingCount(commitmentWorld,3)==1);
		DevelopmentIntent oneBuilding;oneBuilding.buildingType=3;
		oneBuilding.unmetCount=1;oneBuilding.priority=80;oneBuilding.workers=2;
		DevelopmentLimits fourSites;fourSites.newConstruction=4;
		DevelopmentAction committed;
		REQUIRE(commitments.selectAction(commitmentWorld,
			std::vector<DevelopmentIntent>(1,oneBuilding),fourSites,committed));
		REQUIRE(commitments.reserve(commitmentWorld,committed));
		commitments.markIssued(committed.id,100,commitmentWorld.tick);
		REQUIRE(commitments.committedBuildingCount(commitmentWorld,3)==2);
		// A reduced upgrade quota must not consume the independent new-building
		// allowance. This kept large saved colonies from adding training capacity.
		{
			Planner planner;planner.configure(makeProfiles(),1,2,6,5,7);
			DevelopmentLimits limits;limits.newConstruction=1;
			limits.activeLevel1Upgrades=4;limits.activeLevel2Upgrades=3;
			limits.allowUpgrades=false;limits.allowLevel2Upgrades=false;
			DevelopmentAction candidate;
			REQUIRE(planner.selectAction(commitmentWorld,{oneBuilding},limits,candidate));
			REQUIRE(limits.totalCapacity()==8);
			REQUIRE(planner.revalidateSelection(commitmentWorld,{oneBuilding},limits,candidate));
			limits.activeNewConstruction=1;
			REQUIRE(!planner.revalidateSelection(commitmentWorld,{oneBuilding},limits,candidate));
		}

		// Repair generation has an independent master switch and must not fall
		// through into an invalid upgrade candidate when repairs are disabled.
		WorldState repairWorld=makeWorld();
		WorldBuilding damaged;damaged.id=60;damaged.buildingType=2;damaged.level=1;
		damaged.centerX=10;damaged.centerY=10;damaged.hp=50;damaged.hpMax=100;
		repairWorld.buildings.push_back(damaged);
		Planner repairPlanner;repairPlanner.configure(makeProfiles(),1,2,6,5,7);
		DevelopmentLimits repairLimits;repairLimits.newConstruction=0;
		repairLimits.allowRepairs=false;repairLimits.allowUpgrades=false;
		DevelopmentAction repairAction;
		REQUIRE(!repairPlanner.selectAction(repairWorld,
			std::vector<DevelopmentIntent>(),repairLimits,repairAction));
		repairLimits.allowRepairs=true;
		REQUIRE(repairPlanner.selectAction(repairWorld,
			std::vector<DevelopmentIntent>(),repairLimits,repairAction));
		REQUIRE(repairAction.type==RepairBuilding);
		// Final authorization applies to repairs and upgrades as well as builds.
		repairLimits.newConstruction=1;
		RejectionReason currentReason=RejectedTerrain;
		REQUIRE(repairPlanner.revalidateSelection(repairWorld,
			std::vector<DevelopmentIntent>(),repairLimits,repairAction,&currentReason));
		repairLimits.allowRepairs=false;
		REQUIRE(!repairPlanner.revalidateSelection(repairWorld,
			std::vector<DevelopmentIntent>(),repairLimits,repairAction,&currentReason));
		REQUIRE(currentReason==RejectedAuthorization);
		repairLimits.allowRepairs=true;
		WorldState healthyWorld=repairWorld;healthyWorld.buildings[0].hp=100;
		REQUIRE(!repairPlanner.revalidateSelection(healthyWorld,
			std::vector<DevelopmentIntent>(),repairLimits,repairAction,&currentReason));
		REQUIRE(currentReason==RejectedUpgradeContract);
		repairPlanner.adoptStartingBuildings(healthyWorld);
		DevelopmentAction upgradeAction=repairAction;
		upgradeAction.type=UpgradeBuilding;upgradeAction.targetLevel=2;
		repairLimits.allowUpgrades=true;repairLimits.level1Upgrades=1;
		REQUIRE(repairPlanner.revalidateSelection(healthyWorld,
			std::vector<DevelopmentIntent>(),repairLimits,upgradeAction,&currentReason));
		repairLimits.allowUpgrades=false;
		REQUIRE(!repairPlanner.revalidateSelection(healthyWorld,
			std::vector<DevelopmentIntent>(),repairLimits,upgradeAction,&currentReason));
		REQUIRE(currentReason==RejectedAuthorization);
		repairLimits.allowUpgrades=true;repairLimits.activeLevel1Upgrades=1;
		REQUIRE(!repairPlanner.revalidateSelection(healthyWorld,
			std::vector<DevelopmentIntent>(),repairLimits,upgradeAction,&currentReason));
		REQUIRE(currentReason==RejectedAuthorization);

		// Director priorities change only demand before the 25% upgrade preference,
		// preserving every spatial component.
		// Zero is a veto both during selection and if authority changes before issue.
		{
			Planner upgrades;upgrades.configure(makeProfiles(),1,2,6,5,7);
			upgrades.adoptStartingBuildings(healthyWorld);
			DevelopmentLimits priorityLimits;priorityLimits.allowUpgrades=true;
			priorityLimits.level1Upgrades=1;
			const std::vector<DevelopmentIntent> noIntents;
			DevelopmentAction generic,weighted;
			REQUIRE(upgrades.selectAction(healthyWorld,noIntents,priorityLimits,generic));
			REQUIRE((generic.type==UpgradeBuilding && generic.buildingType==2));
			priorityLimits.upgradePriorities[std::make_pair(2,1)]=80;
			REQUIRE(upgrades.selectAction(healthyWorld,noIntents,priorityLimits,weighted));
			REQUIRE(weighted.buildingId==generic.buildingId);
			REQUIRE(weighted.utility.unmetDemand==80);
			REQUIRE(weighted.utility.total-generic.utility.total
				==(80-generic.utility.unmetDemand)*upgrades.policy().unmetDemandWeight*5/4);
			priorityLimits.upgradePriorities[std::make_pair(2,1)]=0;
			REQUIRE(!upgrades.revalidateSelection(healthyWorld,noIntents,
				priorityLimits,weighted,&currentReason));
			REQUIRE(currentReason==RejectedAuthorization);
			REQUIRE(!upgrades.selectAction(healthyWorld,noIntents,priorityLimits,weighted));
			// The upgrade veto must leave repair authorization independent.
			REQUIRE(upgrades.selectAction(repairWorld,noIntents,priorityLimits,weighted));
			REQUIRE(weighted.type==RepairBuilding);

			WorldState choices=healthyWorld;
			WorldBuilding school=choices.buildings.front();school.id=61;
			school.buildingType=6;school.centerX=22;school.centerY=22;
			choices.buildings.push_back(school);
			upgrades.adoptStartingBuildings(choices);
			priorityLimits.upgradePriorities[std::make_pair(6,1)]=80;
			SelectionProgress progress=SelectionPending;
			for(int step=0;step<5000 && progress==SelectionPending;++step)
				progress=upgrades.selectActionIncremental(choices,noIntents,
					priorityLimits,weighted,choices.computeSignature(),1);
			REQUIRE((progress==SelectionFound && weighted.buildingType==6));
			priorityLimits.upgradePriorities[std::make_pair(6,1)]=0;
			priorityLimits.upgradePriorities[std::make_pair(2,1)]=90;
			REQUIRE(upgrades.selectAction(choices,noIntents,priorityLimits,weighted));
			REQUIRE((weighted.buildingType==2 && weighted.utility.unmetDemand==90));
		}

		// Director priorities choose between otherwise identical upgrade roles and
		// remain part of incremental snapshots and final live authorization.
		{
			WorldState upgradeWorld=makeWorld();
			for(int type=3;type<=4;++type)
			{
				WorldBuilding b;b.id=type;b.buildingType=type;b.level=1;
				b.centerX=type==3?8:24;b.centerY=16;b.hp=b.hpMax=100;
				upgradeWorld.buildings.push_back(b);
			}
			Planner upgrades;upgrades.configure(makeProfiles(),1,2,6,5,7);
			upgrades.adoptStartingBuildings(upgradeWorld);
			DevelopmentLimits limits;limits.allowUpgrades=true;limits.level1Upgrades=1;
			limits.upgradePriorities[std::make_pair(3,1)]=1;
			limits.upgradePriorities[std::make_pair(4,1)]=80;
			DevelopmentAction winner;
			REQUIRE(upgrades.selectAction(upgradeWorld,{},limits,winner));
			REQUIRE((winner.buildingType==4 && winner.utility.unmetDemand==80));
			limits.upgradePriorities[std::make_pair(3,1)]=80;
			limits.upgradePriorities[std::make_pair(4,1)]=1;
			REQUIRE(upgrades.selectAction(upgradeWorld,{},limits,winner));
			REQUIRE((winner.buildingType==3 && winner.utility.unmetDemand==80));
			REQUIRE(upgrades.selectActionIncremental(upgradeWorld,{},limits,winner,
				upgradeWorld.computeSignature(),1)==SelectionPending);
			limits.upgradePriorities[std::make_pair(3,1)]=0;
			limits.upgradePriorities[std::make_pair(4,1)]=0;
			SelectionProgress progress=SelectionPending;
			for(int i=0;i<10 && progress==SelectionPending;++i)
				progress=upgrades.selectActionIncremental(upgradeWorld,{},limits,winner,
					upgradeWorld.computeSignature(),1);
			REQUIRE((progress==SelectionFound && winner.buildingType==3));
			RejectionReason reason=RejectedTerrain;
			REQUIRE(!upgrades.revalidateSelection(upgradeWorld,{},limits,winner,&reason));
			REQUIRE(reason==RejectedAuthorization);
			REQUIRE(!upgrades.selectAction(upgradeWorld,{},limits,winner));
			// A priority veto suppresses upgrades, while damaged buildings can repair.
			upgradeWorld.buildings[0].hp=50;
			REQUIRE(upgrades.selectAction(upgradeWorld,{},limits,winner));
			REQUIRE(winner.type==RepairBuilding);
		}

		// Colony seeds share the Swarm type but carry independent placement state.
		WorldState colonyWorld;colonyWorld.reset(64,64);colonyWorld.profiles=makeProfiles();
		for(int y=0;y<64;++y)for(int x=0;x<64;++x)
		{colonyWorld.tile(x,y).discovered=true;colonyWorld.tile(x,y).buildable=true;}
		WorldBuilding home;home.id=1;home.buildingType=0;home.level=1;
		home.centerX=8;home.centerY=8;home.hp=home.hpMax=100;
		colonyWorld.buildings.push_back(home);
		colonyWorld.tile(40,40).resourceType=1; colonyWorld.tile(40,40).foodOpportunity=64*65536;
		colonyWorld.tile(40,40).resourceAmount=10;
		colonyWorld.tile(40,40).permanentResource=true;
		DevelopmentIntent colony;colony.buildingType=0;colony.purpose=ColonySeed;
		colony.unmetCount=1;colony.priority=100;colony.workers=2;
		colony.requiredResourceType=1;
		Planner colonizer;colonizer.configure(makeProfiles(),1,2,6,5,7);
		colonizer.adoptStartingBuildings(colonyWorld);DevelopmentAction seed;
		REQUIRE(colonizer.selectAction(colonyWorld,
			std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
		REQUIRE(seed.purpose==ColonySeed);
		REQUIRE((seed.utility.friendlyDistance>=24&&seed.utility.frontierGain>=8));
		// Being far away is insufficient when an existing or planned inn already
		// claims this food. Test live revalidation, including pending reservations.
		{
			WorldState claimed=colonyWorld;
			WorldBuilding inn=home;inn.id=2;inn.buildingType=1;
			inn.centerX=43;inn.centerY=40;inn.site=true;
			claimed.buildings.push_back(inn);
			RejectionReason reason;
			REQUIRE(!colonizer.revalidateSelection(claimed,{colony},fourSites,seed,&reason));
			REQUIRE((reason==RejectedColonyDistance||reason==RejectedColonyCorn));
			REQUIRE(!colonizer.revalidate(claimed,seed,&reason,true));
			WorldState tiny=colonyWorld;tiny.tile(40,40).foodOpportunity=65536;
			REQUIRE(!colonizer.revalidateSelection(tiny,{colony},fourSites,seed,&reason));
			REQUIRE(reason==RejectedColonyCorn);
			WorldState blocked=colonyWorld;blocked.tile(40,40).foodTraversable=false;
			REQUIRE(!colonizer.revalidateSelection(blocked,{colony},fourSites,seed,&reason));
			REQUIRE(reason==RejectedColonyCorn);
			REQUIRE(colonizer.revalidateSelection(colonyWorld,{colony},fourSites,seed,&reason));
		}

		REQUIRE(colonizer.reserve(colonyWorld,seed));
		REQUIRE(colonizer.activeBuildCount(0,ColonySeed)==1);
		{
			RejectionReason reason;
			REQUIRE(colonizer.revalidateSelection(colonyWorld,{colony},fourSites,seed,&reason));
			DevelopmentAction duplicate=seed;duplicate.id=-1;
			REQUIRE(!colonizer.revalidateSelection(colonyWorld,{colony},fourSites,duplicate,&reason));
			REQUIRE((reason==RejectedColonyDistance||reason==RejectedColonyCorn));
		}

		REQUIRE(colonizer.activeBuildCount(0,CoreCapacity)==0);

		Planner independentlyBlocked;
		independentlyBlocked.configure(makeProfiles(),1,2,6,5,7);
		independentlyBlocked.adoptStartingBuildings(colonyWorld);
		independentlyBlocked.mutablePolicy().colonyMinimumAnchorDistance=65;
		REQUIRE(!independentlyBlocked.selectAction(colonyWorld,
			std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
		REQUIRE(independentlyBlocked.blockedSignature(0,ColonySeed)!=0);
		REQUIRE(independentlyBlocked.blockedSignature(0,CoreCapacity)==0);
		DevelopmentIntent coreSwarm=colony;coreSwarm.purpose=CoreCapacity;
		coreSwarm.requiredResourceType=-1;
		REQUIRE(independentlyBlocked.selectAction(colonyWorld,
			std::vector<DevelopmentIntent>(1,coreSwarm),fourSites,seed));
		WorldState islands=colonyWorld;
		for(size_t tile=0;tile<islands.tiles.size();++tile)
		{islands.tiles[tile].buildable=false;islands.tiles[tile].swimmable=true;islands.tiles[tile].walkable=false;islands.tiles[tile].fertilitySource=true;
		 islands.tiles[tile].resourceType=-1;islands.tiles[tile].foodOpportunity=0;islands.tiles[tile].permanentResource=false;}
		for(int y=0;y<=20;++y)for(int x=0;x<=20;++x)
		{islands.tile(x,y).buildable=true;islands.tile(x,y).swimmable=false;islands.tile(x,y).walkable=true;islands.tile(x,y).fertilitySource=false;}
		for(int y=32;y<=60;++y)for(int x=32;x<=60;++x)
		{islands.tile(x,y).buildable=true;islands.tile(x,y).swimmable=false;islands.tile(x,y).walkable=true;islands.tile(x,y).fertilitySource=false;}
		islands.tile(45,45).resourceType=1; islands.tile(45,45).foodOpportunity=64*65536;
		islands.tile(45,45).resourceAmount=10;
		islands.tile(45,45).permanentResource=true;
		Planner dryColonizer;dryColonizer.configure(makeProfiles(),1,2,6,5,7);
		dryColonizer.adoptStartingBuildings(islands);
		REQUIRE(!dryColonizer.selectAction(islands,
			std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
		REQUIRE(dryColonizer.selectionSummary().rejected[RejectedIslandBuilders]>0);
		// Walkers can cross sand bridges. Keep building parcels on grass, but
		// do not mistake a dry connection between colonies for a swimming route.
		{
			WorldState bridged=islands;
			for(int y=10;y<=35;++y)for(int x=10;x<=35;++x)
				if((x<=13||y>=32)&&bridged.tile(x,y).swimmable)
				{bridged.tile(x,y).swimmable=false;bridged.tile(x,y).walkable=true;bridged.tile(x,y).fertilitySource=false;bridged.tile(x,y).buildable=false;}
			Planner bridgeColonizer;bridgeColonizer.configure(makeProfiles(),1,2,6,5,7);
			bridgeColonizer.adoptStartingBuildings(bridged);
			DevelopmentAction bridgeSeed;
			REQUIRE(bridgeColonizer.selectAction(bridged,{colony},fourSites,bridgeSeed));
			REQUIRE((!bridgeSeed.requiresSwimmingBuilders&&!bridgeSeed.arteryTiles.empty()));
			int sandTile=-1;
			for(int tile:bridgeSeed.arteryTiles)
				if(!bridged.tiles[tile].buildable)sandTile=tile;
			REQUIRE(sandTile>=0);
			REQUIRE(bridgeColonizer.reserve(bridged,bridgeSeed));
			RejectionReason reason;
			REQUIRE(bridgeColonizer.revalidate(bridged,bridgeSeed,&reason,true));
			bridged.tiles[sandTile].swimmable=true;bridged.tiles[sandTile].walkable=false;bridged.tiles[sandTile].fertilitySource=true;
			REQUIRE(!bridgeColonizer.revalidate(bridged,bridgeSeed,&reason,true));
			REQUIRE(reason==RejectedCirculation);
		}
		islands.swimmingBuilders=2;
		// Reuse the failed planner: training swimmers does not alter occupancy.
		REQUIRE(dryColonizer.selectAction(islands,
			std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
		Planner swimmingColonizer;
		swimmingColonizer.configure(makeProfiles(),1,2,6,5,7);
		swimmingColonizer.adoptStartingBuildings(islands);
		REQUIRE(swimmingColonizer.selectAction(islands,
			std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
		REQUIRE((seed.requiresSwimmingBuilders&&seed.arteryTiles.empty()));
		// A valid selection remains valid in an unchanged world, but current
		// authority and construction dependencies must win over its snapshot.
		{
			RejectionReason reason=RejectedTerrain;
			std::vector<DevelopmentIntent> intents(1,colony);
			REQUIRE(swimmingColonizer.revalidateSelection(islands,intents,fourSites,seed,&reason));
			WorldState changed=islands;changed.swimmingBuilders=1;
			REQUIRE(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
			REQUIRE(reason==RejectedIslandBuilders);
			REQUIRE(!swimmingColonizer.revalidateSelection(islands,
				std::vector<DevelopmentIntent>(),fourSites,seed,&reason));
			REQUIRE(reason==RejectedAuthorization);
			DevelopmentLimits stopped=fourSites;stopped.newConstruction=0;
			REQUIRE(!swimmingColonizer.revalidateSelection(islands,intents,stopped,seed,&reason));
			REQUIRE(reason==RejectedAuthorization);
			changed=islands;changed.tiles[seed.parcelTiles.front()].threat=100;
			REQUIRE(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
			REQUIRE(reason==RejectedColonyThreat);
			changed=islands;changed.tile(45,45).resourceType=-1;
			REQUIRE(swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
			changed.tile(45,45).foodOpportunity=0;
			REQUIRE(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
			REQUIRE(reason==RejectedRequiredSource);
			changed=islands;changed.tiles[seed.accessTiles.front()].occupied=true;
			REQUIRE(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
			REQUIRE(reason==RejectedAccess);
			// Relocated corn is still present globally but no longer supports this
			// colony. This catches reuse of distance caches from the old snapshot.
			changed=islands;changed.tile(45,45).resourceType=-1;changed.tile(45,45).foodOpportunity=0;
			changed.tile(8,8).resourceType=1;changed.tile(8,8).foodOpportunity=65536;
			REQUIRE(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
			REQUIRE(reason==RejectedColonyCorn);
			REQUIRE(swimmingColonizer.revalidateSelection(islands,intents,fourSites,seed,&reason));
		}

		{
			GAGCore::MemoryStreamBackend* backend=new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream* serialized=
				new GAGCore::BinaryOutputStream(backend);
			colonizer.save(serialized);backend->seekFromStart(0);
			GAGCore::MemoryStreamBackend* copy=
				new GAGCore::MemoryStreamBackend(*backend);
			delete serialized;
			GAGCore::BinaryInputStream* serializedInput=
				new GAGCore::BinaryInputStream(copy);
			Planner colonyRestored;
			colonyRestored.configure(makeProfiles(),1,2,6,5,7);
			REQUIRE(colonyRestored.load(serializedInput,VERSION_MINOR));
			delete serializedInput;
			REQUIRE(colonyRestored.activeBuildCount(0,ColonySeed)==1);
			REQUIRE(colonyRestored.actions().begin()->second.purpose==ColonySeed);
		}
		WorldBuilding observed;observed.id=100;observed.buildingType=3;observed.level=1;
		observed.centerX=committed.centerX;observed.centerY=committed.centerY;
		observed.hp=observed.hpMax=100;observed.site=true;
		commitmentWorld.buildings.push_back(observed);
		REQUIRE(commitments.committedBuildingCount(commitmentWorld,3)==2);

		// A complete compact campus needs no internal aisle: its permanent outer
		// ring protects circulation while all four slots can be filled.
		WorldState campusWorld=makeWorld();Planner campus;
		campus.configure(makeProfiles(),1,2,6,5,7);DevelopmentIntent hospital;
		hospital.buildingType=2;hospital.unmetCount=4;hospital.priority=80;hospital.workers=2;
		DevelopmentLimits campusLimits;campusLimits.newConstruction=4;
		for(int member=0;member<4;++member)
		{
			DevelopmentAction action;
			REQUIRE(campus.selectAction(campusWorld,
				std::vector<DevelopmentIntent>(1,hospital),campusLimits,action));
			REQUIRE(action.templateId==HospitalCompact);
			REQUIRE(campus.reserve(campusWorld,action));
			RejectionReason issueReason=RejectedReservation;
			REQUIRE(campus.revalidate(campusWorld,action,&issueReason,true));
			campus.markIssued(action.id,100+member,member);
			WorldBuilding building;building.id=100+member;building.buildingType=2;
			building.level=1;building.centerX=action.centerX;building.centerY=action.centerY;
			building.hp=building.hpMax=100;campusWorld.buildings.push_back(building);
			occupy(campusWorld,action);campusWorld.tick=member+1;campus.observe(campusWorld);
		}
		REQUIRE(campus.campuses().size()==1);
		for(size_t slot=0;slot<campus.campuses()[0].slots.size();++slot)
			REQUIRE(campus.campuses()[0].slots[slot].buildingId>=0);
		for(size_t i=0;i<campus.footprintReferences().size();++i)
			REQUIRE(!(campus.footprintReferences()[i]&&campus.circulationReferences()[i]));

		// Version-92 planner state is exact: durable contracts and the next stable
		// decision must survive a binary save/load round trip without reconstruction.
		GAGCore::MemoryStreamBackend* outputBackend=new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream* output=new GAGCore::BinaryOutputStream(outputBackend);
		campus.save(output);outputBackend->seekFromStart(0);
		GAGCore::MemoryStreamBackend* inputBackend=
			new GAGCore::MemoryStreamBackend(*outputBackend);
		delete output;
		GAGCore::BinaryInputStream* input=new GAGCore::BinaryInputStream(inputBackend);
		Planner restored;restored.configure(makeProfiles(),1,2,6,5,7);
		REQUIRE(restored.load(input,VERSION_MINOR));delete input;
		REQUIRE(restored.campuses().size()==campus.campuses().size());
		REQUIRE(restored.reservations().size()==campus.reservations().size());
		REQUIRE(restored.actions().size()==campus.actions().size());
		REQUIRE(restored.footprintReferences()==campus.footprintReferences());
		REQUIRE(restored.circulationReferences()==campus.circulationReferences());
		DevelopmentIntent afterLoad;afterLoad.buildingType=3;afterLoad.unmetCount=1;
		afterLoad.priority=80;afterLoad.workers=2;DevelopmentAction originalNext,restoredNext;
		REQUIRE(campus.selectAction(campusWorld,std::vector<DevelopmentIntent>(1,afterLoad),
			campusLimits,originalNext));
		REQUIRE(restored.selectAction(campusWorld,std::vector<DevelopmentIntent>(1,afterLoad),
			campusLimits,restoredNext));
		REQUIRE(originalNext.type==restoredNext.type);
		REQUIRE(originalNext.templateId==restoredNext.templateId);
		REQUIRE(originalNext.centerX==restoredNext.centerX);
		REQUIRE(originalNext.centerY==restoredNext.centerY);
		REQUIRE(originalNext.utility.total==restoredNext.utility.total);

		// A terminal footprint adopted for a starting building must not overlap the
		// access ring, including an inn whose level-three footprint expands.
		WorldState adoptedWorld=makeWorld();WorldBuilding starting;
		starting.id=9;starting.buildingType=1;starting.level=1;starting.centerX=12;
		starting.centerY=12;starting.hp=starting.hpMax=100;adoptedWorld.buildings.push_back(starting);
		DevelopmentAction startingFootprint;startingFootprint.centerX=12;startingFootprint.centerY=12;
		startingFootprint.initialFootprint=makeProfiles()[1].atLevel(1)->footprint;
		occupy(adoptedWorld,startingFootprint);Planner adopted;
		adopted.configure(makeProfiles(),1,2,6,5,7);adopted.adoptStartingBuildings(adoptedWorld);
		REQUIRE(adopted.standaloneContracts().size()==1);
		REQUIRE(adopted.standaloneContracts()[0].maximumLevel==3);
		for(size_t i=0;i<adopted.footprintReferences().size();++i)
			REQUIRE(!(adopted.footprintReferences()[i]&&adopted.circulationReferences()[i]));

		// Disabling arteries must not label a separated, land-connected parcel an island.
		{
			Planner unrouted;unrouted.configure(makeProfiles(),1,2,6,5,7);
			unrouted.adoptStartingBuildings(adoptedWorld);
			unrouted.mutablePolicy().arteryRoutingEnabled=false;
			unrouted.mutablePolicy().spacingWeight=100;
			DevelopmentAction action;
			REQUIRE(unrouted.selectAction(adoptedWorld,{intent},limits,action));
			REQUIRE(action.utility.friendlyDistance>=2);
			REQUIRE(!action.requiresSwimmingBuilders);
			REQUIRE(unrouted.revalidateSelection(adoptedWorld,{intent},limits,action));
		}
		// Spacing changes only the choice of a new parcel. It does not alter campus
		// templates or the placement of later members inside an existing campus.
		Planner spaced;spaced.configure(makeProfiles(),1,2,6,5,7);
		spaced.adoptStartingBuildings(adoptedWorld);
		spaced.mutablePolicy().spacingTargetTiles=2;
		spaced.mutablePolicy().spacingWeight=100;
		DevelopmentIntent spacedIntent;spacedIntent.buildingType=6;
		spacedIntent.unmetCount=1;spacedIntent.priority=80;spacedIntent.workers=2;
		DevelopmentAction spacedAction;
		REQUIRE(spaced.selectAction(adoptedWorld,
			std::vector<DevelopmentIntent>(1,spacedIntent),limits,spacedAction));
		REQUIRE(spacedAction.utility.friendlyDistance>=2);

		// A reservation may preserve resources in future upgrade/campus space, but
		// the building's immediate engine footprint must already be empty. Placement
		// no longer turns the larger reservation into destructive clearing work.
		WorldState resourceWorld=makeWorld();
		for(size_t i=0;i<resourceWorld.tiles.size();++i)
		{
			if(!resourceWorld.tiles[i].buildable)continue;
			resourceWorld.tiles[i].clearableResource=true;
			resourceWorld.tiles[i].resourceType=0;
			resourceWorld.tiles[i].resourceAmount=5;
		}
		for(int y=10;y<=13;++y)for(int x=10;x<=13;++x)
		{
			resourceWorld.tile(x,y).clearableResource=false;
			resourceWorld.tile(x,y).resourceType=-1;
			resourceWorld.tile(x,y).resourceAmount=0;
		}
		Planner preserving;preserving.configure(makeProfiles(),1,2,6,5,7);
		DevelopmentIntent largeIntent;largeIntent.buildingType=3;
		largeIntent.unmetCount=1;largeIntent.priority=80;largeIntent.workers=2;
		DevelopmentAction preservingAction;
		REQUIRE(preserving.selectAction(resourceWorld,
			std::vector<DevelopmentIntent>(1,largeIntent),limits,preservingAction));
		bool preservedResource=false;
		for(size_t i=0;i<preservingAction.parcelTiles.size();++i)
			preservedResource=preservedResource
				||resourceWorld.tiles[preservingAction.parcelTiles[i]].clearableResource;
		REQUIRE(preservedResource);
		REQUIRE(preserving.reserve(resourceWorld,preservingAction));
		RejectionReason preservingReason=RejectedReservation;
		REQUIRE(preserving.revalidate(resourceWorld,preservingAction,
			&preservingReason,true));

		// New inner-settlement parcels leave valuable food zones to inns and swarms.
		// The halo penalty does not apply to food infrastructure itself.
		WorldState foodWorld=makeWorld();Planner unpenalized;
		unpenalized.configure(makeProfiles(),1,2,6,5,7);
		unpenalized.mutablePolicy().foodZonePenaltyWeight=0;
		DevelopmentIntent schoolIntent;schoolIntent.buildingType=6;
		schoolIntent.unmetCount=1;schoolIntent.priority=80;schoolIntent.workers=2;
		DevelopmentAction baselineSchool;
		REQUIRE(unpenalized.selectAction(foodWorld,
			std::vector<DevelopmentIntent>(1,schoolIntent),limits,baselineSchool));
		foodWorld.tile(baselineSchool.centerX,baselineSchool.centerY)
			.foodOpportunity=1000;
		Planner foodAware;foodAware.configure(makeProfiles(),1,2,6,5,7);
		DevelopmentAction protectedSchool;
		REQUIRE(foodAware.selectAction(foodWorld,
			std::vector<DevelopmentIntent>(1,schoolIntent),limits,protectedSchool));
		REQUIRE((protectedSchool.centerX!=baselineSchool.centerX
			||protectedSchool.centerY!=baselineSchool.centerY));
		REQUIRE(protectedSchool.utility.foodZonePressure==0);
		DevelopmentIntent innIntent;innIntent.buildingType=1;
		innIntent.unmetCount=1;innIntent.priority=80;innIntent.workers=2;
		DevelopmentAction foodInn;
		REQUIRE(foodAware.selectAction(foodWorld,
			std::vector<DevelopmentIntent>(1,innIntent),limits,foodInn));
		REQUIRE(foodInn.utility.foodZonePressure==0);

		// Missing required sources and negative utility become stable waiting
		// decisions. An unchanged signature does not scan the map again.
		Planner blocked;blocked.configure(makeProfiles(),1,2,6,5,7);
		DevelopmentIntent required;required.buildingType=1;required.unmetCount=1;
		required.priority=80;required.requiredResourceType=1;DevelopmentAction none;
		REQUIRE(!blocked.selectAction(makeWorld(),std::vector<DevelopmentIntent>(1,required),limits,none));
		REQUIRE(blocked.selectionSummary().rejected[RejectedRequiredSource]==1);
		REQUIRE(!blocked.actions().empty());
		REQUIRE(blocked.actions().begin()->second.state==RequiredSourceMissing);
		Planner negative;negative.configure(makeProfiles(),1,2,6,5,7);
		negative.mutablePolicy().unmetDemandWeight=0;
		negative.mutablePolicy().serviceGainWeight=0;
		negative.mutablePolicy().capabilityGainWeight=0;
		negative.mutablePolicy().parallelismGainWeight=0;
		negative.mutablePolicy().redundancyGainWeight=0;
		negative.mutablePolicy().roleLocationQualityWeight=0;
		negative.mutablePolicy().defendednessWeight=0;
		negative.mutablePolicy().compactnessWeight=0;
		negative.mutablePolicy().spacingWeight=0;
		negative.mutablePolicy().newlyReservedLandWeight=100;
		WorldState unchanged=makeWorld();DevelopmentIntent ordinary;
		ordinary.buildingType=2;ordinary.unmetCount=1;ordinary.priority=80;
		REQUIRE(!negative.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,none));
		REQUIRE(negative.selectionSummary().rejected[RejectedNegativeUtility]==1);
		REQUIRE(!negative.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,none));
		REQUIRE(negative.selectionSummary().candidateCount==0);

		// Negative utility is only stable while its scoring inputs and demand match.
		negative.mutablePolicy().unmetDemandWeight=20;
		ordinary.priority=0;
		REQUIRE(!negative.selectAction(unchanged,{ordinary},limits,none));
		ordinary.priority=100;
		REQUIRE(negative.selectAction(unchanged,{ordinary},limits,none));

		// A create timeout is distinct and quarantines the failed coordinate while
		// the world remains otherwise unchanged.
		Planner timeout;timeout.configure(makeProfiles(),1,2,6,5,7);DevelopmentAction timed;
		REQUIRE(timeout.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,timed));
		REQUIRE(timeout.reserve(unchanged,timed));timeout.markIssued(timed.id,77,0);
		unchanged.tick=301;timeout.observe(unchanged);
		REQUIRE(timeout.actions().find(timed.id)->second.state==CreateTimedOut);
		DevelopmentAction replanned;
		REQUIRE(timeout.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,replanned));
		REQUIRE((replanned.centerX!=timed.centerX||replanned.centerY!=timed.centerY));
		// Discovery/resource churn elsewhere must not reactivate a locally failed
		// engine coordinate.
		unchanged.tile(20,20).resourceType=1;unchanged.tile(20,20).resourceAmount=7;
		DevelopmentAction afterRemoteChange;
		REQUIRE(timeout.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),
			limits,afterRemoteChange));
		REQUIRE((afterRemoteChange.centerX!=timed.centerX
			||afterRemoteChange.centerY!=timed.centerY));

		std::cout<<"templates="<<planner.templates().size()
			<<" first="<<first.centerX<<","<<first.centerY
			<<" second="<<second.centerX<<","<<second.centerY
			<<" campus_members="<<campus.campuses()[0].slots.size()<<"\n";
	}
}

TEST_CASE("Explicit noncanonical neighborhood tables survive compact saves" * doctest::test_suite("Maxima.Placement"))
{
    class ExplicitTable : public GAGCore::BinaryOutputStream
    {
    public:
        using BinaryOutputStream::BinaryOutputStream;
        void writeUint32(const Uint32 value,const std::string name) override
        {
            BinaryOutputStream::writeUint32(name=="neighborhoodEncoding"?2:value,name);
            if(name=="neighborhoodEncoding")
            {
                const std::vector<int> table={INT_MIN,-1,77,INT_MAX};
                AIMaximaContinuation::Writer archive(this,true);
                archive("scoringNeighborhoodCache",table);
            }
        }
    };
    auto* memory=new GAGCore::MemoryStreamBackend;
    ExplicitTable out(memory);
    AIMaximaPlacement::Planner original;
    original.saveExecutionState(&out);
    const auto bytes=memory->takeContents();
    GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(bytes.data(),bytes.size()));
    input.seekFromStart(0);
    GAGCore::BinaryInputStream::CheckedReads checked(&input);
    AIMaximaPlacement::Planner restored;
    restored.loadExecutionState(&input,VERSION_MINOR);
    auto* result=new GAGCore::MemoryStreamBackend;
    GAGCore::BinaryOutputStream writer(result);
    restored.saveExecutionState(&writer);
    CHECK(result->takeContents()==bytes);
}
