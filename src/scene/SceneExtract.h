// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scene/Scene.h"
#include "sim/ClientRequests.h"
#include "sim/EntityRef.h"

class Game;

//! What the client wants drawn this frame: whose view, and what it has selected.
struct SceneRequest
{
	int localTeam = 0;
	ClientRequests::ClientView view;
	BuildingRef selectedBuilding;
	UnitRef selectedUnit;
};

//! Fill scene from game for request. The only place presentation code reads the
//! simulation: it runs where the game may be read (between ticks, on the
//! simulation side once threaded) and never modifies the game. Each part of the
//! Scene is extracted next to the code that draws it.
void extractScene(const Game &game, const SceneRequest &request, Scene &scene);
