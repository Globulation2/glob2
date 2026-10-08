// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

// Shared internals for the src/render/ translation units. Not exposed beyond
// the rendering split-out files.

// Set to 1 to render AI gradient / coordinate debug overlays.
#define DEBUG_RENDER_GRADIENTS 0


// All values here are presentation data. Tile bounds include the extra boundary
// row/column; viewport dimensions are logical pixels before backend transforms.
struct GameRenderFrame
{
    GAGCore::GraphicContext& target;
    GAGCore::Sprite& terrain;
    int left, top, right, bottom, width, height, viewportX, viewportY, localTeam;
    Uint32 options, visibleTeams;
};
