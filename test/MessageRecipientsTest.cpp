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

#include "Glob2Test.h"
#include <vector>
#include <cstdint>

#include "MessageRecipients.h"

class MessageRecipientsTest
{
public:

protected:
	void testSingleRecipient(void)
	{
		const std::vector<int> got = messageRecipientPlayers(1u << 3, 8);
		const std::vector<int> want = {3};
		CHECK(got == want);
	}

	// The bug: only the lowest set bit used to be echoed; a multi-recipient
	// mask tripped assert(k<Team::MAX_COUNT). All set bits must come back.
	void testMultipleRecipientsAreAllReturned(void)
	{
		const std::uint32_t mask = (1u << 0) | (1u << 2) | (1u << 5);
		const std::vector<int> got = messageRecipientPlayers(mask, 8);
		const std::vector<int> want = {0, 2, 5};
		CHECK(got == want);
	}

	// A bit at or beyond the live player count must be ignored rather than
	// indexing an unpopulated players[] slot.
	void testBitsBeyondPlayerCountAreDropped(void)
	{
		const std::uint32_t mask = (1u << 1) | (1u << 9) | (1u << 20);
		const std::vector<int> got = messageRecipientPlayers(mask, 4);
		const std::vector<int> want = {1};
		CHECK(got == want);
	}

	void testEmptyMaskYieldsNothing(void)
	{
		CHECK(messageRecipientPlayers(0u, 12).empty());
	}

	void testNonPositivePlayerCountYieldsNothing(void)
	{
		CHECK(messageRecipientPlayers(0xFFFFFFFFu, 0).empty());
		CHECK(messageRecipientPlayers(0xFFFFFFFFu, -3).empty());
	}

	// Player index 31 is the top of a 32-bit mask; the shift must not overflow.
	void testHighestBitIsReachable(void)
	{
		const std::vector<int> got = messageRecipientPlayers(1u << 31, 32);
		const std::vector<int> want = {31};
		CHECK(got == want);
	}
};

TEST_SUITE("MessageRecipients")
{
	TEST_CASE_FIXTURE(MessageRecipientsTest, "SingleRecipient") { testSingleRecipient(); }
	TEST_CASE_FIXTURE(MessageRecipientsTest, "MultipleRecipientsAreAllReturned") { testMultipleRecipientsAreAllReturned(); }
	TEST_CASE_FIXTURE(MessageRecipientsTest, "BitsBeyondPlayerCountAreDropped") { testBitsBeyondPlayerCountAreDropped(); }
	TEST_CASE_FIXTURE(MessageRecipientsTest, "EmptyMaskYieldsNothing") { testEmptyMaskYieldsNothing(); }
	TEST_CASE_FIXTURE(MessageRecipientsTest, "NonPositivePlayerCountYieldsNothing") { testNonPositivePlayerCountYieldsNothing(); }
	TEST_CASE_FIXTURE(MessageRecipientsTest, "HighestBitIsReachable") { testHighestBitIsReachable(); }
}
