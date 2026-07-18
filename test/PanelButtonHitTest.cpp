// SPDX-License-Identifier: GPL-3.0-or-later

// Regression test for the view-selector strip hit-test in
// GameGUI::handleMenuClick. The old code computed `dm = (mx-dec)/32` inline and
// then tested `(1<<dm) & hiddenGUIElements`. With mx in the empty left margin
// (mx < dec) the numerator is negative; C++ integer division truncated toward
// zero, folding the margin onto dm=0, which happened to keep the subsequent
// `1<<dm` shift defined only by luck. Any tweak that let the quotient go
// negative (a wider inset, a narrower menu, dropping the mx>0 guard) turned the
// shift into undefined behaviour.
//
// panelButtonIndex() replaces that expression with a pure, non-negative-by-
// construction hit-test. This fixture pins it against a transcription of the old
// expression (referenceIndex) everywhere the old code was well-defined, proves
// the returned index can never be negative (the shift's UB precondition), and
// checks the two real strip layouts: 4 buttons/dec=16 in play, 3 buttons/dec=32
// in replay.
//
// PanelButtonHit.h only pulls in <optional> and is header-only, so this links
// nothing — no globalContainer, no SDL.

#include <cppunit/extensions/HelperMacros.h>

#include <optional>

#include "PanelButtonHit.h"

namespace
{
	// In-play strip: NB_VIEWS==4 buttons, dec=(160-4*32)/2.
	constexpr int PLAY_BUTTONS = 4;
	constexpr int PLAY_DEC = 16;
	// Replay strip: RDM_NB_VIEWS==3 buttons, dec=(160-3*32)/2.
	constexpr int REPLAY_BUTTONS = 3;
	constexpr int REPLAY_DEC = 32;

	/// The original algorithm, transcribed from the pre-fix handleMenuClick.
	/// Only defined for rel >= -31 (the range the mx>0 outer guard could actually
	/// produce), because outside it the old `(mx-dec)/32` truncated to a negative
	/// dm and the caller's `1<<dm` was undefined — there is nothing well-defined
	/// to compare against there. Returns the button index the old code would act
	/// on: dm when dm < numButtons, else "no button" (the `dm < NB_VIEWS` guard).
	std::optional<int> referenceIndex(int mx, int leftMargin, int numButtons)
	{
		const int dm = (mx - leftMargin) / 32; // truncates toward zero
		if (dm < numButtons)
			return dm;
		return std::nullopt;
	}

	std::string describe(int mx, int leftMargin, int numButtons)
	{
		return "mx=" + std::to_string(mx) + " dec=" + std::to_string(leftMargin)
			+ " n=" + std::to_string(numButtons);
	}

	/// cppunit can't stream std::optional, so assert on the unwrapped parts.
	void assertIndexIs(int expected, std::optional<int> actual, const std::string& msg)
	{
		CPPUNIT_ASSERT_MESSAGE(msg, actual.has_value());
		CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, expected, *actual);
	}
}

class PanelButtonHitTest : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(PanelButtonHitTest);
	CPPUNIT_TEST(testMatchesOldExpressionWhereDefined);
	CPPUNIT_TEST(testIndexNeverNegative);
	CPPUNIT_TEST(testLeftMarginSelectsFirstButton);
	CPPUNIT_TEST(testInRangeButtons);
	CPPUNIT_TEST(testBeyondLastButtonIsDead);
	CPPUNIT_TEST(testReplayStripLayout);
	CPPUNIT_TEST_SUITE_END();

public:
	/// Brute-force the new hit-test against the transcribed old expression over
	/// the whole range the mx>0 outer guard could produce (mx in 1..panel width),
	/// for both real strip layouts. They must agree everywhere the old code was
	/// well-defined.
	void testMatchesOldExpressionWhereDefined()
	{
		for (const auto [buttons, dec] : {std::pair{PLAY_BUTTONS, PLAY_DEC},
		                                  std::pair{REPLAY_BUTTONS, REPLAY_DEC}})
		{
			for (int mx = 1; mx <= 160; ++mx)
			{
				// Only compare where the old expression was defined (rel>=-31,
				// which for these decs holds for every mx>=1).
				const auto now = panelButtonIndex(mx, dec, buttons);
				const auto before = referenceIndex(mx, dec, buttons);
				CPPUNIT_ASSERT_MESSAGE(describe(mx, dec, buttons),
				                       now.has_value() == before.has_value());
				if (now)
					CPPUNIT_ASSERT_EQUAL_MESSAGE(describe(mx, dec, buttons),
					                             *before, *now);
			}
		}
	}

	/// The whole point of the fix: the index handed to `1<<index` is never
	/// negative, even under a hypothetically wider margin that would have driven
	/// the old truncating division below zero.
	void testIndexNeverNegative()
	{
		for (int leftMargin = 0; leftMargin <= 96; ++leftMargin)
			for (int mx = 1; mx <= 160; ++mx)
			{
				const auto id = panelButtonIndex(mx, leftMargin, PLAY_BUTTONS);
				if (id)
					CPPUNIT_ASSERT(*id >= 0);
			}
	}

	/// Clicks in the empty left margin fold onto button 0 (preserved quirk).
	void testLeftMarginSelectsFirstButton()
	{
		for (int mx = 1; mx < PLAY_DEC; ++mx)
			assertIndexIs(0, panelButtonIndex(mx, PLAY_DEC, PLAY_BUTTONS),
			              describe(mx, PLAY_DEC, PLAY_BUTTONS));
		// Even a margin far wider than any click could reach stays on button 0
		// instead of producing a negative index.
		assertIndexIs(0, panelButtonIndex(1, 96, PLAY_BUTTONS), "wide margin");
	}

	/// Each 32px button occupies exactly its own cell, starting at dec.
	void testInRangeButtons()
	{
		for (int button = 0; button < PLAY_BUTTONS; ++button)
			for (int dx = 0; dx < PANEL_BUTTON_WIDTH; ++dx)
			{
				const int mx = PLAY_DEC + button * PANEL_BUTTON_WIDTH + dx;
				assertIndexIs(button, panelButtonIndex(mx, PLAY_DEC, PLAY_BUTTONS),
				              describe(mx, PLAY_DEC, PLAY_BUTTONS));
			}
	}

	/// The empty margin to the right of the last button hits nothing.
	void testBeyondLastButtonIsDead()
	{
		const int firstDead = PLAY_DEC + PLAY_BUTTONS * PANEL_BUTTON_WIDTH; // 144
		for (int mx = firstDead; mx <= 200; ++mx)
			CPPUNIT_ASSERT(!panelButtonIndex(mx, PLAY_DEC, PLAY_BUTTONS).has_value());
	}

	/// The replay strip has one fewer button and a wider inset; its last cell
	/// ends earlier and everything past it is dead.
	void testReplayStripLayout()
	{
		assertIndexIs(0, panelButtonIndex(REPLAY_DEC, REPLAY_DEC, REPLAY_BUTTONS),
		              "replay button 0");
		assertIndexIs(2, panelButtonIndex(REPLAY_DEC + 2 * PANEL_BUTTON_WIDTH,
		                                  REPLAY_DEC, REPLAY_BUTTONS),
		              "replay button 2");
		const int firstDead = REPLAY_DEC + REPLAY_BUTTONS * PANEL_BUTTON_WIDTH; // 128
		CPPUNIT_ASSERT(!panelButtonIndex(firstDead, REPLAY_DEC, REPLAY_BUTTONS).has_value());
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(PanelButtonHitTest);
