// SPDX-License-Identifier: GPL-3.0-or-later
// The LockstepSession contract as the engine loop drives it, exercised on
// NetEngine (the single-player and legacy implementation). Later
// implementations can reuse these sequences.

#include "Glob2Test.h"
#include "LockstepSession.h"
#include "NetEngine.h"
#include "Order.h"

#include <memory>

namespace
{
// One engine tick as Engine::gatherAndAdvanceOrders/executeOrdersAndStep run it
// for a client with no AI players; returns the executed order types.
std::vector<Uint8> engineTick(LockstepSession &session, int players, int local,
							  std::shared_ptr<Order> localOrder, Uint32 checksum, bool &ready)
{
	std::vector<Uint8> executed;
	if (ready)
	{
		session.setLocalPlayer(local);
		session.addLocalOrder(localOrder);
		session.advanceStep(checksum);
	}
	ready = session.tickReady();
	if (ready)
	{
		CHECK(session.matchCheckSums());
		for (int p = 0; p < players; ++p)
			executed.push_back(session.retrieveOrder(p)->getOrderType());
		session.clearTopOrders();
	}
	return executed;
}
} // namespace

TEST_SUITE("LockstepSession")
{
	TEST_CASE("single player executes the local order on the tick after it is submitted")
	{
		std::unique_ptr<LockstepSession> session = std::make_unique<NetEngine>(2, 0);
		bool ready = true;
		// Player 1 is an AI: the engine supplies its order whenever it is missing.
		session->setLocalPlayer(0);
		CHECK_FALSE(session->tickReady());
		CHECK_EQ(session->getWaitingOnMask(), 0x3u);
		CHECK_FALSE(session->orderReceived(1));
		session->pushOrder(std::make_shared<NullOrder>(), 1, true);
		CHECK(session->orderReceived(1));
		CHECK_EQ(session->getWaitingOnMask(), 0x1u);

		auto pause = std::make_shared<PauseGameOrder>(true);
		const auto executed = engineTick(*session, 2, 0, pause, 42, ready);
		REQUIRE(ready);
		CHECK_EQ(executed, std::vector<Uint8>{ORDER_PAUSE_GAME, ORDER_NULL});
		CHECK_EQ(pause->gameCheckSum, 42u);
		CHECK_EQ(pause->sender, 0);
		CHECK_EQ(session->getWaitingOnMask(), 0x3u);
	}

	TEST_CASE("null local orders are not queued and a tick waits for every player")
	{
		NetEngine engine(2, 1);
		LockstepSession &session = engine;
		bool ready = true;
		auto executed = engineTick(session, 2, 1, std::make_shared<NullOrder>(), 7, ready);
		// Player 0 has supplied nothing: the tick is not ready.
		CHECK_FALSE(ready);
		CHECK(executed.empty());
		CHECK_EQ(session.getWaitingOnMask(), 0x1u);
		session.pushOrder(std::make_shared<NullOrder>(), 0, true);
		CHECK(session.tickReady());
		CHECK_EQ(session.getWaitingOnMask(), 0x0u);
		CHECK_EQ(session.retrieveOrder(1)->gameCheckSum, 7u);
	}

	TEST_CASE("mismatched checksums are reported")
	{
		NetEngine engine(2, 0);
		LockstepSession &session = engine;
		auto remote = std::make_shared<NullOrder>();
		remote->gameCheckSum = 2;
		session.pushOrder(remote, 1, false);
		session.advanceStep(1);
		REQUIRE(session.tickReady());
		CHECK_FALSE(session.matchCheckSums());
	}

	TEST_CASE("flushAllOrders submits every queued local order without a checksum")
	{
		NetEngine engine(1, 0);
		LockstepSession &session = engine;
		session.addLocalOrder(std::make_shared<PauseGameOrder>(true));
		session.addLocalOrder(std::make_shared<PauseGameOrder>(false));
		session.flushAllOrders();
		for (int i = 0; i < 2; ++i)
		{
			REQUIRE(session.tickReady());
			CHECK_EQ(session.retrieveOrder(0)->getOrderType(), ORDER_PAUSE_GAME);
			CHECK_EQ(session.retrieveOrder(0)->gameCheckSum, ORDER_CHECKSUM_NONE);
			session.clearTopOrders();
		}
		CHECK_FALSE(session.tickReady());
	}
}
