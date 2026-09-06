// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
//
// Standalone unit tests for GameMusicController. The controller is a pure
// state machine over GameMusicEvents with no SDL / Team / globalContainer
// dependencies, so this test links only the controller .cpp itself.

#include <cppunit/extensions/HelperMacros.h>

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

class GameMusicControllerTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(GameMusicControllerTest);
		CPPUNIT_TEST(testWarEventSetsWarTrackAndTimer);
		CPPUNIT_TEST(testGoodEventSetsBuildingTrackAndTimer);
		CPPUNIT_TEST(testTimerDecaysToDefaultTrack);
		CPPUNIT_TEST(testSimultaneousEventsLastWriterWins);
		CPPUNIT_TEST(testResetClearsTimers);
		CPPUNIT_TEST(testNoEventNoTrack);
		CPPUNIT_TEST(testWarEventOverridesExpiringBuildingTimer);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp(void) override {}
	void tearDown(void) override {}

protected:
	void testWarEventSetsWarTrackAndTimer()
	{
		GameMusicController c;
		auto track = c.tick(warEvent());
		CPPUNIT_ASSERT(track.has_value());
		CPPUNIT_ASSERT_EQUAL(MusicTrack::WarEvent, *track);
		// EVENT_TIMEOUT_TICKS gets set then decremented to 219 in the same tick.
		CPPUNIT_ASSERT_EQUAL(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getWarTimeoutTicks());
	}

	void testGoodEventSetsBuildingTrackAndTimer()
	{
		GameMusicController c;
		auto track = c.tick(goodEvent());
		CPPUNIT_ASSERT(track.has_value());
		CPPUNIT_ASSERT_EQUAL(MusicTrack::BuildingEvent, *track);
		CPPUNIT_ASSERT_EQUAL(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getBuildingTimeoutTicks());
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
			CPPUNIT_ASSERT(!t.has_value());
		}
		// Sanity: timer should read 1 at the start of the next tick.
		CPPUNIT_ASSERT_EQUAL(1u, c.getWarTimeoutTicks());
		auto track = c.tick(nothing());
		CPPUNIT_ASSERT(track.has_value());
		CPPUNIT_ASSERT_EQUAL(MusicTrack::InGameDefault, *track);
		CPPUNIT_ASSERT_EQUAL(0u, c.getWarTimeoutTicks());
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
		CPPUNIT_ASSERT(track.has_value());
		CPPUNIT_ASSERT_EQUAL(MusicTrack::BuildingEvent, *track);
		CPPUNIT_ASSERT_EQUAL(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getWarTimeoutTicks());
		CPPUNIT_ASSERT_EQUAL(GameMusicController::EVENT_TIMEOUT_TICKS - 1, c.getBuildingTimeoutTicks());
	}

	void testResetClearsTimers()
	{
		GameMusicController c;
		c.tick(warEvent());
		c.tick(goodEvent());
		CPPUNIT_ASSERT(c.getWarTimeoutTicks() > 0);
		CPPUNIT_ASSERT(c.getBuildingTimeoutTicks() > 0);
		c.reset();
		CPPUNIT_ASSERT_EQUAL(0u, c.getWarTimeoutTicks());
		CPPUNIT_ASSERT_EQUAL(0u, c.getBuildingTimeoutTicks());
		// A reset controller should behave identically to a fresh one — no
		// stale "timer == 1" transition on the very next tick.
		auto track = c.tick(nothing());
		CPPUNIT_ASSERT(!track.has_value());
	}

	void testNoEventNoTrack()
	{
		GameMusicController c;
		for (int i = 0; i < 50; ++i)
		{
			auto t = c.tick(nothing());
			CPPUNIT_ASSERT(!t.has_value());
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
		CPPUNIT_ASSERT_EQUAL(1u, c.getBuildingTimeoutTicks());
		// Now fire a war event on the same tick the building timer expires.
		auto track = c.tick(warEvent());
		CPPUNIT_ASSERT(track.has_value());
		CPPUNIT_ASSERT_EQUAL(MusicTrack::InGameDefault, *track);
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(GameMusicControllerTest);
