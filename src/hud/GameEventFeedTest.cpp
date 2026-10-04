// SPDX-License-Identifier: GPL-3.0-or-later
// The coalesced game-event notifications: which reports share a row, the row
// cap, expiry on game time with a wall-clock floor, and GoToEvent's cycle.
// Time and map distance are explicit, so every case is exact.
#include "Glob2Test.h"

#include "GameEventFeed.h"

#include <cstdlib>

namespace
{
// A 128x128 torus.
std::int64_t torusDistanceSquared(int px, int py, int qx, int qy)
{
	auto axis = [](int a, int b)
	{
		const int d = std::abs(a - b) % 128;
		return std::min(d, 128 - d);
	};
	const std::int64_t dx = axis(px, qx), dy = axis(py, qy);
	return dx * dx + dy * dy;
}

GameEventFeedEvent event(GameEventType type, std::uint32_t subject, std::uint32_t tick, int x = 10, int y = 10)
{
	return {type, subject, tick, x, y, "message", GAGCore::Color()};
}
} // namespace

TEST_SUITE("GameEventFeed")
{
	TEST_CASE("reports of one situation share a row that keeps its place")
	{
		GameEventFeed feed;
		feed.ingest(event(GEUnitUnderAttack, 0, 100, 10, 10), 1000, torusDistanceSquared);
		feed.ingest(event(GEBuildingCompleted, 2, 110), 1100, torusDistanceSquared);
		feed.ingest(event(GEUnitUnderAttack, 0, 150, 14, 12), 1200, torusDistanceSquared);
		feed.ingest(event(GEUnitUnderAttack, 0, 200, 15, 11), 1300, torusDistanceSquared);

		REQUIRE(feed.rows().size() == 2);
		// The completion arrived later and stays on top; the attack row was
		// updated in place below it.
		CHECK(feed.rows()[0].type == GEBuildingCompleted);
		const GameEventFeed::Row &attack = feed.rows()[1];
		CHECK(attack.type == GEUnitUnderAttack);
		CHECK(attack.count == 3);
		CHECK(attack.x == 15);
		CHECK(attack.y == 11);
		CHECK(attack.lastTick == 200);
		CHECK(attack.lastWallMs == 1300);
	}

	TEST_CASE("different subjects, areas and teams are different situations")
	{
		GameEventFeed feed;
		feed.ingest(event(GEUnitUnderAttack, 0, 1, 10, 10), 0, torusDistanceSquared);
		feed.ingest(event(GEUnitUnderAttack, 2, 1, 10, 10), 0, torusDistanceSquared);  // warriors
		feed.ingest(event(GEUnitUnderAttack, 0, 1, 60, 60), 0, torusDistanceSquared);  // another front
		feed.ingest(event(GEBuildingUnderAttack, 0, 1, 10, 10), 0, torusDistanceSquared);
		feed.ingest(event(GEUnitLostConversion, 1, 1), 0, torusDistanceSquared);
		feed.ingest(event(GEUnitLostConversion, 2, 1), 0, torusDistanceSquared);
		feed.ingest(event(GEUnitGainedConversion, 1, 1), 0, torusDistanceSquared);
		CHECK(feed.rows().size() == 7);

		// Conversions and completions ignore position.
		feed.ingest(event(GEUnitLostConversion, 1, 2, 100, 100), 0, torusDistanceSquared);
		CHECK(feed.rows().size() == 7);
	}

	TEST_CASE("attack areas are measured across the map edge")
	{
		GameEventFeed feed;
		feed.ingest(event(GEBuildingUnderAttack, 4, 1, 2, 2), 0, torusDistanceSquared);
		feed.ingest(event(GEBuildingUnderAttack, 4, 2, 125, 127), 0, torusDistanceSquared);
		REQUIRE(feed.rows().size() == 1);
		CHECK(feed.rows()[0].count == 2);
		// Just outside the radius is a separate attack.
		feed.ingest(event(GEBuildingUnderAttack, 4, 3, 125 - GameEventFeed::kRegionRadius - 1, 127),
					0, torusDistanceSquared);
		CHECK(feed.rows().size() == 2);
	}

	TEST_CASE("at most kMaxRows rows; a new situation evicts the least recently updated")
	{
		GameEventFeed feed;
		for (std::uint32_t team = 0; team < GameEventFeed::kMaxRows; ++team)
			feed.ingest(event(GEUnitGainedConversion, team, team), team, torusDistanceSquared);
		// Team 0's row is the oldest, but a fresh report makes team 1's the stalest.
		feed.ingest(event(GEUnitGainedConversion, 0, 20), 20, torusDistanceSquared);
		feed.ingest(event(GEUnitLostConversion, 0, 21), 21, torusDistanceSquared);

		REQUIRE(feed.rows().size() == GameEventFeed::kMaxRows);
		CHECK(feed.rows()[0].type == GEUnitLostConversion);
		bool sawTeam0 = false, sawTeam1 = false;
		for (const auto &row : feed.rows())
			if (row.type == GEUnitGainedConversion)
			{
				sawTeam0 |= row.subject == 0;
				sawTeam1 |= row.subject == 1;
			}
		CHECK(sawTeam0);
		CHECK_FALSE(sawTeam1);
	}

	TEST_CASE("expiry, normal speed: game time decides")
	{
		const std::uint32_t linger = GameEventFeed::kAttackLingerTicks;
		GameEventFeed feed;
		feed.ingest(event(GEUnitUnderAttack, 0, 1000), 10000, torusDistanceSquared);
		// 25 ticks per second: the linger runs out after linger * 40 ms.
		const std::uint64_t lapsed = 10000 + linger * 40 + 40;
		feed.expire(1000 + linger, lapsed - 80);
		CHECK(feed.rows().size() == 1);
		feed.expire(1000 + linger + 1, lapsed);
		REQUIRE(feed.rows().size() == 1);
		CHECK(GameEventFeed::opacity(feed.rows()[0], lapsed) == 1.f);
		CHECK(GameEventFeed::opacity(feed.rows()[0], lapsed + GameEventFeed::kFadeMs / 2) ==
			  doctest::Approx(0.5f));
		feed.expire(1000 + linger + 50, lapsed + GameEventFeed::kFadeMs);
		CHECK(feed.rows().empty());
	}

	TEST_CASE("expiry, maximum speed: the wall-clock floor keeps the row readable")
	{
		const std::uint32_t linger = GameEventFeed::kAttackLingerTicks;
		GameEventFeed feed;
		feed.ingest(event(GEUnitUnderAttack, 0, 1000), 10000, torusDistanceSquared);
		// Hundreds of ticks pass within a few frames.
		feed.expire(1000 + linger * 4, 10200);
		REQUIRE(feed.rows().size() == 1);
		feed.expire(1000 + linger * 40, 10000 + GameEventFeed::kMinVisibleMs);
		REQUIRE(feed.rows().size() == 1);
		CHECK(GameEventFeed::opacity(feed.rows()[0], 10000 + GameEventFeed::kMinVisibleMs) == 1.f);
		feed.expire(1000 + linger * 60, 10000 + GameEventFeed::kMinVisibleMs + GameEventFeed::kFadeMs);
		CHECK(feed.rows().empty());
	}

	TEST_CASE("expiry, paused: wall-clock time alone never expires a row")
	{
		GameEventFeed feed;
		feed.ingest(event(GEUnitUnderAttack, 0, 1000), 10000, torusDistanceSquared);
		feed.expire(1000, 10000 + 600000);
		CHECK(feed.rows().size() == 1);
	}

	TEST_CASE("expiry, an ongoing situation stays up")
	{
		const std::uint32_t linger = GameEventFeed::kAttackLingerTicks;
		GameEventFeed feed;
		std::uint64_t now = 10000;
		for (std::uint32_t tick = 1000; tick < 1000 + linger * 20; tick += 50, now += 100)
		{
			feed.ingest(event(GEUnitUnderAttack, 0, tick), now, torusDistanceSquared);
			feed.expire(tick + 10, now + 50);
			REQUIRE(feed.rows().size() == 1);
		}
	}

	TEST_CASE("expiry, a fresh report revives a fading row")
	{
		GameEventFeed feed;
		feed.ingest(event(GEBuildingCompleted, 3, 1000), 10000, torusDistanceSquared);
		feed.expire(5000, 12000);
		const std::uint64_t fading = 10000 + GameEventFeed::kMinVisibleMs + GameEventFeed::kFadeMs / 2;
		feed.expire(5000, fading);
		REQUIRE(feed.rows().size() == 1);
		CHECK(GameEventFeed::opacity(feed.rows()[0], fading) < 1.f);
		feed.ingest(event(GEBuildingCompleted, 3, 5000), fading, torusDistanceSquared);
		CHECK(GameEventFeed::opacity(feed.rows()[0], fading) == 1.f);
		CHECK(feed.rows()[0].count == 2);
	}

	TEST_CASE("linger times per event kind")
	{
		CHECK(GameEventFeed::lingerTicks(GEBuildingCompleted) == GameEventFeed::kCompletedLingerTicks);
		CHECK(GameEventFeed::lingerTicks(GEUnitGainedConversion) == GameEventFeed::kConversionLingerTicks);
	}

	TEST_CASE("GoToEvent visits the most recent row, then steps through the rest")
	{
		GameEventFeed feed;
		CHECK(feed.nextJumpTarget(0) == nullptr);

		feed.ingest(event(GEUnitUnderAttack, 0, 1, 10, 10), 0, torusDistanceSquared);   // A
		feed.ingest(event(GEBuildingCompleted, 1, 2, 40, 40), 0, torusDistanceSquared); // B
		feed.ingest(event(GEUnitLostConversion, 1, 3, 70, 70), 0, torusDistanceSquared); // C
		feed.ingest(event(GEUnitUnderAttack, 0, 4, 12, 12), 0, torusDistanceSquared);  // A again

		// By recency: A, C, B, then wrapping round.
		const std::uint64_t t = 50000;
		CHECK(feed.nextJumpTarget(t)->x == 12);
		CHECK(feed.nextJumpTarget(t + 500)->x == 70);
		CHECK(feed.nextJumpTarget(t + 1000)->x == 40);
		CHECK(feed.nextJumpTarget(t + 1500)->x == 12);

		// Reports between presses do not reorder an ongoing cycle.
		feed.ingest(event(GEBuildingCompleted, 1, 5, 41, 41), t + 1600, torusDistanceSquared);
		CHECK(feed.nextJumpTarget(t + 2000)->x == 70);

		// After a pause the cycle restarts from the most recent row, B.
		CHECK(feed.nextJumpTarget(t + 2000 + GameEventFeed::kJumpCycleMs)->x == 41);
	}

	TEST_CASE("clear forgets rows and records the team they belong to")
	{
		GameEventFeed feed;
		CHECK(feed.team() == -1);
		feed.ingest(event(GEUnitUnderAttack, 0, 1), 0, torusDistanceSquared);
		feed.clear(3);
		CHECK(feed.rows().empty());
		CHECK(feed.team() == 3);
		CHECK(feed.nextJumpTarget(0) == nullptr);
	}
}
