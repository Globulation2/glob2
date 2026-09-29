// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise Nicowar's farming scan through the real area-order executors.
#define SDL_MAIN_HANDLED
#include "GlobalContainer.h"
#include "Game.h"
#include "Building.h"
#include "IntBuildingType.h"
#include "FileManager.h"
#include "Player.h"
#include <boost/logic/tribool.hpp>
#include <boost/tuple/tuple.hpp>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>
#define private public
#include "AINicowar.h"
#undef private
#include <cstdio>
#include <cstdlib>

GlobalContainer* globalContainer = nullptr;

static void require(bool ok, const char* message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

struct Fixture
{
	Game game{nullptr};
	std::unique_ptr<AIEcho::Echo> echo;
	Fixture()
	{
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam();
		game.teams[0]->race.loadDefault();
		game.players[0] = new Player(0, "farming", game.teams[0], BasePlayer::P_LOCAL);
		game.gameHeader.setNumberOfPlayers(1);
		for (int y=0; y<64; ++y)
			for (int x=0; x<64; ++x)
				if (x != 50 || y != 50) game.map.setMapDiscovered(x, y, game.teams[0]->me);
		echo.reset(new AIEcho::Echo(new NewNicowar, game.players[0]));
		echo->gm.reset(new AIEcho::Gradients::GradientManager(&game.map));
	}
	void update()
	{
		static_cast<NewNicowar*>(echo->echoai.get())->update_farming(*echo);
		echo->update_management_orders();
		for (auto order : echo->orders)
		{
			order->sender = 0;
			game.executeOrder(order, 0);
		}
		echo->orders.clear();
	}
	bool clearing(int x, int y) { return game.map.isClearArea(x, y, game.teams[0]->me); }
};

int main(int argc, char** argv)
{
	require(argc == 3 && std::string(argv[1]).find("glob2-save-test-") == 0,
		"usage: NicowarFarmingHarness DISPOSABLE_PROFILE ROOT");
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.fileManager->addDir(argv[2]);
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	{
		Fixture f;
		auto& map = f.game.map;
		for (int y=0; y<64; ++y) map.getTile(0,y).terrain = 256;
		for (int x : {5, 6, 7, 9, 10}) map.setResource(x, 9, WOOD, 0);
		map.setResource(4, 9, WHEAT, 0);
		map.setResource(5, 20, WOOD, 0);
		map.addForbidden(7, 9, 0);
		f.update();
		require(f.clearing(5,9), "wheat adjacency overrides wood zone");
		require(!f.clearing(5,20), "wood zone survives away from wheat");
		require(f.clearing(6,9) && f.clearing(7,9) && f.clearing(9,9), "wood cleared throughout wheat-only band");
		require(!f.clearing(10,9), "unrelated wood beyond wheat zone survives");
		require(!map.isForbidden(7,9,1), "clearing target loses farming protection");
		map.setResource(7,9,WHEAT,0);
		map.setResource(5,9,WHEAT,0);
		map.setNoResource(9,9,0);
		f.update();
		require(!f.clearing(5,9) && map.isForbidden(5,9,1), "replacement wheat protected inside wood zone");
		require(!f.clearing(7,9) && !f.clearing(9,9), "cleared tiles released for wheat");
		require(map.isForbidden(7,9,1), "replacement wheat protected in same scan");
	}
	{
		Fixture f;
		auto& map = f.game.map;
		const int directions[][2] = {{-1,0}, {1,0}, {0,-1}, {0,1}, {-1,-1}, {-1,1}, {1,-1}, {1,1}};
		for (int i=0; i<8; ++i)
		{
			map.setResource(20,6+7*i,WOOD,0);
			map.setResource(20+directions[i][0],6+7*i+directions[i][1],WHEAT,0);
		}
		map.setResource(50,50,WOOD,0); map.setResource(51,50,WHEAT,0);
		map.setResource(40,40,WOOD,0); map.setResource(41,41,WHEAT,0);
		map.setResource(63,20,WOOD,0); map.setResource(0,20,WHEAT,0);
		map.setResource(20,63,WOOD,0); map.setResource(20,0,WHEAT,0);
		map.setResource(63,63,WOOD,0); map.setResource(0,0,WHEAT,0);
		f.update();
		for (int i=0; i<8; ++i) require(f.clearing(20,6+7*i), "eight-way wheat adjacency");
		require(!f.clearing(50,50), "undiscovered wood is not targeted");
		require(f.clearing(40,40), "diagonal wheat adjacency");
		require(f.clearing(63,20) && f.clearing(20,63), "adjacency wraps both map seams");
		require(f.clearing(63,63), "diagonal adjacency wraps map corner");
		map.setNoResource(63,20,0); map.setNoResource(0,20,0);
		const int inn = globals.buildingsTypes.getTypeNum("inn",0,false);
		require(f.game.addBuilding(0,30,inn,0) != nullptr, "create building beside wrap seam");
		map.addClearArea(63,29,0);
		f.update();
		require(!f.clearing(63,20), "cleanup survives loss of neighboring wheat");
		require(f.clearing(63,29), "preserve diagonal building clearance across seam");
	}
	std::puts("PASS: Nicowar zone boundaries, eight-way adjacency, wrap seams, discovery, protection, cleanup and building clearance");
}
