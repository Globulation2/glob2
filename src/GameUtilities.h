// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

class Game;

namespace GameUtilities
{
	//! Transform world coordinate to user view coordinate, always in case units
	void globalCoordToLocalView(const Game *game, int localTeam, int globalX, int globalY, int *localX, int *localY);
};

