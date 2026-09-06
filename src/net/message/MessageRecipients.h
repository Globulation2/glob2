// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

/// Expands a player-indexed recipient bitmask into the list of targeted player
/// indices, in strictly ascending order.
///
/// Bit `p` set in `recepientsMask` means "player `p` is a recipient". The mask
/// arrives unvalidated over the network (see MessageOrder), so any bit at or
/// beyond `numberOfPlayers` is silently dropped rather than trusted — every
/// returned index is a live slot the caller can safely use to index the
/// fixed-size `Game::players[]` array. A mask with several bits set yields
/// several recipients; the historical single-bit assumption is not required.
///
/// `numberOfPlayers` is the count of live player slots
/// (GameHeader::getNumberOfPlayers()); values <= 0 yield an empty result, and
/// values above 32 are capped since the mask only carries 32 bits.
std::vector<int> messageRecipientPlayers(std::uint32_t recepientsMask, int numberOfPlayers);
