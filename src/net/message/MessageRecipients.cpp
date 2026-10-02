// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageRecipients.h"
#include <algorithm>
#include <limits>

std::vector<int> messageRecipientPlayers(std::uint32_t recipientsMask, int numberOfPlayers)
{
	std::vector<int> recipients;
	if (numberOfPlayers <= 0)
		return recipients;

	// The mask is 32 bits wide, so no bit beyond position 31 can ever be set.
	constexpr int maskSlots = std::numeric_limits<std::uint32_t>::digits;
	const int slotCount = std::min(numberOfPlayers, maskSlots);
	for (int player = 0; player < slotCount; ++player)
		if (recipientsMask & (std::uint32_t(1) << player))
			recipients.push_back(player);

	return recipients;
}
