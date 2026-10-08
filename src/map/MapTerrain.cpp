// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "TerrainCornerPresentation.h"
#include "Utilities.h"
#include <algorithm>
#include <stdexcept>

// Transitional terrain adapters over the vertex store: the classic corner
// brushes and the sprite frames the legacy renderer still reads.

Uint16 Map::getTerrain(size_t pos) const
{
	const int x = int(pos & wMask), y = int(pos >> wDec);
	return legacyCellFrame(terrainRegistry(), cellCorners(x, y), x, y);
}

TerrainType Map::presentationTypeAt(size_t index) const
{
	return dominantCornerTerrain(cellCorners(index));
}

void Map::setTerrain(int x, int y, Uint16 sprite)
{
	if (const auto corners = legacyFrameCorners(sprite))
	{
		auto batch = editTerrain();
		setVertexTerrain(x, y, (*corners)[0]);
		setVertexTerrain(x + 1, y, (*corners)[1]);
		setVertexTerrain(x, y + 1, (*corners)[2]);
		setVertexTerrain(x + 1, y + 1, (*corners)[3]);
		return;
	}
	for (unsigned t = 0; t < terrainRegistry().size(); ++t)
	{
		const auto &frames = terrainRegistry().compatibility(TerrainType(t));
		if (sprite >= frames.firstFrame && sprite < frames.firstFrame + frames.variants)
		{
			setCellTerrain(x, y, TerrainType(t));
			return;
		}
	}
	throw std::invalid_argument("Unknown terrain sprite");
}

void Map::setUMatPos(int x, int y, TerrainType t, int l)
{
	auto terrainBatch = editTerrain();
	const TerrainType clash = t == GRASS ? WATER : t == WATER ? GRASS : t;
	for (int dx = x - (l >> 1); dx < x + (l >> 1) + 1; dx++)
		for (int dy = y - (l >> 1); dy < y + (l >> 1) + 1; dy++)
		{
			if (clash != t)
				for (int ny = -1; ny <= 1; ++ny)
					for (int nx = -1; nx <= 1; ++nx)
						if ((nx || ny) && vertexTerrainAt(dx + nx, dy + ny) == clash)
							setVertexTerrain(dx + nx, dy + ny, SAND);
			setVertexTerrain(dx, dy, t);
		}
}

void Map::paintLegacyCells(const std::vector<std::pair<int, int>> &cells, TerrainType t)
{
	if (t != GRASS && t != SAND && t != WATER)
		throw std::invalid_argument("paintLegacyCells requires a legacy corner terrain");
	if (cells.empty())
		return;
	auto terrainBatch = editTerrain();
	// The corners written: all four of every listed cell, sorted for lookup.
	std::vector<size_t> written;
	written.reserve(cells.size() * 4);
	for (const auto &[x, y] : cells)
		for (int dy = 0; dy <= 1; ++dy)
			for (int dx = 0; dx <= 1; ++dx)
				written.push_back(coordToIndex(x + dx, y + dy));
	std::sort(written.begin(), written.end());
	written.erase(std::unique(written.begin(), written.end()), written.end());
	for (const auto corner : written)
		setVertexTerrain(corner, t);
	// Grass and water corners never touch, so an opposite-kind corner next to
	// the written set becomes sand. Sand needs no shore.
	if (t != SAND)
	{
		const TerrainType clash = t == GRASS ? WATER : GRASS;
		for (const auto corner : written)
		{
			const int x = int(corner & wMask), y = int(corner >> wDec);
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const auto neighbour = size_t(coordToIndex(x + dx, y + dy));
					if (vertexTerrain[neighbour] == clash &&
						!std::binary_search(written.begin(), written.end(), neighbour))
						setVertexTerrain(neighbour, SAND);
				}
		}
	}
}
