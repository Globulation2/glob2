// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise Nicowar's farming scan through the real area-order executors.
#include "EngineFixtures.h"
#include "GlobalContainer.h"
#include "Game.h"
#include "Building.h"
#include "shared_runtime/BuildingDemands.h"
#include "FileManager.h"
#include "Player.h"
#include "../src/ai/shared_runtime/Tribool.h"
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>
#include "AINicowar.h"
#include "ExperimentalFeatures.h"
#include <cstdio>
#include <cstdlib>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
}

struct Fixture
{
	Game game{nullptr};
	std::unique_ptr<AISharedRuntime::Runtime> runtime;
	explicit Fixture(bool farmAreas = false)
	{
		if (farmAreas)
			game.gameHeader.getExperiments().set(ExperimentId::FarmAreas);
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam();
		game.teams[0]->race.loadDefault();
		game.players[0] = new Player(0, "farming", game.teams[0], BasePlayer::P_LOCAL);
		game.gameHeader.setNumberOfPlayers(1);
		for (int y=0; y<64; ++y)
			for (int x=0; x<64; ++x)
				if (x != 50 || y != 50) game.map.setMapDiscovered(x, y, game.teams[0]->me);
		runtime.reset(new AISharedRuntime::Runtime(new NewNicowar, game.players[0]));
		runtime->gm.reset(new AISharedRuntime::Gradients::GradientManager(&game.map));
	}
	void update()
	{
        AISharedRuntime::Runtime::OwnerObservationScope observation(*runtime);
		static_cast<NewNicowar*>(runtime->runtimeai.get())->update_farming(*runtime);
		runtime->update_management_orders();
		for (auto order : runtime->orders)
		{
			order->sender = 0;
			game.executeOrder(order, 0);
		}
		runtime->orders.clear();
	}
	bool clearing(int x, int y) { return game.map.isClearArea(x, y, game.teams[0]->me); }
};
}

TEST_SUITE("NicowarFarming")
{
	TEST_CASE("zone boundaries; eight-way adjacency; wrap seams; discovery; protection and building clearance")
	{
		glob2test::HeadlessGlobals globals;
		{
			Fixture dry;
			auto& map=dry.game.map;
			map.setResourceByIndex(20,20,WOOD,0);
			map.setResourceByIndex(21,20,WHEAT,0);
			require(map.materialExpansionRateAt(map.coordToIndex(20,20),MaterialId::Wood)==0,
				"dry fixture cannot propagate wood");
			dry.update();
			require(!dry.clearing(20,20),"nonspreading wood beside food remains useful stock");
		}
		{
			Fixture f;
			auto& map = f.game.map;
			for (int y=0; y<64; ++y) map.setCellTerrain(0,y,WATER);
			for (int x : {5, 6, 7, 9, 10}) map.setResourceByIndex(x, 9, WOOD, 0);
			map.setResourceByIndex(4, 9, WHEAT, 0);
			map.setResourceByIndex(5, 20, WOOD, 0);
			map.addForbidden(7, 9, 0);
			f.update();
			require(f.clearing(5,9), "wheat adjacency overrides wood zone");
			require(!f.clearing(5,20), "wood zone survives away from wheat");
			require(f.clearing(6,9) && f.clearing(7,9) && f.clearing(9,9), "wood cleared throughout wheat-only band");
			require(!f.clearing(10,9), "unrelated wood beyond wheat zone survives");
			require(!map.isForbidden(7,9,1), "clearing target loses farming protection");
			map.setResourceByIndex(7,9,WHEAT,0);
			map.setResourceByIndex(5,9,WHEAT,0);
			map.setNoResource(9,9,0);
			f.update();
			require(!f.clearing(5,9) && map.isForbidden(5,9,1), "replacement wheat protected inside wood zone");
			require(!f.clearing(7,9) && !f.clearing(9,9), "cleared tiles released for wheat");
			require(map.isForbidden(7,9,1), "replacement wheat protected in same scan");
		}
		{
			Fixture f;
			auto& map = f.game.map;
			// This block tests adjacency of actual spreading threats. The old
			// all-grass fixture had zero ecological growth everywhere.
			for(int x:{18,38,48,61}) for(int y=0;y<64;++y)
				map.setCellTerrain(x,y,WATER);
			const int directions[][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}, {-1,-1}, {-1,1}, {1,-1}, {1,1}};
			for (int i=0; i<8; ++i)
			{
				map.setResourceByIndex(20,6+7*i,WOOD,0);
				map.setResourceByIndex(20+directions[i][0],6+7*i+directions[i][1],WHEAT,0);
				require(map.materialExpansionRateAt(map.coordToIndex(20,6+7*i),MaterialId::Wood)>0,
					"adjacency fixture has an active wood propagation threat");
			}
			map.setResourceByIndex(50,50,WOOD,0); map.setResourceByIndex(51,50,WHEAT,0);
			map.setResourceByIndex(40,40,WOOD,0); map.setResourceByIndex(41,41,WHEAT,0);
			map.setResourceByIndex(63,20,WOOD,0); map.setResourceByIndex(0,20,WHEAT,0);
			map.setResourceByIndex(20,63,WOOD,0); map.setResourceByIndex(20,0,WHEAT,0);
			map.setResourceByIndex(63,63,WOOD,0); map.setResourceByIndex(0,0,WHEAT,0);
			f.update();
			for (int i=0; i<8; ++i) require(f.clearing(20,6+7*i), "eight-way wheat adjacency");
			require(!f.clearing(50,50), "undiscovered wood is not targeted");
			require(f.clearing(40,40), "diagonal wheat adjacency");
			require(f.clearing(63,20) && f.clearing(20,63), "adjacency wraps both map seams");
			require(f.clearing(63,63), "diagonal adjacency wraps map corner");
			map.setNoResource(63,20,0); map.setNoResource(0,20,0);
			const int inn = globals->buildingsTypes.getTypeNum("inn",0,false);
			require(f.game.addBuilding(0,30,inn,0) != nullptr, "create building beside wrap seam");
			map.addClearArea(63,29,0);
			f.update();
			require(!f.clearing(63,20), "cleanup survives loss of neighboring wheat");
			require(f.clearing(63,29), "preserve diagonal building clearance across seam");
		}
	}

	TEST_CASE("farm areas reuse wheat protection while wood keeps its existing forbidden and clearing rules")
	{
		glob2test::HeadlessGlobals globals;
		Fixture f(true), baseline;
		auto setup=[](Map& map) {
			for (int y=0; y<64; ++y) for (int x=0; x<6; ++x) map.setUMatPos(x, y, WATER, 1);
			for (int y=10; y<14; ++y) for (int x=9; x<13; ++x) map.setResourceByIndex(x, y, WHEAT, 0);
			map.setResourceByIndex(11, 11, WOOD, 0);
			map.addForbidden(9, 11, 0);
			for (int y=30; y<34; ++y) for (int x=9; x<13; ++x) map.setResourceByIndex(x, y, WOOD, 0);
		};
		auto& map=f.game.map;
		setup(map); setup(baseline.game.map);
		// Loaded prototype paint covered both parities and a surrounding ring.
		for (int y=9; y<15; ++y) for (int x=8; x<14; ++x) map.addFarmArea(x,y,0);
		f.update(); baseline.update();
		const Uint32 me=f.game.teams[0]->me;
		for (int y=9; y<15; ++y) for (int x=8; x<14; ++x)
		{
			CAPTURE(x); CAPTURE(y);
			const bool wheat=map.getResource(x,y).type==WHEAT;
			require(map.isFarmArea(x,y,me)==(wheat && baseline.game.map.isForbidden(x,y,me)),
				"farm paint matches the original wheat protection pattern");
			if(wheat) require(!map.isForbidden(x,y,me), "old wheat forbidden paint is removed");
		}
		require(f.clearing(11,11), "wood beside wheat keeps its clearing area");
		require(map.isForbidden(9,31,me), "wood keeps its forbidden spots");
		require(!map.isFarmArea(9,31,me), "wood is not farmed");
		for (int y=8; y<16; ++y) for (int x=7; x<15; ++x) map.setNoResource(x,y,0);
		f.update();
		require(!map.isFarmArea(9,11,me), "an emptied field releases its farm");
	}
}
