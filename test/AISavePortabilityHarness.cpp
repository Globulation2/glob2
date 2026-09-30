// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineFixtures.h"
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
#include "AICastor.h"
#include <algorithm>
#include <new>
#include "../src/ai/shared_runtime/Tribool.h"
#include <list>
#include <queue>
#include <sstream>
#include <tuple>
#include "AINicowar.h"
#include <cstdio>
#include <cstdlib>

namespace
{
static void require(bool ok, const char* message)
{
	GLOB2_REQUIRE(ok, message);
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
static void checkArea(AISharedRuntime::Runtime& runtime, Game& game, bool adding)
{
	AreaOrder<Order> original(ClearingArea);
	original.add_location(3, 17);
	original.add_location(8, 29);
	auto* bytes = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream output(bytes);
	original.save(&output);
	const auto saved = bytes->takeContents();
	GAGCore::BinaryInputStream input(new GAGCore::MemoryStreamBackend(saved.data(), saved.size()));
	input.seekFromStart(0);
	AreaOrder<Order> restored;
	require(restored.load(&input, game.players[0], VERSION_MINOR), "area load");
	auto* roundTrip = new GAGCore::MemoryStreamBackend;
	GAGCore::BinaryOutputStream second(roundTrip);
	restored.save(&second);
	require(roundTrip->takeContents() == saved, "area coordinates survive save/load unchanged");
	restored.modify(runtime);
	for (auto order : runtime.orders)
	{
		order->sender = 0;
		game.executeOrder(order, 0);
	}
	runtime.orders.clear();
	require(game.map.isClearArea(3, 17, 1) == adding &&
		game.map.isClearArea(8, 29, 1) == adding, "restored order changes intended tiles");
	require(!game.map.isClearArea(17, 3, 1) && !game.map.isClearArea(29, 8, 1),
		"restored order does not change transposed tiles");
}
}

TEST_SUITE("AISavePortability")
{
	TEST_CASE("runtime initialisation; area order serialisation and Maxima reserved telemetry")
	{
		glob2test::HeadlessGlobals globals;
		{
			Game game(nullptr);
			game.map.setSize(6, 6, GRASS);
			game.map.setGame(&game);
			game.addTeam();
			game.teams[0]->race.loadDefault();
			game.players[0] = new Player(0, "portability", game.teams[0], BasePlayer::P_LOCAL);
			game.gameHeader.setNumberOfPlayers(1);
			// A captured pre-boot Castor cache must not depend on allocator contents.
			alignas(AICastor) unsigned char castorStorage[sizeof(AICastor)];
			std::fill(std::begin(castorStorage), std::end(castorStorage), 0xa5);
			auto *castor = new (castorStorage) AICastor(game.players[0]);
			AITelemetry::Series castorSeries;
			castorSeries.fields = AITelemetry::schema(2);
			castorSeries.current.values.resize(castorSeries.fields.size());
			castor->telemetry.series = &castorSeries;
			castor->captureTelemetry();
			unsigned buildingCounts = 0;
			for (size_t i = 0; i < castorSeries.fields.size(); ++i)
				if (castorSeries.fields[i].name.starts_with("state.buildingSum_"))
				{
					++buildingCounts;
					require(castorSeries.current.values[i].bits == 0, "Castor pre-boot counts initialized");
				}
			require(buildingCounts > 0, "Castor building count columns checked");
			castor->~AICastor();
			AISharedRuntime::Runtime runtime(new NewNicowar, game.players[0]);
			require(!runtime.gm && runtime.allies == 0 && runtime.enemies == 0 &&
				runtime.inn_view == 0 && runtime.market_view == 0 && runtime.other_view == 0,
				"Runtime serialized fields initialized before first tick");
			checkArea<AISharedRuntime::Management::AddArea>(runtime, game, true);
			checkArea<AISharedRuntime::Management::RemoveArea>(runtime, game, false);

			AIMaxima::Maxima maxima(game.players[0]);
			maxima.getOrder(); // Initialize the director before sampling its policy state.
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
			input.seekFromStart(0);
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
	}
}
