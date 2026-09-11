#include "../src/AIMaximaPlacement.h"
#include "../src/Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>

#include <cassert>
#include <iostream>

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
	{world.tile(x,y).discovered=true;world.tile(x,y).grass=true;world.tile(x,y).protectedness=60;}
	for(int x=0;x<world.width;++x){world.tile(x,0).water=true;world.tile(x,0).grass=false;}
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
		assert(planner.standaloneContracts().at(0).maximumLevel==2);
		DevelopmentLimits limits;limits.allowUpgrades=true;limits.level1Upgrades=1;
		DevelopmentAction upgrade;assert(planner.selectAction(world,{},limits,upgrade));
		assert(upgrade.targetLevel==2);
		const int reservation=planner.standaloneContracts()[0].reservationId;
		const uint32_t revision=planner.spatialRevision();
		planner.adoptStartingBuildings(world);assert(planner.spatialRevision()==revision);
		world.tile(11,12).resourceType=-1;world.tile(11,12).clearableResource=false;
		planner.adoptStartingBuildings(world);
		assert(planner.standaloneContracts().at(0).maximumLevel==3);
		assert(planner.standaloneContracts().at(0).reservationId==reservation);
		for(size_t index=0;index<world.tiles.size();++index)
			assert(!(planner.isFootprintReserved(index)&&planner.isCirculationReserved(index)));
	}
	{
		WorldState world=makeWorld();
		// Real racetracks grow from 4x4 to 6x6 on their first upgrade.
		world.profiles[3].levels[1].footprint=world.profiles[3].levels[2].footprint;
		Planner planner;planner.configure(world.profiles,1,2,6,5,7);
		DevelopmentIntent intent;intent.buildingType=3;intent.unmetCount=1;intent.priority=100;
		DevelopmentLimits limits;limits.newConstruction=1;DevelopmentAction build;
		assert(planner.selectAction(world,{intent},limits,build));assert(planner.reserve(world,build));
		planner.markIssued(build.id,20,0);
		WorldBuilding building;building.id=20;building.buildingType=3;building.level=1;
		building.centerX=build.centerX;building.centerY=build.centerY;building.hp=building.hpMax=100;
		world.buildings.push_back(building);occupy(world,build);planner.observe(world);
		for(int index:build.parcelTiles)if(!world.tiles[index].occupied)
		{world.tiles[index].resourceType=0;world.tiles[index].clearableResource=true;}
		limits.newConstruction=0;limits.allowUpgrades=true;limits.level1Upgrades=1;
		DevelopmentAction upgrade;assert(planner.selectAction(world,{},limits,upgrade));
		RejectionReason reason;
		assert(planner.revalidateSelection(world,{},limits,upgrade,&reason));
		assert(planner.reserve(world,upgrade));
		assert(!planner.revalidate(world,upgrade,&reason,true)&&reason==RejectedClearableResource);
		assert(planner.reservations().at(upgrade.reservationId).footprintTiles.size()==36);
		limits.activeLevel1Upgrades=1;
		assert(planner.revalidateSelection(world,{},limits,upgrade,&reason));
		limits.upgradePriorities[{3,1}]=0;
		assert(!planner.revalidateSelection(world,{},limits,upgrade,&reason)&&reason==RejectedAuthorization);
		limits.upgradePriorities[{3,1}]=100;
		// A pending clearing contract survives the existing save format.
		auto backend=new GAGCore::MemoryStreamBackend;
		auto output=new GAGCore::BinaryOutputStream(backend);planner.save(output);backend->seekFromStart(0);
		auto inputBackend=new GAGCore::MemoryStreamBackend(*backend);delete output;
		GAGCore::BinaryInputStream input(inputBackend);Planner restored;
		restored.configure(world.profiles,1,2,6,5,7);assert(restored.load(&input,92));
		assert(restored.actions().at(upgrade.id).state==ParcelReserved);
		for(int index:build.parcelTiles){world.tiles[index].resourceType=-1;world.tiles[index].clearableResource=false;}
		assert(restored.revalidateSelection(world,{},limits,upgrade,&reason));
		assert(restored.revalidate(world,upgrade,&reason,true));
		restored.markInvalidated(upgrade.id,UpgradeBlocked,world.computeSignature());
		assert(restored.reservations().size()==1);
		assert(restored.reservations().count(build.reservationId));
	}
	for(bool timeout:{false,true})
	{
		WorldState world=makeWorld();Planner planner;planner.configure(world.profiles,1,2,6,5,7);
		DevelopmentIntent intent;intent.buildingType=2;intent.unmetCount=4;intent.priority=100;
		DevelopmentLimits limits;limits.newConstruction=4;DevelopmentAction first,second;
		assert(planner.selectAction(world,{intent},limits,first));assert(first.templateId==HospitalCompact);
		assert(planner.reserve(world,first));planner.markIssued(first.id,10,0);
		assert(planner.selectAction(world,{intent},limits,second));assert(second.campusId==first.campusId);
		assert(planner.reserve(world,second));planner.markIssued(second.id,11,1);
		WorldBuilding building;building.id=11;building.buildingType=2;building.level=1;
		building.centerX=second.centerX;building.centerY=second.centerY;building.hp=building.hpMax=100;
		world.buildings.push_back(building);occupy(world,second);world.tick=2;planner.observe(world);
		if(timeout){world.tick=301;planner.observe(world);}
		else planner.markInvalidated(first.id,EngineRejected,world.computeSignature());
		assert(planner.campuses().size()==1);
		assert(planner.reservations().count(first.reservationId));
		limits.newConstruction=0;limits.level1Upgrades=1;limits.allowUpgrades=true;
		DevelopmentAction upgrade;assert(planner.selectAction(world,{},limits,upgrade));
		assert(upgrade.buildingId==11);
		// The shared contract is finally released when its last survivor dies.
		world.buildings.clear();for(auto& tile:world.tiles){tile.occupied=false;tile.ownOccupied=false;}
		planner.observe(world);assert(planner.campuses().empty()&&planner.reservations().empty());
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
	assert(planner.selectAction(world,{intent},limits,first));
	assert(planner.reserve(world,first));
	planner.markIssued(first.id,30,0);

	// The second has to be backed by capacity, and there is none.
	DevelopmentAction second;
	assert(!planner.selectAction(world,{intent},limits,second));
	assert(planner.diagnostics().rejected[RejectedFoodCapacity]>0);

	// A protected farm appears, and the same request now succeeds beside it.
	const int farmX=24,farmY=24;
	for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
		world.tile(farmX+dx,farmY+dy).protectedYield=80;
	DevelopmentAction backed;
	assert(planner.selectAction(world,{intent},limits,backed));
	// Judge the site by the capacity it can actually harvest rather than by a
	// distance proxy: harvesting routes are eight-neighbour steps and wrap, so a
	// site that looks distant in Manhattan terms can still work the same farm.
	assert(planner.reserve(world,backed));
	const AIMaximaFoodLedger::Result& after=planner.evaluateFoodLedger(world);
	const AIMaximaFoodLedger::ConsumerResult* placed=after.consumer(-(backed.id+1));
	assert(placed&&placed->claimed>0);
}

// A checkpoint taken during a one-cell search must preserve both its winner
// and the exact number of remaining slices (order timing is game behavior).
static void placementContinuationRegression()
{
    for(int boundary:{1,7,83})
    {
        WorldState world=makeWorld();
        Planner uninterrupted,restored;
        uninterrupted.configure(makeProfiles(),1,2,6,5,7);
        restored.configure(makeProfiles(),1,2,6,5,7);
        DevelopmentIntent intent;intent.buildingType=3;
        intent.unmetCount=2;intent.priority=80;intent.workers=2;
        DevelopmentLimits limits;limits.newConstruction=2;
        DevelopmentAction expected,actual;
        for(int step=0;step<boundary;++step)
            assert(uninterrupted.selectActionIncremental(world,{intent},limits,
                expected,world.computeSignature(),1)==SelectionPending);
        auto backend=new GAGCore::MemoryStreamBackend;
        auto output=new GAGCore::BinaryOutputStream(backend);
        uninterrupted.save(output);
        uninterrupted.saveExecutionState(output);
        backend->seekFromStart(0);
        auto copy=new GAGCore::MemoryStreamBackend(*backend);
        delete output;
        GAGCore::BinaryInputStream input(copy);
        assert(restored.load(&input,92));
        // The execution state was just written by the current writer, so it is
        // read back at the current format rather than the older planner one.
        restored.loadExecutionState(&input,VERSION_MINOR);
        SelectionProgress progress=SelectionPending;
        for(int step=0;step<10000&&progress==SelectionPending;++step)
        {
            progress=uninterrupted.selectActionIncremental(world,{intent},limits,
                expected,world.computeSignature(),1);
            assert(restored.selectActionIncremental(world,{intent},limits,
                actual,world.computeSignature(),1)==progress);
        }
        assert(progress==SelectionFound);
        assert(actual.type==expected.type&&actual.buildingType==expected.buildingType);
        assert(actual.centerX==expected.centerX&&actual.centerY==expected.centerY);
        assert(actual.utility.total==expected.utility.total);
        assert(actual.parcelTiles==expected.parcelTiles);
        assert(actual.accessTiles==expected.accessTiles);
        assert(actual.arteryTiles==expected.arteryTiles);
    }
}

int main()
{
	placementReviewRegressions();
	foodLedgerPlacementRegression();
	placementContinuationRegression();
	Planner planner;planner.configure(makeProfiles(),1,2,6,5,7);
	std::string error;assert(planner.validateTemplates(&error));
	bool compact=false,expandable=false,large=false;
	for(size_t i=0;i<planner.templates().size();++i)
	{
		const DevelopmentTemplate& value=planner.templates()[i];
		if(value.id==InnCompact){compact=true;assert(value.slots.size()==4);assert(value.slots[0].maximumLevel==2);}
		if(value.id==InnExpandable){expandable=true;assert(value.slots[0].terminalFootprint.width==3);}
		if(value.buildingType==3){large=true;assert(value.slots[0].terminalFootprint.width==6);}
	}
	assert(compact&&expandable&&large);
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
			assert(progress!=SelectionPending);return progress==SelectionFound;
		};
		available.activeNewConstruction=1;assert(!select());
		available.activeNewConstruction=0;assert(select());
		assert(selected.utility.capabilityGain==10);
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
		assert(reference.selectAction(world,intents,selectionLimits,expected));
		SelectionProgress progress=SelectionPending;
		for(int step=0;step<5000&&progress==SelectionPending;++step)
			progress=sliced.selectActionIncremental(world,intents,
				selectionLimits,actual,world.computeSignature(),1);
		assert(progress==SelectionFound);
		assert(actual.id==expected.id&&actual.type==expected.type);
		assert(actual.buildingType==expected.buildingType);
		assert(actual.centerX==expected.centerX&&actual.centerY==expected.centerY);
		assert(actual.utility.total==expected.utility.total);
		assert(actual.parcelTiles==expected.parcelTiles);
		assert(actual.accessTiles==expected.accessTiles);
		assert(actual.arteryTiles==expected.arteryTiles);
	}
	DevelopmentLimits limits;limits.newConstruction=2;DevelopmentAction first;
	assert(planner.selectAction(world,std::vector<DevelopmentIntent>(1,intent),limits,first));
	assert(!first.fallbackWaterTier);assert(planner.reserve(world,first));
	for(size_t i=0;i<planner.footprintReferences().size();++i)
		assert(!(planner.footprintReferences()[i]&&planner.circulationReferences()[i]));
	DevelopmentAction second;
	assert(planner.selectAction(world,std::vector<DevelopmentIntent>(1,intent),limits,second));
	for(size_t i=0;i<second.parcelTiles.size();++i)
		assert(!planner.isFootprintReserved(second.parcelTiles[i]));

	// Live counts and not-yet-observed build actions form one commitment count.
	// This is what prevents a one-building deficit from consuming every free site.
	WorldState commitmentWorld=makeWorld();Planner commitments;
	commitments.configure(makeProfiles(),1,2,6,5,7);
	commitmentWorld.swimmingBuilders=2;
	WorldBuilding existing;existing.id=50;existing.buildingType=3;existing.level=1;
	existing.centerX=4;existing.centerY=4;existing.hp=existing.hpMax=100;
	commitmentWorld.buildings.push_back(existing);
	assert(commitments.committedBuildingCount(commitmentWorld,3)==1);
	DevelopmentIntent oneBuilding;oneBuilding.buildingType=3;
	oneBuilding.unmetCount=1;oneBuilding.priority=80;oneBuilding.workers=2;
	DevelopmentLimits fourSites;fourSites.newConstruction=4;
	DevelopmentAction committed;
	assert(commitments.selectAction(commitmentWorld,
		std::vector<DevelopmentIntent>(1,oneBuilding),fourSites,committed));
	assert(commitments.reserve(commitmentWorld,committed));
	commitments.markIssued(committed.id,100,commitmentWorld.tick);
	assert(commitments.committedBuildingCount(commitmentWorld,3)==2);

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
	assert(!repairPlanner.selectAction(repairWorld,
		std::vector<DevelopmentIntent>(),repairLimits,repairAction));
	repairLimits.allowRepairs=true;
	assert(repairPlanner.selectAction(repairWorld,
		std::vector<DevelopmentIntent>(),repairLimits,repairAction));
	assert(repairAction.type==RepairBuilding);
	// Final authorization applies to repairs and upgrades as well as builds.
	repairLimits.newConstruction=1;
	RejectionReason currentReason=RejectedTerrain;
	assert(repairPlanner.revalidateSelection(repairWorld,
		std::vector<DevelopmentIntent>(),repairLimits,repairAction,&currentReason));
	repairLimits.allowRepairs=false;
	assert(!repairPlanner.revalidateSelection(repairWorld,
		std::vector<DevelopmentIntent>(),repairLimits,repairAction,&currentReason));
	assert(currentReason==RejectedAuthorization);
	repairLimits.allowRepairs=true;
	WorldState healthyWorld=repairWorld;healthyWorld.buildings[0].hp=100;
	assert(!repairPlanner.revalidateSelection(healthyWorld,
		std::vector<DevelopmentIntent>(),repairLimits,repairAction,&currentReason));
	assert(currentReason==RejectedUpgradeContract);
	repairPlanner.adoptStartingBuildings(healthyWorld);
	DevelopmentAction upgradeAction=repairAction;
	upgradeAction.type=UpgradeBuilding;upgradeAction.targetLevel=2;
	repairLimits.allowUpgrades=true;repairLimits.level1Upgrades=1;
	assert(repairPlanner.revalidateSelection(healthyWorld,
		std::vector<DevelopmentIntent>(),repairLimits,upgradeAction,&currentReason));
	repairLimits.allowUpgrades=false;
	assert(!repairPlanner.revalidateSelection(healthyWorld,
		std::vector<DevelopmentIntent>(),repairLimits,upgradeAction,&currentReason));
	assert(currentReason==RejectedAuthorization);
	repairLimits.allowUpgrades=true;repairLimits.activeLevel1Upgrades=1;
	assert(!repairPlanner.revalidateSelection(healthyWorld,
		std::vector<DevelopmentIntent>(),repairLimits,upgradeAction,&currentReason));
	assert(currentReason==RejectedAuthorization);

	// Director priorities change only demand, preserving every spatial score.
	// Zero is a veto both during selection and if authority changes before issue.
	{
		Planner upgrades;upgrades.configure(makeProfiles(),1,2,6,5,7);
		upgrades.adoptStartingBuildings(healthyWorld);
		DevelopmentLimits priorityLimits;priorityLimits.allowUpgrades=true;
		priorityLimits.level1Upgrades=1;
		const std::vector<DevelopmentIntent> noIntents;
		DevelopmentAction generic,weighted;
		assert(upgrades.selectAction(healthyWorld,noIntents,priorityLimits,generic));
		assert(generic.type==UpgradeBuilding && generic.buildingType==2);
		priorityLimits.upgradePriorities[std::make_pair(2,1)]=80;
		assert(upgrades.selectAction(healthyWorld,noIntents,priorityLimits,weighted));
		assert(weighted.buildingId==generic.buildingId);
		assert(weighted.utility.unmetDemand==80);
		assert(weighted.utility.total-generic.utility.total
			==(80-generic.utility.unmetDemand)*upgrades.policy().unmetDemandWeight);
		priorityLimits.upgradePriorities[std::make_pair(2,1)]=0;
		assert(!upgrades.revalidateSelection(healthyWorld,noIntents,
			priorityLimits,weighted,&currentReason));
		assert(currentReason==RejectedAuthorization);
		assert(!upgrades.selectAction(healthyWorld,noIntents,priorityLimits,weighted));
		// The upgrade veto must leave repair authorization independent.
		assert(upgrades.selectAction(repairWorld,noIntents,priorityLimits,weighted));
		assert(weighted.type==RepairBuilding);

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
		assert(progress==SelectionFound && weighted.buildingType==6);
		priorityLimits.upgradePriorities[std::make_pair(6,1)]=0;
		priorityLimits.upgradePriorities[std::make_pair(2,1)]=90;
		assert(upgrades.selectAction(choices,noIntents,priorityLimits,weighted));
		assert(weighted.buildingType==2 && weighted.utility.unmetDemand==90);
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
		assert(upgrades.selectAction(upgradeWorld,{},limits,winner));
		assert(winner.buildingType==4 && winner.utility.unmetDemand==80);
		limits.upgradePriorities[std::make_pair(3,1)]=80;
		limits.upgradePriorities[std::make_pair(4,1)]=1;
		assert(upgrades.selectAction(upgradeWorld,{},limits,winner));
		assert(winner.buildingType==3 && winner.utility.unmetDemand==80);
		assert(upgrades.selectActionIncremental(upgradeWorld,{},limits,winner,
			upgradeWorld.computeSignature(),1)==SelectionPending);
		limits.upgradePriorities[std::make_pair(3,1)]=0;
		limits.upgradePriorities[std::make_pair(4,1)]=0;
		SelectionProgress progress=SelectionPending;
		for(int i=0;i<10 && progress==SelectionPending;++i)
			progress=upgrades.selectActionIncremental(upgradeWorld,{},limits,winner,
				upgradeWorld.computeSignature(),1);
		assert(progress==SelectionFound && winner.buildingType==3);
		RejectionReason reason=RejectedTerrain;
		assert(!upgrades.revalidateSelection(upgradeWorld,{},limits,winner,&reason));
		assert(reason==RejectedAuthorization);
		assert(!upgrades.selectAction(upgradeWorld,{},limits,winner));
		// A priority veto suppresses upgrades, while damaged buildings can repair.
		upgradeWorld.buildings[0].hp=50;
		assert(upgrades.selectAction(upgradeWorld,{},limits,winner));
		assert(winner.type==RepairBuilding);
	}

	// Colony seeds share the Swarm type but carry independent placement state.
	WorldState colonyWorld;colonyWorld.reset(64,64);colonyWorld.profiles=makeProfiles();
	for(int y=0;y<64;++y)for(int x=0;x<64;++x)
	{colonyWorld.tile(x,y).discovered=true;colonyWorld.tile(x,y).grass=true;}
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
	assert(colonizer.selectAction(colonyWorld,
		std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
	assert(seed.purpose==ColonySeed);
	assert(seed.utility.friendlyDistance>=24&&seed.utility.frontierGain>=8);
	// Being far away is insufficient when an existing or planned inn already
	// claims this food. Test live revalidation, including pending reservations.
	{
		WorldState claimed=colonyWorld;
		WorldBuilding inn=home;inn.id=2;inn.buildingType=1;
		inn.centerX=43;inn.centerY=40;inn.site=true;
		claimed.buildings.push_back(inn);
		RejectionReason reason;
		assert(!colonizer.revalidateSelection(claimed,{colony},fourSites,seed,&reason));
		assert(reason==RejectedColonyDistance||reason==RejectedColonyCorn);
		assert(!colonizer.revalidate(claimed,seed,&reason,true));
		WorldState tiny=colonyWorld;tiny.tile(40,40).foodOpportunity=65536;
		assert(!colonizer.revalidateSelection(tiny,{colony},fourSites,seed,&reason));
		assert(reason==RejectedColonyCorn);
		WorldState blocked=colonyWorld;blocked.tile(40,40).foodTraversable=false;
		assert(!colonizer.revalidateSelection(blocked,{colony},fourSites,seed,&reason));
		assert(reason==RejectedColonyCorn);
		assert(colonizer.revalidateSelection(colonyWorld,{colony},fourSites,seed,&reason));
	}

	assert(colonizer.reserve(colonyWorld,seed));
	assert(colonizer.activeBuildCount(0,ColonySeed)==1);
	{
		RejectionReason reason;
		assert(colonizer.revalidateSelection(colonyWorld,{colony},fourSites,seed,&reason));
		DevelopmentAction duplicate=seed;duplicate.id=-1;
		assert(!colonizer.revalidateSelection(colonyWorld,{colony},fourSites,duplicate,&reason));
		assert(reason==RejectedColonyDistance||reason==RejectedColonyCorn);
	}

	assert(colonizer.activeBuildCount(0,CoreCapacity)==0);

	Planner independentlyBlocked;
	independentlyBlocked.configure(makeProfiles(),1,2,6,5,7);
	independentlyBlocked.adoptStartingBuildings(colonyWorld);
	independentlyBlocked.mutablePolicy().colonyMinimumAnchorDistance=65;
	assert(!independentlyBlocked.selectAction(colonyWorld,
		std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
	assert(independentlyBlocked.blockedSignature(0,ColonySeed)!=0);
	assert(independentlyBlocked.blockedSignature(0,CoreCapacity)==0);
	DevelopmentIntent coreSwarm=colony;coreSwarm.purpose=CoreCapacity;
	coreSwarm.requiredResourceType=-1;
	assert(independentlyBlocked.selectAction(colonyWorld,
		std::vector<DevelopmentIntent>(1,coreSwarm),fourSites,seed));
	WorldState islands=colonyWorld;
	for(size_t tile=0;tile<islands.tiles.size();++tile)
	{islands.tiles[tile].grass=false;islands.tiles[tile].water=true;
	 islands.tiles[tile].resourceType=-1;islands.tiles[tile].foodOpportunity=0;islands.tiles[tile].permanentResource=false;}
	for(int y=0;y<=20;++y)for(int x=0;x<=20;++x)
	{islands.tile(x,y).grass=true;islands.tile(x,y).water=false;}
	for(int y=32;y<=60;++y)for(int x=32;x<=60;++x)
	{islands.tile(x,y).grass=true;islands.tile(x,y).water=false;}
	islands.tile(45,45).resourceType=1; islands.tile(45,45).foodOpportunity=64*65536;
	islands.tile(45,45).resourceAmount=10;
	islands.tile(45,45).permanentResource=true;
	Planner dryColonizer;dryColonizer.configure(makeProfiles(),1,2,6,5,7);
	dryColonizer.adoptStartingBuildings(islands);
	assert(!dryColonizer.selectAction(islands,
		std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
	assert(dryColonizer.selectionSummary().rejected[RejectedIslandBuilders]>0);
	islands.swimmingBuilders=2;
	// Reuse the failed planner: training swimmers does not alter occupancy.
	assert(dryColonizer.selectAction(islands,
		std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
	Planner swimmingColonizer;
	swimmingColonizer.configure(makeProfiles(),1,2,6,5,7);
	swimmingColonizer.adoptStartingBuildings(islands);
	assert(swimmingColonizer.selectAction(islands,
		std::vector<DevelopmentIntent>(1,colony),fourSites,seed));
	assert(seed.requiresSwimmingBuilders&&seed.arteryTiles.empty());
	// A valid selection remains valid in an unchanged world, but current
	// authority and construction dependencies must win over its snapshot.
	{
		RejectionReason reason=RejectedTerrain;
		std::vector<DevelopmentIntent> intents(1,colony);
		assert(swimmingColonizer.revalidateSelection(islands,intents,fourSites,seed,&reason));
		WorldState changed=islands;changed.swimmingBuilders=1;
		assert(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
		assert(reason==RejectedIslandBuilders);
		assert(!swimmingColonizer.revalidateSelection(islands,
			std::vector<DevelopmentIntent>(),fourSites,seed,&reason));
		assert(reason==RejectedAuthorization);
		DevelopmentLimits stopped=fourSites;stopped.newConstruction=0;
		assert(!swimmingColonizer.revalidateSelection(islands,intents,stopped,seed,&reason));
		assert(reason==RejectedAuthorization);
		changed=islands;changed.tiles[seed.parcelTiles.front()].threat=100;
		assert(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
		assert(reason==RejectedColonyThreat);
		changed=islands;changed.tile(45,45).resourceType=-1;
		assert(swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
		changed.tile(45,45).foodOpportunity=0;
		assert(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
		assert(reason==RejectedRequiredSource);
		changed=islands;changed.tiles[seed.accessTiles.front()].occupied=true;
		assert(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
		assert(reason==RejectedAccess);
		// Relocated corn is still present globally but no longer supports this
		// colony. This catches reuse of distance caches from the old snapshot.
		changed=islands;changed.tile(45,45).resourceType=-1;changed.tile(45,45).foodOpportunity=0;
		changed.tile(8,8).resourceType=1;changed.tile(8,8).foodOpportunity=65536;
		assert(!swimmingColonizer.revalidateSelection(changed,intents,fourSites,seed,&reason));
		assert(reason==RejectedColonyCorn);
		assert(swimmingColonizer.revalidateSelection(islands,intents,fourSites,seed,&reason));
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
		assert(colonyRestored.load(serializedInput,92));
		delete serializedInput;
		assert(colonyRestored.activeBuildCount(0,ColonySeed)==1);
		assert(colonyRestored.actions().begin()->second.purpose==ColonySeed);
	}
	WorldBuilding observed;observed.id=100;observed.buildingType=3;observed.level=1;
	observed.centerX=committed.centerX;observed.centerY=committed.centerY;
	observed.hp=observed.hpMax=100;observed.site=true;
	commitmentWorld.buildings.push_back(observed);
	assert(commitments.committedBuildingCount(commitmentWorld,3)==2);

	// A complete compact campus needs no internal aisle: its permanent outer
	// ring protects circulation while all four slots can be filled.
	WorldState campusWorld=makeWorld();Planner campus;
	campus.configure(makeProfiles(),1,2,6,5,7);DevelopmentIntent hospital;
	hospital.buildingType=2;hospital.unmetCount=4;hospital.priority=80;hospital.workers=2;
	DevelopmentLimits campusLimits;campusLimits.newConstruction=4;
	for(int member=0;member<4;++member)
	{
		DevelopmentAction action;
		assert(campus.selectAction(campusWorld,
			std::vector<DevelopmentIntent>(1,hospital),campusLimits,action));
		assert(action.templateId==HospitalCompact);
		assert(campus.reserve(campusWorld,action));
		RejectionReason issueReason=RejectedReservation;
		assert(campus.revalidate(campusWorld,action,&issueReason,true));
		campus.markIssued(action.id,100+member,member);
		WorldBuilding building;building.id=100+member;building.buildingType=2;
		building.level=1;building.centerX=action.centerX;building.centerY=action.centerY;
		building.hp=building.hpMax=100;campusWorld.buildings.push_back(building);
		occupy(campusWorld,action);campusWorld.tick=member+1;campus.observe(campusWorld);
	}
	assert(campus.campuses().size()==1);
	for(size_t slot=0;slot<campus.campuses()[0].slots.size();++slot)
		assert(campus.campuses()[0].slots[slot].buildingId>=0);
	for(size_t i=0;i<campus.footprintReferences().size();++i)
		assert(!(campus.footprintReferences()[i]&&campus.circulationReferences()[i]));

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
	assert(restored.load(input,92));delete input;
	assert(restored.campuses().size()==campus.campuses().size());
	assert(restored.reservations().size()==campus.reservations().size());
	assert(restored.actions().size()==campus.actions().size());
	assert(restored.footprintReferences()==campus.footprintReferences());
	assert(restored.circulationReferences()==campus.circulationReferences());
	DevelopmentIntent afterLoad;afterLoad.buildingType=3;afterLoad.unmetCount=1;
	afterLoad.priority=80;afterLoad.workers=2;DevelopmentAction originalNext,restoredNext;
	assert(campus.selectAction(campusWorld,std::vector<DevelopmentIntent>(1,afterLoad),
		campusLimits,originalNext));
	assert(restored.selectAction(campusWorld,std::vector<DevelopmentIntent>(1,afterLoad),
		campusLimits,restoredNext));
	assert(originalNext.type==restoredNext.type);
	assert(originalNext.templateId==restoredNext.templateId);
	assert(originalNext.centerX==restoredNext.centerX);
	assert(originalNext.centerY==restoredNext.centerY);
	assert(originalNext.utility.total==restoredNext.utility.total);

	// A terminal footprint adopted for a starting building must not overlap the
	// access ring, including an inn whose level-three footprint expands.
	WorldState adoptedWorld=makeWorld();WorldBuilding starting;
	starting.id=9;starting.buildingType=1;starting.level=1;starting.centerX=12;
	starting.centerY=12;starting.hp=starting.hpMax=100;adoptedWorld.buildings.push_back(starting);
	DevelopmentAction startingFootprint;startingFootprint.centerX=12;startingFootprint.centerY=12;
	startingFootprint.initialFootprint=makeProfiles()[1].atLevel(1)->footprint;
	occupy(adoptedWorld,startingFootprint);Planner adopted;
	adopted.configure(makeProfiles(),1,2,6,5,7);adopted.adoptStartingBuildings(adoptedWorld);
	assert(adopted.standaloneContracts().size()==1);
	assert(adopted.standaloneContracts()[0].maximumLevel==3);
	for(size_t i=0;i<adopted.footprintReferences().size();++i)
		assert(!(adopted.footprintReferences()[i]&&adopted.circulationReferences()[i]));

	// Disabling arteries must not label a separated, land-connected parcel an island.
	{
		Planner unrouted;unrouted.configure(makeProfiles(),1,2,6,5,7);
		unrouted.adoptStartingBuildings(adoptedWorld);
		unrouted.mutablePolicy().arteryRoutingEnabled=false;
		unrouted.mutablePolicy().spacingWeight=100;
		DevelopmentAction action;
		assert(unrouted.selectAction(adoptedWorld,{intent},limits,action));
		assert(action.utility.friendlyDistance>=2);
		assert(!action.requiresSwimmingBuilders);
		assert(unrouted.revalidateSelection(adoptedWorld,{intent},limits,action));
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
	assert(spaced.selectAction(adoptedWorld,
		std::vector<DevelopmentIntent>(1,spacedIntent),limits,spacedAction));
	assert(spacedAction.utility.friendlyDistance>=2);

	// A reservation may preserve resources in future upgrade/campus space, but
	// the building's immediate engine footprint must already be empty. Placement
	// no longer turns the larger reservation into destructive clearing work.
	WorldState resourceWorld=makeWorld();
	for(size_t i=0;i<resourceWorld.tiles.size();++i)
	{
		if(!resourceWorld.tiles[i].grass)continue;
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
	assert(preserving.selectAction(resourceWorld,
		std::vector<DevelopmentIntent>(1,largeIntent),limits,preservingAction));
	bool preservedResource=false;
	for(size_t i=0;i<preservingAction.parcelTiles.size();++i)
		preservedResource=preservedResource
			||resourceWorld.tiles[preservingAction.parcelTiles[i]].clearableResource;
	assert(preservedResource);
	assert(preserving.reserve(resourceWorld,preservingAction));
	RejectionReason preservingReason=RejectedReservation;
	assert(preserving.revalidate(resourceWorld,preservingAction,
		&preservingReason,true));

	// New inner-settlement parcels leave valuable food zones to inns and swarms.
	// The halo penalty does not apply to food infrastructure itself.
	WorldState foodWorld=makeWorld();Planner unpenalized;
	unpenalized.configure(makeProfiles(),1,2,6,5,7);
	unpenalized.mutablePolicy().foodZonePenaltyWeight=0;
	DevelopmentIntent schoolIntent;schoolIntent.buildingType=6;
	schoolIntent.unmetCount=1;schoolIntent.priority=80;schoolIntent.workers=2;
	DevelopmentAction baselineSchool;
	assert(unpenalized.selectAction(foodWorld,
		std::vector<DevelopmentIntent>(1,schoolIntent),limits,baselineSchool));
	foodWorld.tile(baselineSchool.centerX,baselineSchool.centerY)
		.foodOpportunity=1000;
	Planner foodAware;foodAware.configure(makeProfiles(),1,2,6,5,7);
	DevelopmentAction protectedSchool;
	assert(foodAware.selectAction(foodWorld,
		std::vector<DevelopmentIntent>(1,schoolIntent),limits,protectedSchool));
	assert(protectedSchool.centerX!=baselineSchool.centerX
		||protectedSchool.centerY!=baselineSchool.centerY);
	assert(protectedSchool.utility.foodZonePressure==0);
	DevelopmentIntent innIntent;innIntent.buildingType=1;
	innIntent.unmetCount=1;innIntent.priority=80;innIntent.workers=2;
	DevelopmentAction foodInn;
	assert(foodAware.selectAction(foodWorld,
		std::vector<DevelopmentIntent>(1,innIntent),limits,foodInn));
	assert(foodInn.utility.foodZonePressure==0);

	// Missing required sources and negative utility become stable waiting
	// decisions. An unchanged signature does not scan the map again.
	Planner blocked;blocked.configure(makeProfiles(),1,2,6,5,7);
	DevelopmentIntent required;required.buildingType=1;required.unmetCount=1;
	required.priority=80;required.requiredResourceType=1;DevelopmentAction none;
	assert(!blocked.selectAction(makeWorld(),std::vector<DevelopmentIntent>(1,required),limits,none));
	assert(blocked.selectionSummary().rejected[RejectedRequiredSource]==1);
	assert(!blocked.actions().empty());
	assert(blocked.actions().begin()->second.state==RequiredSourceMissing);
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
	assert(!negative.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,none));
	assert(negative.selectionSummary().rejected[RejectedNegativeUtility]==1);
	assert(!negative.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,none));
	assert(negative.selectionSummary().candidateCount==0);

	// Negative utility is only stable while its scoring inputs and demand match.
	negative.mutablePolicy().unmetDemandWeight=20;
	ordinary.priority=0;
	assert(!negative.selectAction(unchanged,{ordinary},limits,none));
	ordinary.priority=100;
	assert(negative.selectAction(unchanged,{ordinary},limits,none));

	// A create timeout is distinct and quarantines the failed coordinate while
	// the world remains otherwise unchanged.
	Planner timeout;timeout.configure(makeProfiles(),1,2,6,5,7);DevelopmentAction timed;
	assert(timeout.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,timed));
	assert(timeout.reserve(unchanged,timed));timeout.markIssued(timed.id,77,0);
	unchanged.tick=301;timeout.observe(unchanged);
	assert(timeout.actions().find(timed.id)->second.state==CreateTimedOut);
	DevelopmentAction replanned;
	assert(timeout.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),limits,replanned));
	assert(replanned.centerX!=timed.centerX||replanned.centerY!=timed.centerY);
	// Discovery/resource churn elsewhere must not reactivate a locally failed
	// engine coordinate.
	unchanged.tile(20,20).resourceType=1;unchanged.tile(20,20).resourceAmount=7;
	DevelopmentAction afterRemoteChange;
	assert(timeout.selectAction(unchanged,std::vector<DevelopmentIntent>(1,ordinary),
		limits,afterRemoteChange));
	assert(afterRemoteChange.centerX!=timed.centerX
		||afterRemoteChange.centerY!=timed.centerY);

	std::cout<<"templates="<<planner.templates().size()
		<<" first="<<first.centerX<<","<<first.centerY
		<<" second="<<second.centerX<<","<<second.centerY
		<<" campus_members="<<campus.campuses()[0].slots.size()<<"\n";
	return 0;
}
