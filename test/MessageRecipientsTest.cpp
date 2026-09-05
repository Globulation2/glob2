// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit test for messageRecipientPlayers(), the pure helper that expands a
// private-message recipient bitmask into an ordered list of live player
// indices. It guards the extraction of the old GameGUI::executeOrder echo
// loop, which walked the mask assuming exactly one set bit within
// Team::MAX_COUNT: multi-recipient masks fell through and tripped an assert,
// and bits at or beyond the live player count could index an unpopulated
// Game::players[] slot. The helper must instead return every in-range bit and
// silently drop out-of-range ones.

#include <cppunit/extensions/HelperMacros.h>

#include "MessageRecipients.h"

class MessageRecipientsTest: public CPPUNIT_NS::TestCase
{
CPPUNIT_TEST_SUITE(MessageRecipientsTest);
		CPPUNIT_TEST(testSingleRecipient);
		CPPUNIT_TEST(testMultipleRecipientsAreAllReturned);
		CPPUNIT_TEST(testBitsBeyondPlayerCountAreDropped);
		CPPUNIT_TEST(testEmptyMaskYieldsNothing);
		CPPUNIT_TEST(testNonPositivePlayerCountYieldsNothing);
		CPPUNIT_TEST(testHighestBitIsReachable);
	CPPUNIT_TEST_SUITE_END();

protected:
	void testSingleRecipient(void)
	{
		const std::vector<int> got = messageRecipientPlayers(1u << 3, 8);
		const std::vector<int> want = {3};
		CPPUNIT_ASSERT(got == want);
	}

	// The bug: only the lowest set bit used to be echoed; a multi-recipient
	// mask tripped assert(k<Team::MAX_COUNT). All set bits must come back.
	void testMultipleRecipientsAreAllReturned(void)
	{
		const std::uint32_t mask = (1u << 0) | (1u << 2) | (1u << 5);
		const std::vector<int> got = messageRecipientPlayers(mask, 8);
		const std::vector<int> want = {0, 2, 5};
		CPPUNIT_ASSERT(got == want);
	}

	// A bit at or beyond the live player count must be ignored rather than
	// indexing an unpopulated players[] slot.
	void testBitsBeyondPlayerCountAreDropped(void)
	{
		const std::uint32_t mask = (1u << 1) | (1u << 9) | (1u << 20);
		const std::vector<int> got = messageRecipientPlayers(mask, 4);
		const std::vector<int> want = {1};
		CPPUNIT_ASSERT(got == want);
	}

	void testEmptyMaskYieldsNothing(void)
	{
		CPPUNIT_ASSERT(messageRecipientPlayers(0u, 12).empty());
	}

	void testNonPositivePlayerCountYieldsNothing(void)
	{
		CPPUNIT_ASSERT(messageRecipientPlayers(0xFFFFFFFFu, 0).empty());
		CPPUNIT_ASSERT(messageRecipientPlayers(0xFFFFFFFFu, -3).empty());
	}

	// Player index 31 is the top of a 32-bit mask; the shift must not overflow.
	void testHighestBitIsReachable(void)
	{
		const std::vector<int> got = messageRecipientPlayers(1u << 31, 32);
		const std::vector<int> want = {31};
		CPPUNIT_ASSERT(got == want);
	}
};

CPPUNIT_TEST_SUITE_REGISTRATION(MessageRecipientsTest);
