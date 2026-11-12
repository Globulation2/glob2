// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "MapRouting.h"
#include "GradientRuntime.h"
#include "field/astar/AStar.h"

MapRouting::MapRouting(Map &owner) : map(owner), gradientRuntime(std::make_unique<GradientRuntime>())
{
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MaterialSlotCount; r++)
			for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
			{
				materialGradients[t][r][s] = NULL;
				marketMaterialGradients[t][r][s] = NULL;
			}
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int s=0; s<MOVEMENT_CLASS_COUNT; s++)
		{
			forbiddenGradient[t][s] = NULL;
			guardAreasGradient[t][s] = NULL;
			clearAreasGradient[t][s] = NULL;
		}
}

// Map::clear frees every buffer above before the owner releases this object.
MapRouting::~MapRouting() = default;
