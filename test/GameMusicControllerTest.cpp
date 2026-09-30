// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
//
// Standalone unit tests for GameMusicController. The controller is a pure
// state machine over GameMusicEvents with no SDL / Team / globalContainer
// dependencies, so this test links only the controller .cpp itself.

#include "Glob2Test.h"

#include "../src/gui/GameMusicController.h"

namespace
{
	GameMusicEvents warEvent()
	{
		GameMusicEvents e;
		e.unitUnderAttack = true;
		return e;
	}

	GameMusicEvents goodEvent()
	{
		GameMusicEvents e;
		e.buildingCompleted = true;
		return e;
	}

	GameMusicEvents nothing() { return GameMusicEvents{}; }
}

class GameMusicControllerTest
{

public:
	GameMusicControllerTest() {}
	~GameMusicControllerTest() {}

protected:
	void testWarEventSetsWarTrackAndTimer()
	{
		GameMusicController c;
		auto track = c.tick(warEvent());
		CHECK(track.has_value());
		CHECK_EQ(MusicTrack::WarEvent, *track);
		// EVENT_TIMEOUT_TICKS gets set then decremented to 219 in the same tick.
		CHECK_EQ(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getWarTimeoutTicks());
	}

	void testGoodEventSetsBuildingTrackAndTimer()
	{
		GameMusicController c;
		auto track = c.tick(goodEvent());
		CHECK(track.has_value());
		CHECK_EQ(MusicTrack::BuildingEvent, *track);
		CHECK_EQ(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getBuildingTimeoutTicks());
	}

	void testTimerDecaysToDefaultTrack()
	{
		GameMusicController c;
		c.tick(warEvent());
		// Drain to the tick where warTimeoutTicks == 1 at the top of tick().
		// After the war event tick, war timer is EVENT_TIMEOUT_TICKS - 1.
		// We need it to read 1 at the start of a tick, so we need
		// (EVENT_TIMEOUT_TICKS - 1) - 1 more empty ticks to bring it to 1
		// at the start of the *next* tick.
		const unsigned ticksUntilOne = GameMusicController::EVENT_TIMEOUT_TICKS - 2;
		for (unsigned i = 0; i < ticksUntilOne; ++i)
		{
			auto t = c.tick(nothing());
			CHECK(!t.has_value());
		}
		// Sanity: timer should read 1 at the start of the next tick.
		CHECK_EQ(1u, c.getWarTimeoutTicks());
		auto track = c.tick(nothing());
		CHECK(track.has_value());
		CHECK_EQ(MusicTrack::InGameDefault, *track);
		CHECK_EQ(0u, c.getWarTimeoutTicks());
	}

	void testSimultaneousEventsLastWriterWins()
	{
		GameMusicController c;
		GameMusicEvents both;
		both.unitUnderAttack = true;
		both.buildingCompleted = true;
		auto track = c.tick(both);
		// Original musicStep does the good-event branch second; that's the
		// last setNextTrack call before the timeout check, so building wins
		// when both fire and neither timer is at 1.
		CHECK(track.has_value());
		CHECK_EQ(MusicTrack::BuildingEvent, *track);
		CHECK_EQ(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getWarTimeoutTicks());
		CHECK_EQ(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getBuildingTimeoutTicks());
	}

	void testResetClearsTimers()
	{
		GameMusicController c;
		c.tick(warEvent());
		c.tick(goodEvent());
		CHECK(c.getWarTimeoutTicks() > 0);
		CHECK(c.getBuildingTimeoutTicks() > 0);
		c.reset();
		CHECK_EQ(0u, c.getWarTimeoutTicks());
		CHECK_EQ(0u, c.getBuildingTimeoutTicks());
		// A reset controller should behave identically to a fresh one — no
		// stale "timer == 1" transition on the very next tick.
		auto track = c.tick(nothing());
		CHECK(!track.has_value());
	}

	void testNoEventNoTrack()
	{
		GameMusicController c;
		for (int i = 0; i < 50; ++i)
		{
			auto t = c.tick(nothing());
			CHECK(!t.has_value());
		}
	}

	void testWarEventOverridesExpiringBuildingTimer()
	{
		// Original musicStep behavior: when an event fires AND the OTHER
		// timer hits 1 on the same tick, the InGameDefault branch runs
		// last and wins. Verify the controller preserves that ordering.
		GameMusicController c;
		c.tick(goodEvent());
		// Drain building timer to read 1 at the start of the next tick.
		for (unsigned i = 0; i < GameMusicController::EVENT_TIMEOUT_TICKS - 2; ++i)
			c.tick(nothing());
		CHECK_EQ(1u, c.getBuildingTimeoutTicks());
		// Now fire a war event on the same tick the building timer expires.
		auto track = c.tick(warEvent());
		CHECK(track.has_value());
		CHECK_EQ(MusicTrack::InGameDefault, *track);
	}
};
TEST_SUITE("GameMusicController")
{
	TEST_CASE_FIXTURE(GameMusicControllerTest, "WarEventSetsWarTrackAndTimer") { testWarEventSetsWarTrackAndTimer(); }
	TEST_CASE_FIXTURE(GameMusicControllerTest, "GoodEventSetsBuildingTrackAndTimer") { testGoodEventSetsBuildingTrackAndTimer(); }
	TEST_CASE_FIXTURE(GameMusicControllerTest, "TimerDecaysToDefaultTrack") { testTimerDecaysToDefaultTrack(); }
	TEST_CASE_FIXTURE(GameMusicControllerTest, "SimultaneousEventsLastWriterWins") { testSimultaneousEventsLastWriterWins(); }
	TEST_CASE_FIXTURE(GameMusicControllerTest, "ResetClearsTimers") { testResetClearsTimers(); }
	TEST_CASE_FIXTURE(GameMusicControllerTest, "NoEventNoTrack") { testNoEventNoTrack(); }
	TEST_CASE_FIXTURE(GameMusicControllerTest, "WarEventOverridesExpiringBuildingTimer") { testWarEventOverridesExpiringBuildingTimer(); }
}
