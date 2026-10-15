// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
#include "Game.h"
#include "Team.h"
#include "Unit.h"
#include "Building.h"
#include "IntrusiveEntityList.h"
#include <vector>
#include <type_traits>
#include <memory>

namespace {
template<class T> constexpr bool hasPopulationHooks = requires(T& entity) {
    entity.populationPrevious; entity.populationNext; entity.populationMembership;
};
static_assert(!hasPopulationHooks<UnitState>);
static_assert(!hasPopulationHooks<BuildingStateRecord>);
static_assert(!std::is_copy_constructible_v<Unit>);
struct PopulationNode {
	PopulationNode* populationPrevious=nullptr;
	PopulationNode* populationNext=nullptr;
	const void* populationMembership=nullptr;
	int gid;
	static int GIDtoID(int id) { return id; }
};
using Population=IntrusiveEntityList<PopulationNode>;
std::vector<int> ids(const Population& population) {
	std::vector<int> result;
	for (auto* node : population) result.push_back(node->gid);
	return result;
}
}

TEST_SUITE("TeamLiveLists") {
TEST_CASE("population append unlink preserves survivor order and slot reuse appends") {
	Population population;
	PopulationNode a{.gid=4},b{.gid=1},c{.gid=7},replacement{.gid=1};
	CHECK(population.empty());
	population.append(a); CHECK(population.front()==&a); CHECK(population.back()==&a);
	population.append(b); population.append(c);
	CHECK((ids(population)==std::vector<int>{4,1,7}));
	population.remove(b); population.append(replacement);
	CHECK((ids(population)==std::vector<int>{4,7,1}));
	population.remove(a); CHECK(population.front()==&c);
	population.remove(replacement); CHECK(population.back()==&c);
	population.remove(c); CHECK(population.empty());
	CHECK(population.size()==0);
}
TEST_CASE("mutable population walk repairs pending removals and retains original fence") {
	Population population;
	PopulationNode a{.gid=0},b{.gid=1},c{.gid=2},arrival{.gid=3};
	population.append(a); population.append(b); population.append(c);
	{
		auto walk=population.mutableWalk();
		CHECK(walk.next()==&a);
		population.remove(a); // current may be freed immediately
		population.append(arrival);
		population.remove(c); // original tail cannot expose arrival
		CHECK(walk.next()==&b);
		CHECK(walk.next()==nullptr);
	}
	population.clear();
	population.append(a); population.append(b); population.append(c);
	{
		auto walk=population.mutableWalk(); CHECK(walk.next()==&a);
		population.remove(b); // remove upcoming
		CHECK(walk.next()==&c); CHECK(walk.next()==nullptr);
	}
	population.clear();
}
TEST_CASE("transfers repair nested cursors and destination captures arrivals by phase") {
	Population source,destination;
	PopulationNode a{.gid=0},b{.gid=1},c{.gid=2};
	source.append(a); source.append(b); destination.append(c);
	{
		auto earlyDestination=destination.mutableWalk();
		auto outer=source.mutableWalk(); CHECK(outer.next()==&a);
		{
			auto inner=source.mutableWalk(); CHECK(inner.next()==&a);
			source.remove(b); destination.append(b);
			CHECK(inner.next()==nullptr); CHECK(outer.next()==nullptr);
		}
		CHECK(earlyDestination.next()==&c); CHECK(earlyDestination.next()==nullptr);
		// A later team phase includes the arrival.
		auto laterDestination=destination.mutableWalk();
		CHECK(laterDestination.next()==&c); CHECK(laterDestination.next()==&b);
		CHECK(laterDestination.next()==nullptr);
		CHECK((ids(destination)==std::vector<int>{2,1})); // nested read-only query
	}
	source.clear(); destination.clear();
}
TEST_CASE("unlink repairs cursors before heap entities are destroyed") {
	Population source,destination;
	auto current=std::make_unique<PopulationNode>();
	auto pending=std::make_unique<PopulationNode>();
	auto survivor=std::make_unique<PopulationNode>();
	auto tail=std::make_unique<PopulationNode>();
	auto arrival=std::make_unique<PopulationNode>();
	source.append(*current); source.append(*pending); source.append(*survivor); source.append(*tail);
	{
		auto outer=source.mutableWalk(); REQUIRE(outer.next()==current.get());
		auto nested=source.mutableWalk(); REQUIRE(nested.next()==current.get());
		source.remove(*current); current.reset();
		source.remove(*pending); pending.reset();
		source.append(*arrival);
		source.remove(*tail); tail.reset();
		source.remove(*survivor); destination.append(*survivor);
		CHECK(outer.next()==nullptr); CHECK(nested.next()==nullptr);
		auto destinationWalk=destination.mutableWalk();
		CHECK(destinationWalk.next()==survivor.get());
		destination.remove(*survivor); survivor.reset();
		CHECK(destinationWalk.next()==nullptr);
	}
	CHECK(source.front()==arrival.get());
	source.remove(*arrival); arrival.reset();
	CHECK(source.empty()); CHECK(destination.empty());
}
TEST_CASE("editor lifecycle and death preserve append order and repair scheduler") {
	glob2test::HeadlessGlobals globals;
	glob2test::HeadlessGame world({.teams=2,.discovered=true,.clearImmobile=true,.loadDefaultRace=true,.header=true});
	auto* team=world.game.teams[0];
	auto* a=world.addUnit(WORKER,8,12,0);
	auto* b=world.addUnit(WORKER,9,12,0);
	auto* c=world.addUnit(WORKER,10,12,0);
	REQUIRE(a); REQUIRE(b); REQUIRE(c);
	const auto bSlot=Unit::GIDtoID(b->gid);
	team->taskSwapNext=bSlot;
	REQUIRE(world.game.removeUnitAndBuildingAndFlags(9,12,unsigned(Game::DEL_GROUND_UNIT)));
	CHECK(team->taskSwapNext==Unit::GIDtoID(c->gid));
	auto* replacement=world.addUnit(WORKER,9,12,0); REQUIRE(replacement);
	CHECK(Unit::GIDtoID(replacement->gid)==bSlot);
	std::vector<Unit*> expected{a,c,replacement};
	CHECK(std::vector<Unit*>(team->liveUnits.begin(),team->liveUnits.end())==expected);
	team->createLists(); // capability rebuild must not reorder population
	CHECK(std::vector<Unit*>(team->liveUnits.begin(),team->liveUnits.end())==expected);
	// The same entities and IDs in a different update order must hash differently.
	const auto orderedChecksum=team->checkSum();
	const auto aSlot=Unit::GIDtoID(a->gid);
	team->attachUnit(aSlot,team->detachUnit(aSlot));
	CHECK(team->checkSum()!=orderedChecksum);
	c->isDead=true; team->syncStep();
	CHECK(team->liveUnits.size()==2);
	CHECK(team->liveUnits.matches(team->myUnits,Unit::MAX_COUNT));
	REQUIRE(world.addBuilding("inn",4,4,0,0));
	REQUIRE(world.game.removeUnitAndBuildingAndFlags(4,4,unsigned(Game::DEL_BUILDING)));
	CHECK(team->liveBuildings.empty());
	team->clearMem();
	CHECK(team->liveUnits.empty()); CHECK(team->taskSwapNext==0xffff);
}
}
