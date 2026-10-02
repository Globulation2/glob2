// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/SceneExtract.h"

#include "Game.h"

void extractScene(const Game &game, const SceneRequest &request, Scene &scene)
{
	(void)request;
	scene.tick = game.stepCounter;
	scene.map.extract(game.map);
}
