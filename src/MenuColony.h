// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Game.h"
#include "Utilities.h"
#include <memory>
#include <string>

// A local decorative game. Never owns input, audio, networking or replay sinks.
class MenuColony
{
public:
	bool load(const std::string& path = "data/menu/colony.bin");
	void update(Uint64 now, bool visible = true);
	void pause();
	void draw(int width, int height);
	bool ready() const { return bool(game); }
	Uint32 tick() const { return game ? game->stepCounter : 0; }
	Uint32 checksum() const;
private:
	std::unique_ptr<Game> game;
	Game::ViewState view;
	boost::mt19937 rng;
	Uint64 lastTime = 0, pending = 0;
	bool clockStarted = false;
	int centerX = 0, centerY = 0;
};
