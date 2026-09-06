// SPDX-License-Identifier: GPL-3.0-or-later
#include "MessageRecipients.h"

std::vector<int> messageRecipientPlayers(std::uint32_t recepientsMask, int numberOfPlayers)
{
	std::vector<int> recipients;
	if (numberOfPlayers <= 0)
		return recipients;

	// The mask is 32 bits wide, so no bit beyond position 31 can ever be set.
	const int slotCount = numberOfPlayers < 32 ? numberOfPlayers : 32;
	for (int player = 0; player < slotCount; ++player)
		if (recepientsMask & (std::uint32_t(1) << player))
			recipients.push_back(player);

	return recipients;
}
