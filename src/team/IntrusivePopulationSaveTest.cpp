// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "LegacyPopulationOutput.h"
#include "Version.h"
#include "gradient/GradientRuntime.h"
#include <BinaryStream.h>
#include <TextStream.h>
#include <StreamBackend.h>
#include <memory>
#include <algorithm>
#include <exception>

namespace {
std::string savePopulation(Game& game,bool text) {
	auto* backend=new GAGCore::MemoryStreamBackend;
	std::unique_ptr<GAGCore::OutputStream> output(text
		? static_cast<GAGCore::OutputStream*>(new GAGCore::TextOutputStream(backend))
		: static_cast<GAGCore::OutputStream*>(new GAGCore::BinaryOutputStream(backend)));
	game.save(output.get(),false,"intrusive population continuation"); output->flush();
	return backend->takeContents();
}
bool loadPopulation(Game& game,const std::string& bytes,bool text) {
	auto backend=std::make_unique<GAGCore::MemoryStreamBackend>(bytes.data(),bytes.size());
	backend->seekFromStart(0);
	if (text) { GAGCore::TextInputStream input(backend.get()); return game.load(&input); }
	GAGCore::BinaryInputStream input(backend.release()); return game.load(&input);
}
template<class List> std::vector<Uint16> populationOrder(const List& list) {
	std::vector<Uint16> result; for (const auto* item : list) result.push_back(item->gid); return result;
}
std::vector<Uint32> populationState(Game& game) {
	std::vector<Uint32> stages,buildings,units;
	game.checkSum(&stages,&buildings,&units,true);
	// MapHeader's on-disk version is recorded when loading a fresh setup.
	stages.erase(stages.begin());
	stages.insert(stages.end(),buildings.begin(),buildings.end());
	stages.insert(stages.end(),units.begin(),units.end());
	return stages;
}
}
TEST_SUITE("IntrusivePopulationSave") {
TEST_CASE("binary and text saves restore nonslot order scheduler and continuation") {
	glob2test::HeadlessGlobals globals;
	for (bool text : {false,true}) {
		glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
		auto& game=world.game; auto* team=game.teams[0];
		auto* first=world.addUnit(WORKER,8,12,0);
		auto* gap=world.addUnit(WORKER,9,12,0);
		auto* final=world.addUnit(WORKER,10,12,0);
		REQUIRE(first); REQUIRE(gap); REQUIRE(final);
		REQUIRE(game.removeUnitAndBuildingAndFlags(9,12,unsigned(Game::DEL_GROUND_UNIT)));
		REQUIRE(world.addUnit(WORKER,9,12,0));
		REQUIRE(world.addBuilding("inn",4,4,0,0));
		REQUIRE(world.addBuilding("inn",14,4,0,0));
		REQUIRE(world.addBuilding("inn",24,4,0,0));
		REQUIRE(game.removeUnitAndBuildingAndFlags(14,4,unsigned(Game::DEL_BUILDING)));
		REQUIRE(world.addBuilding("inn",14,4,0,0));
		team->taskSwapNext=Unit::GIDtoID(final->gid);
		const auto units=populationOrder(team->liveUnits), buildings=populationOrder(team->liveBuildings);
		REQUIRE_FALSE(std::is_sorted(units.begin(),units.end()));
		REQUIRE_FALSE(std::is_sorted(buildings.begin(),buildings.end()));
		const auto next=team->taskSwapNext;
		const auto bytes=savePopulation(game,text);
		glob2test::HeadlessGame restored({.header=true});
		REQUIRE(loadPopulation(restored.game,bytes,text));
		auto* loaded=restored.game.teams[0];
		CHECK(populationOrder(loaded->liveUnits)==units);
		CHECK(populationOrder(loaded->liveBuildings)==buildings);
		CHECK(loaded->taskSwapNext==next);
		CHECK(loaded->liveUnits.matches(loaded->myUnits,Unit::MAX_COUNT));
		CHECK(loaded->liveBuildings.matches(loaded->myBuildings,Building::MAX_COUNT));
		CHECK(populationState(restored.game)==populationState(game));
		game.setWaitingOnMask(0); restored.game.setWaitingOnMask(0);
		for (int tick=0;tick<32;++tick) {
			game.step(0); restored.game.step(0);
			CHECK(populationState(restored.game)==populationState(game));
			CHECK(populationOrder(loaded->liveUnits)==populationOrder(team->liveUnits));
			CHECK(loaded->taskSwapNext==team->taskSwapNext);
		}
	}
}
TEST_CASE("formats164 through166 preserve population encoding and scheduler boundaries") {
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
	auto& game=world.game; auto* team=game.teams[0];
	REQUIRE(world.addUnit(WORKER,8,12,0)); REQUIRE(world.addUnit(WORKER,9,12,0));
	REQUIRE(world.addUnit(WORKER,10,12,0));
	REQUIRE(game.removeUnitAndBuildingAndFlags(9,12,unsigned(Game::DEL_GROUND_UNIT)));
	REQUIRE(world.addUnit(WORKER,9,12,0));
	REQUIRE(world.addBuilding("inn",4,4,0,0)); REQUIRE(world.addBuilding("inn",14,4,0,0));
	REQUIRE(world.addBuilding("inn",24,4,0,0));
	REQUIRE(game.removeUnitAndBuildingAndFlags(14,4,unsigned(Game::DEL_BUILDING)));
	REQUIRE(world.addBuilding("inn",14,4,0,0));
	auto expectedUnits=populationOrder(team->liveUnits);
	auto expectedBuildings=populationOrder(team->liveBuildings);
	REQUIRE_FALSE(std::is_sorted(expectedUnits.begin(),expectedUnits.end()));
	REQUIRE_FALSE(std::is_sorted(expectedBuildings.begin(),expectedBuildings.end()));
	std::sort(expectedUnits.begin(),expectedUnits.end());
	std::sort(expectedBuildings.begin(),expectedBuildings.end());
	// A real pre166 simulation always traversed slot order. The synthetic adapter
	// rewrites Team records but map runtime continuation still has implicit order,
	// so canonicalize this deliberately nonslot fixture before emitting the historical format.
	std::vector<Unit*> units(team->liveUnits.begin(),team->liveUnits.end());
	std::vector<Building*> buildings(team->liveBuildings.begin(),team->liveBuildings.end());
	for (auto* unit : units) team->detachUnit(Unit::GIDtoID(unit->gid));
	for (auto* building : buildings) team->detachBuilding(Building::GIDtoID(building->gid));
	std::sort(units.begin(),units.end(),[](const auto* a,const auto* b) { return a->gid<b->gid; });
	std::sort(buildings.begin(),buildings.end(),[](const auto* a,const auto* b) { return a->gid<b->gid; });
	for (auto* unit : units) team->attachUnit(Unit::GIDtoID(unit->gid),unit);
	for (auto* building : buildings) team->attachBuilding(Building::GIDtoID(building->gid),building);
	team->taskSwapNext=Unit::GIDtoID(units.back()->gid); // Historical formats must not persist this new field.
	struct HistoricalOutput : glob2test::LegacyPopulationOutput {
		int version;
		HistoricalOutput(GAGCore::MemoryStreamBackend* backend,Game& game,int version)
			: LegacyPopulationOutput(backend,&game,version),version(version) {}
		void writeSint32(Sint32 value,std::string_view name) override {
			LegacyPopulationOutput::writeSint32(name=="versionMinor" ? version : value,name);
		}
	};
	for (int version : {164,165,166}) {
		CAPTURE(version);
		auto* backend=new GAGCore::MemoryStreamBackend;
		HistoricalOutput output(backend,game,version);
		game.save(&output,false,"historical population bootstrap"); output.flush();
		const auto bytes=backend->takeContents();
		glob2test::HeadlessGame restored({.header=true});
		REQUIRE(loadPopulation(restored.game,bytes,false));
		CHECK(restored.game.mapHeader.getVersionMinor()==version);
		auto* loaded=restored.game.teams[0];
		CHECK(populationOrder(loaded->liveUnits)==expectedUnits);
		CHECK(populationOrder(loaded->liveBuildings)==expectedBuildings);
		CHECK(loaded->taskSwapNext==Unit::GIDtoID(version<166 ? expectedUnits.front() : expectedUnits.back()));
		CHECK(loaded->liveUnits.matches(loaded->myUnits,Unit::MAX_COUNT));
		CHECK(loaded->liveBuildings.matches(loaded->myBuildings,Building::MAX_COUNT));
		CHECK(restored.game.teams[1]->liveUnits.empty());
		CHECK(restored.game.teams[1]->liveBuildings.empty());
		CHECK(restored.game.teams[1]->taskSwapNext==0xffff);
	}
}
TEST_CASE("format166 pending jobs and append order survive an immediate upgrade before observation") {
    glob2test::HeadlessGlobals globals;
    std::string historical;
    std::vector<Uint16> expectedUnits,expectedBuildings;
    Uint16 expectedNext=0xffff;
    std::size_t pendingFields=0,pendingGrowth=0;
    {
        glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
        auto& game=world.game; auto* team=game.teams[0];
        REQUIRE(world.addUnit(WORKER,8,12)); REQUIRE(world.addUnit(WORKER,9,12));
        auto* final=world.addUnit(WORKER,10,12); REQUIRE(final);
        REQUIRE(game.removeUnitAndBuildingAndFlags(9,12,unsigned(Game::DEL_GROUND_UNIT)));
        REQUIRE(world.addUnit(WORKER,9,12));
        REQUIRE(world.addBuilding("inn",4,4)); REQUIRE(world.addBuilding("inn",14,4));
        REQUIRE(world.addBuilding("inn",24,4));
        REQUIRE(game.removeUnitAndBuildingAndFlags(14,4,unsigned(Game::DEL_BUILDING)));
        REQUIRE(world.addBuilding("inn",14,4));
        team->taskSwapNext=Unit::GIDtoID(final->gid);
        expectedUnits=populationOrder(team->liveUnits);
        expectedBuildings=populationOrder(team->liveBuildings);
        expectedNext=team->taskSwapNext;
        REQUIRE_FALSE(std::is_sorted(expectedUnits.begin(),expectedUnits.end()));
        REQUIRE_FALSE(std::is_sorted(expectedBuildings.begin(),expectedBuildings.end()));
        // Released format166 used synchronous cold construction. Prime complete
        // reference generations so the historical save contains representable
        // warm jobs rather than new-format cold publication requests.
        for (auto* building:team->liveBuildings) {
            const auto slot=building->routeSlot(0,BuildingRoute::Automatic);
            REQUIRE(!building->globalGradient[slot]);
            building->globalGradient[slot]=game.map.acquireBuildingGradientBuffer();
            game.map.updateGlobalGradient(building,0);
        }
        game.map.setResourceByIndex(20,20,WHEAT,1);
        glob2test::LegacyPopulationOutput::primeMaterialGradient(game.map,0,WHEAT,0);
        game.map.configureCompute(1);
        game.map.configureGradientPipeline(0,3);
        for (auto* building:team->liveBuildings)
            game.map.requestBuildingRefresh(building,building->routeSlot(0,BuildingRoute::Automatic));
        game.map.updateMaterialGradient(0,WHEAT,0);
        game.observeCompletedWorld();
        REQUIRE(game.stepCounter==0);
        pendingFields=game.map.gradientPipelineStatus().pending;
        pendingGrowth=game.map.gradientRuntime->growth.count();
        REQUIRE(pendingFields>0); REQUIRE(pendingGrowth==1);
        struct HistoricalOutput : glob2test::LegacyPopulationOutput {
            HistoricalOutput(GAGCore::MemoryStreamBackend* backend,Game& game)
                : LegacyPopulationOutput(backend,&game,166,GradientLayout::PreservePending) {}
            void writeSint32(Sint32 value,std::string_view name) override {
                BinaryOutputStream::writeSint32(name=="versionMinor" ? 166 : value,name);
            }
        };
        auto* backend=new GAGCore::MemoryStreamBackend;
        HistoricalOutput output(backend,game);
        game.save(&output,false,"format166 pending observation"); output.flush();
        historical=backend->takeContents();
    }
    for (unsigned workers : {1u,4u}) {
        CAPTURE(workers);
        glob2test::HeadlessGame direct({.header=true}),upgraded({.header=true});
        REQUIRE(loadPopulation(direct.game,historical,false));
        REQUIRE(direct.game.mapHeader.getVersionMinor()==166);
        CHECK(populationOrder(direct.game.teams[0]->liveUnits)==expectedUnits);
        CHECK(populationOrder(direct.game.teams[0]->liveBuildings)==expectedBuildings);
        CHECK(direct.game.teams[0]->taskSwapNext==expectedNext);
        CHECK(direct.game.map.gradientPipelineStatus().pending==pendingFields);
        CHECK(direct.game.map.gradientRuntime->growth.count()==pendingGrowth);
        // Save the migration before any observation can publish old jobs. The
        // current format must retain submitted-without-published phase state.
        CHECK_THROWS_AS(direct.game.observation(),std::logic_error);
        const auto current=savePopulation(direct.game,false);
        REQUIRE(loadPopulation(upgraded.game,current,false));
        CHECK(upgraded.game.mapHeader.getVersionMinor()==VERSION_MINOR);
        CHECK_THROWS_AS(upgraded.game.observation(),std::logic_error);
        direct.game.map.configureCompute(1);
        upgraded.game.map.configureCompute(workers);
        for (Game* game : {&direct.game,&upgraded.game}) {
            CHECK(game->map.gradientPipelineStatus().pending==pendingFields);
            CHECK(game->map.gradientRuntime->growth.count()==pendingGrowth);
            game->setWaitingOnMask(0);
            game->observeCompletedWorld();
            // Restored T0 jobs retain their original inputs. Bootstrap must not
            // admit a replacement growth batch or material-field job at T0.
            CHECK(game->map.resourceGrowthMetrics().submitted==0);
            CHECK(game->map.gradientPipelineStatus().jobs==0);
            CHECK(game->map.gradientRuntime->growth.count()==pendingGrowth);
        }
        CHECK(populationState(upgraded.game)==populationState(direct.game));
        for (unsigned tick=0;tick<16;++tick) {
            CAPTURE(tick);
            direct.game.step(0); upgraded.game.step(0);
            CHECK(populationState(upgraded.game)==populationState(direct.game));
            CHECK(populationOrder(upgraded.game.teams[0]->liveUnits)==populationOrder(direct.game.teams[0]->liveUnits));
            CHECK(populationOrder(upgraded.game.teams[0]->liveBuildings)==populationOrder(direct.game.teams[0]->liveBuildings));
            CHECK(upgraded.game.teams[0]->taskSwapNext==direct.game.teams[0]->taskSwapNext);
        }
    }
}
TEST_CASE("invalid population records fail without leaving broken membership") {
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
	REQUIRE(world.addUnit(WORKER,8,12,0)); REQUIRE(world.addUnit(WORKER,9,12,0));
	REQUIRE(world.addBuilding("inn",4,4,0,0));
	REQUIRE(world.addBuilding("inn",14,4,0,0));
	auto* teamBackend=new GAGCore::MemoryStreamBackend;
	GAGCore::TextOutputStream teamOutput(teamBackend);
	world.game.teams[0]->save(&teamOutput); teamOutput.flush();
	const auto original=teamBackend->takeContents();
	// Reuse one initialized destination: every failed partial load must be safe
	// to clear and retry. This also avoids rebuilding complete game catalogs
	// for each independent small Team record corruption.
	glob2test::HeadlessGame restored({.header=true});
	// Ensure the small fixture is independently loadable before corrupting it.
	{
		GAGCore::MemoryStreamBackend backend(original.data(),original.size());
		backend.seekFromStart(0);
		GAGCore::TextInputStream input(&backend);
		REQUIRE(restored.game.teams[0]->load(&input,&restored.game.buildingsTypes,VERSION_MINOR));
		CHECK(populationOrder(restored.game.teams[0]->liveUnits)==populationOrder(world.game.teams[0]->liveUnits));
		CHECK(populationOrder(restored.game.teams[0]->liveBuildings)==populationOrder(world.game.teams[0]->liveBuildings));
	}
	const auto replaceField=[](std::string& bytes,const std::string& section,const std::string& field,unsigned occurrence,unsigned value) {
		std::size_t position=bytes.find(section);
		REQUIRE(position!=std::string::npos);
		for (unsigned index=0;index<=occurrence;++index) {
			position=bytes.find(field+" = ",position);
			REQUIRE(position!=std::string::npos);
			position+=field.size()+3;
			if (index<occurrence) position=bytes.find(';',position)+1;
		}
		const auto end=bytes.find(';',position); REQUIRE(end!=std::string::npos);
		bytes.replace(position,end-position,std::to_string(value));
	};
	for (int invalid=0;invalid<11;++invalid) {
		auto bytes=original;
		if (invalid==0) replaceField(bytes,"myUnits","slot",1,0); // duplicate ID
		if (invalid==1) replaceField(bytes,"myUnits","slot",0,Unit::MAX_COUNT);
		if (invalid==2) replaceField(bytes,"myUnits","gid",0,Unit::GIDfrom(0,1)); // wrong owner
		if (invalid==3) replaceField(bytes,"myUnits","gid",0,1); // mismatched slot
		if (invalid==4) replaceField(bytes,"myUnits","count",0,Unit::MAX_COUNT+1);
		if (invalid==5) replaceField(bytes,"myUnits","taskSwapNext",0,Unit::MAX_COUNT);
		if (invalid==6) replaceField(bytes,"myBuildings","slot",1,0);
		if (invalid==7) replaceField(bytes,"myBuildings","slot",0,Building::MAX_COUNT);
		if (invalid==8) replaceField(bytes,"myBuildings","gid",0,Building::GIDfrom(0,1));
		if (invalid==9) replaceField(bytes,"myBuildings","gid",0,1);
		if (invalid==10) replaceField(bytes,"myBuildings","count",0,Building::MAX_COUNT+1);
		CAPTURE(invalid);
		bool rejected=false;
		try {
			GAGCore::MemoryStreamBackend backend(bytes.data(),bytes.size());
			backend.seekFromStart(0);
			GAGCore::TextInputStream input(&backend);
			rejected=!restored.game.teams[0]->load(&input,&restored.game.buildingsTypes,VERSION_MINOR);
		}
		catch (const std::exception&) { rejected=true; }
		CHECK(rejected);
		if (restored.game.teams[0]) {
			auto* team=restored.game.teams[0];
			CHECK(team->liveUnits.matches(team->myUnits,Unit::MAX_COUNT));
			CHECK(team->liveBuildings.matches(team->myBuildings,Building::MAX_COUNT));
			CHECK((team->taskSwapNext==0xffff || (team->taskSwapNext<Unit::MAX_COUNT && team->myUnits[team->taskSwapNext])));
		}
	}
	// A valid record must still succeed after the final failed partial load.
	{
		GAGCore::MemoryStreamBackend backend(original.data(),original.size());
		backend.seekFromStart(0);
		GAGCore::TextInputStream input(&backend);
		REQUIRE(restored.game.teams[0]->load(&input,&restored.game.buildingsTypes,VERSION_MINOR));
		CHECK(populationOrder(restored.game.teams[0]->liveUnits)==populationOrder(world.game.teams[0]->liveUnits));
		CHECK(populationOrder(restored.game.teams[0]->liveBuildings)==populationOrder(world.game.teams[0]->liveBuildings));
		CHECK(restored.game.teams[0]->taskSwapNext==world.game.teams[0]->taskSwapNext);
	}
	const auto binary=savePopulation(world.game,false);
	glob2test::HeadlessGame truncated({.header=true});
	bool rejected=false;
	try { rejected=!loadPopulation(truncated.game,binary.substr(0,binary.size()/2),false); }
	catch (const std::exception&) { rejected=true; }
	CHECK(rejected);
}

}
