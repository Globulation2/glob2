// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Glob2Test.h"

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
class KeyActionLookupTest
{

public:
	KeyActionLookupTest()
	{
		GameGUIKeyActions::init();
		MapEditKeyActions::init();
	}

protected:
	void testGuiKnownNameResolves(void)
	{
		std::optional<Uint32> a = GameGUIKeyActions::getAction("pause game");
		CHECK(a.has_value());
		CHECK_EQ(static_cast<Uint32>(GameGUIKeyActions::PauseGame), *a);
	}

	void testGuiDoNothingNameResolves(void)
	{
		// The literal "do nothing" binding is intentional and must keep working
		// (it must be distinguishable from an unknown token, not conflated).
		std::optional<Uint32> a = GameGUIKeyActions::getAction("do nothing");
		CHECK(a.has_value());
		CHECK_EQ(static_cast<Uint32>(GameGUIKeyActions::DoNothing), *a);
	}

	void testGuiUnknownNameIsNullopt(void)
	{
		// A plausible real typo: the canonical name is "select construct
		// swimming pool" (one word), not "swimming pool".
		CHECK(!GameGUIKeyActions::getAction("select construct swimming pool").has_value());
		CHECK(!GameGUIKeyActions::getAction("not a real action").has_value());
	}

	void testGuiUnknownNameStaysUnknown(void)
	{
		// A failed lookup must not teach the table the bogus name.
		CHECK(!GameGUIKeyActions::getAction("some bogus token").has_value());
		CHECK(!GameGUIKeyActions::getAction("some bogus token").has_value());
	}

	void testGuiRoundTripAllActions(void)
	{
		for(Uint32 i = GameGUIKeyActions::DoNothing; i < GameGUIKeyActions::ActionSize; ++i)
		{
			std::string name = GameGUIKeyActions::getName(i);
			std::optional<Uint32> a = GameGUIKeyActions::getAction(name);
			CHECK(a.has_value());
			CHECK_EQ(i, *a);
		}
	}

	void testGuiGetNameOutOfRangeIsEmpty(void)
	{
		CHECK_EQ(std::string(""), GameGUIKeyActions::getName(GameGUIKeyActions::ActionSize));
		CHECK_EQ(std::string(""), GameGUIKeyActions::getName(9999));
	}

	void testMapEditKnownNameResolves(void)
	{
		std::optional<Uint32> a = MapEditKeyActions::getAction("select delete tool");
		CHECK(a.has_value());
		CHECK_EQ(static_cast<Uint32>(MapEditKeyActions::SelectDeleteTool), *a);
	}

	void testMapEditUnknownNameIsNullopt(void)
	{
		CHECK(!MapEditKeyActions::getAction("select delete toool").has_value());
	}

	void testMapEditUnknownNameStaysUnknown(void)
	{
		CHECK(!MapEditKeyActions::getAction("bogus mapedit token").has_value());
		CHECK(!MapEditKeyActions::getAction("bogus mapedit token").has_value());
	}
};
TEST_SUITE("KeyActionLookup")
{
	TEST_CASE_FIXTURE(KeyActionLookupTest, "GuiKnownNameResolves") { testGuiKnownNameResolves(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "GuiDoNothingNameResolves") { testGuiDoNothingNameResolves(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "GuiUnknownNameIsNullopt") { testGuiUnknownNameIsNullopt(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "GuiUnknownNameStaysUnknown") { testGuiUnknownNameStaysUnknown(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "GuiRoundTripAllActions") { testGuiRoundTripAllActions(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "GuiGetNameOutOfRangeIsEmpty") { testGuiGetNameOutOfRangeIsEmpty(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "MapEditKnownNameResolves") { testMapEditKnownNameResolves(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "MapEditUnknownNameIsNullopt") { testMapEditUnknownNameIsNullopt(); }
	TEST_CASE_FIXTURE(KeyActionLookupTest, "MapEditUnknownNameStaysUnknown") { testMapEditUnknownNameStaysUnknown(); }
}
