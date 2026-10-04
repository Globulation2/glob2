// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Game.h"
#include "Player.h"

#include <algorithm>
#include <thread>

// Count the submitting thread too. Keep the default bounded by useful AI jobs
// and available CPUs; explicit thread-count options can still request 1..64.
inline unsigned defaultAIThreadCount(const Game& game)
{
	unsigned controllers=0;
	for(int i=0; i<game.gameHeader.getNumberOfPlayers(); ++i)
		if(game.players[i] && game.players[i]->ai)
			++controllers;
	const unsigned hardware=std::thread::hardware_concurrency();
	return std::clamp(std::min(controllers, hardware ? hardware : 1u), 1u, 4u);
}
