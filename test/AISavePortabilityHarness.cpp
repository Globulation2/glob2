// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include "GlobalContainer.h"
#include "Game.h"
#include "IntBuildingType.h"
#include "FileManager.h"
#include "Player.h"
#include "BinaryStream.h"
#include "StreamBackend.h"
#include "Version.h"
#include "Order.h"
#include "AIMaxima.h"
#include <boost/logic/tribool.hpp>
#include <boost/tuple/tuple.hpp>
#include <list>
#include <queue>
#include <sstream>
#include <tuple>
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

// Exercise the saved wire order and actual area executor, with asymmetric coordinates.
template<class Order>
struct AreaOrder : Order
{
	using Order::Order;
	using Order::save;
	using Order::load;
	using Order::modify;
};

template<class Order>
static void checkArea(AIEcho::Echo& echo, Game& game, bool adding)
{
	AreaOrder<Order> original(ClearingArea);
	original.add_location(3, 17);
	original.add_location(8, 29);
	auto* bytes = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream output(bytes);
	original.save(&output);
	const auto saved = bytes->takeContents();
	GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(), saved.size()));
	AreaOrder<Order> restored;
	require(restored.load(&input, game.players[0], VERSION_MINOR), "area load");
	auto* roundTrip = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream second(roundTrip);
	restored.save(&second);
	require(roundTrip->takeContents() == saved, "area coordinates survive save/load unchanged");
	restored.modify(echo);
	for (auto order : echo.orders)
	{
		order->sender = 0;
		game.executeOrder(order, 0);
	}
	echo.orders.clear();
	require(game.map.isClearArea(3, 17, 1) == adding &&
		game.map.isClearArea(8, 29, 1) == adding, "restored order changes intended tiles");
	require(!game.map.isClearArea(17, 3, 1) && !game.map.isClearArea(29, 8, 1),
		"restored order does not change transposed tiles");
}

int main(int argc, char** argv)
{
	require(argc == 3 && std::string(argv[1]).find("glob2-save-test-") == 0,
		"usage: AISavePortabilityHarness DISPOSABLE_PROFILE ROOT");
	GlobalContainer globals(argv[1]);
	globalContainer = &globals;
	globals.fileManager->addDir(argv[2]);
	globals.runNoX = true;
	globals.settings.rememberUnit = false;
	globals.buildingsTypes.init();
	IntBuildingType::init();
	{
		Game game(nullptr);
		game.map.setSize(6, 6, GRASS);
		game.map.setGame(&game);
		game.addTeam();
		game.teams[0]->race.loadDefault();
		game.players[0] = new Player(0, "portability", game.teams[0], BasePlayer::P_LOCAL);
		game.gameHeader.setNumberOfPlayers(1);
		AIEcho::Echo echo(new NewNicowar, game.players[0]);
		require(!echo.update_gm && echo.allies == 0 && echo.enemies == 0 &&
			echo.inn_view == 0 && echo.market_view == 0 && echo.other_view == 0,
			"Echo serialized fields initialized before first tick");
		checkArea<AIEcho::Management::AddArea>(echo, game, true);
		checkArea<AIEcho::Management::RemoveArea>(echo, game, false);

		AIMaxima::Maxima maxima(game.players[0]);
		auto stored = std::make_shared<AITelemetry::Series>();
		auto& series = *stored;
		series.implementation = 7;
		series.current.tick = 17;
		series.fields = maxima.telemetrySchema();
		series.current.values.resize(series.fields.size());
		maxima.telemetry.series = &series;
		maxima.telemetry.tick = 17;
		unsigned reserved = 0;
		for (unsigned i = 0; i < series.fields.size(); ++i)
			if (series.fields[i].name.starts_with("state.policy_bids_6."))
			{
				series.current.values[i] = {12345, 10, true};
				++reserved;
			}
		require(reserved == 20, "legacy telemetry columns retained");
		auto* bytes = new GAGCore::MemoryStreamBackend;
		GAGCore::BinaryOutputStream output(bytes);
		AITelemetry::save(&output, {stored});
		const auto saved = bytes->takeContents();
		GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(), saved.size()));
		std::vector<std::shared_ptr<AITelemetry::Series>> loaded;
		AITelemetry::load(&input, loaded);
		require(loaded.size() == 1 && loaded[0]->current == series.current,
			"legacy telemetry remains readable without changing stored samples");
		series = *loaded[0];
		maxima.captureTelemetry();
		for (unsigned i = 0; i < series.fields.size(); ++i)
		{
			if (series.fields[i].name.starts_with("state.policy_bids_6."))
				require(series.current.values[i] == AITelemetry::Value{}, "invalid policy sample cleared");
			if (series.fields[i].name == "state.policy_bids_5.utility")
				require(series.current.values[i].valid, "real sixth policy still sampled");
		}
	}
	globalContainer = nullptr;
	std::puts("PASS Echo initialization, AddArea/RemoveArea serialization, Maxima reserved telemetry");
}
