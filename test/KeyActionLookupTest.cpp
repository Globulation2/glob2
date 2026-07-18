// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <cppunit/extensions/HelperMacros.h>

#include <optional>
#include <string>

#include "GameGUIKeyActions.h"
#include "MapEditKeyActions.h"

// Regression tests for GameGUIKeyActions::getAction / MapEditKeyActions::getAction.
// The reverse lookup used std::map::operator[], so an unknown action name from a
// user's keyboard-gui.txt / keyboard-mapedit.txt silently returned 0 (DoNothing)
// AND inserted the bogus token into the static map (a slow leak, one entry per bad
// token per session). getAction now returns std::optional<Uint32>: a known name
// resolves to its id, an unknown name yields std::nullopt and does not grow the map.
// getName is bounds-checked so an out-of-range id yields an empty string instead of
// indexing past the names vector.
class KeyActionLookupTest : public CPPUNIT_NS::TestCase
{
	CPPUNIT_TEST_SUITE(KeyActionLookupTest);
		CPPUNIT_TEST(testGuiKnownNameResolves);
		CPPUNIT_TEST(testGuiDoNothingNameResolves);
		CPPUNIT_TEST(testGuiUnknownNameIsNullopt);
		CPPUNIT_TEST(testGuiUnknownNameDoesNotGrowMap);
		CPPUNIT_TEST(testGuiRoundTripAllActions);
		CPPUNIT_TEST(testGuiGetNameOutOfRangeIsEmpty);
		CPPUNIT_TEST(testMapEditKnownNameResolves);
		CPPUNIT_TEST(testMapEditUnknownNameIsNullopt);
		CPPUNIT_TEST(testMapEditUnknownNameDoesNotGrowMap);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp(void) override
	{
		GameGUIKeyActions::init();
		MapEditKeyActions::init();
	}

protected:
	void testGuiKnownNameResolves(void)
	{
		std::optional<Uint32> a = GameGUIKeyActions::getAction("pause game");
		CPPUNIT_ASSERT(a.has_value());
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(GameGUIKeyActions::PauseGame), *a);
	}

	void testGuiDoNothingNameResolves(void)
	{
		// The literal "do nothing" binding is intentional and must keep working
		// (it must be distinguishable from an unknown token, not conflated).
		std::optional<Uint32> a = GameGUIKeyActions::getAction("do nothing");
		CPPUNIT_ASSERT(a.has_value());
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(GameGUIKeyActions::DoNothing), *a);
	}

	void testGuiUnknownNameIsNullopt(void)
	{
		// A plausible real typo: the canonical name is "select construct
		// swimmingpool" (one word), not "swimming pool".
		CPPUNIT_ASSERT(!GameGUIKeyActions::getAction("select construct swimming pool").has_value());
		CPPUNIT_ASSERT(!GameGUIKeyActions::getAction("not a real action").has_value());
	}

	void testGuiUnknownNameDoesNotGrowMap(void)
	{
		size_t before = GameGUIKeyActions::keys.size();
		GameGUIKeyActions::getAction("some bogus token");
		GameGUIKeyActions::getAction("another bogus token");
		CPPUNIT_ASSERT_EQUAL(before, GameGUIKeyActions::keys.size());
	}

	void testGuiRoundTripAllActions(void)
	{
		for(Uint32 i = GameGUIKeyActions::DoNothing; i < GameGUIKeyActions::ActionSize; ++i)
		{
			std::string name = GameGUIKeyActions::getName(i);
			std::optional<Uint32> a = GameGUIKeyActions::getAction(name);
			CPPUNIT_ASSERT(a.has_value());
			CPPUNIT_ASSERT_EQUAL(i, *a);
		}
	}

	void testGuiGetNameOutOfRangeIsEmpty(void)
	{
		CPPUNIT_ASSERT_EQUAL(std::string(""),
			GameGUIKeyActions::getName(GameGUIKeyActions::ActionSize));
		CPPUNIT_ASSERT_EQUAL(std::string(""),
			GameGUIKeyActions::getName(9999));
	}

	void testMapEditKnownNameResolves(void)
	{
		std::optional<Uint32> a = MapEditKeyActions::getAction("select delete tool");
		CPPUNIT_ASSERT(a.has_value());
		CPPUNIT_ASSERT_EQUAL(static_cast<Uint32>(MapEditKeyActions::SelectDeleteTool), *a);
	}

	void testMapEditUnknownNameIsNullopt(void)
	{
		CPPUNIT_ASSERT(!MapEditKeyActions::getAction("select delete toool").has_value());
	}

	void testMapEditUnknownNameDoesNotGrowMap(void)
	{
		size_t before = MapEditKeyActions::keys.size();
		MapEditKeyActions::getAction("bogus mapedit token");
		CPPUNIT_ASSERT_EQUAL(before, MapEditKeyActions::keys.size());
	}
};
CPPUNIT_TEST_SUITE_REGISTRATION(KeyActionLookupTest);
