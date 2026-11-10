// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Game.h"
#include "Utilities.h"
#include <memory>
#include <string>

class ScenePreparation;
struct ViewState;
// A local decorative game. Never owns input, audio, networking or replay sinks.
class MenuColony
{
public:
	MenuColony();
	~MenuColony();
	bool load(const std::string& path = "data/menu/colony.bin");
	void update(Uint64 now, bool visible = true);
	void pause();
	// Frees the game and its presentation; ready() is false until the next load().
	void unload();
	void draw(int width, int height, bool showStatus = true);
	bool ready() const { return bool(game); }
	Uint32 tick() const { return game ? game->stepCounter : 0; }
	Uint32 checksum() const;
private:
	std::unique_ptr<Game> game;
	std::unique_ptr<ViewState> view; // held by pointer so this header stays free of render headers
	std::unique_ptr<ScenePreparation> presentation;
	std::unique_ptr<PresentationFrame> initialScene;
	Uint64 worldTickTime = 0;
	Uint64 lastTime = 0, pending = 0; // pending is accumulated nanoseconds
	bool clockStarted = false;
	bool terrainCompleted = false; // the first frame finishes its terrain before showing
	int centerX = 0, centerY = 0;
};
