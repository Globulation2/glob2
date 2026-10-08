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
	paintVertexSquare(x, y, t, l);
}

void Map::paintLegacyCells(const std::vector<std::pair<int, int>> &cells, TerrainType t)
{
	std::vector<std::pair<int, int>> corners;
	for (const auto &[x, y] : cells)
		for (int dy = 0; dy <= 1; ++dy)
			for (int dx = 0; dx <= 1; ++dx)
				corners.push_back({x + dx, y + dy});
	paintVertices(corners, t);
}

std::vector<size_t> Map::paintVertices(const std::vector<std::pair<int, int>> &vertices, TerrainType type,
									   bool beaches)
{
	if (!validTerrainType(type)) throw std::invalid_argument("Unknown terrain identity");
	std::vector<size_t> written, changed;
	written.reserve(vertices.size());
	for (const auto &[x, y] : vertices)
		written.push_back(size_t(coordToIndex(x, y)));
	std::sort(written.begin(), written.end());
	written.erase(std::unique(written.begin(), written.end()), written.end());
	auto batch = editTerrain();
	for (const auto vertex : written)
		if (vertexTerrain[vertex] != type)
		{
			setVertexTerrain(vertex, type);
			changed.push_back(vertex);
		}
	if (beaches && (type == GRASS || type == WATER))
	{
		const TerrainType clash = type == GRASS ? WATER : GRASS;
		for (const auto vertex : written)
		{
			const int x = int(vertex & wMask), y = int(vertex >> wDec);
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const auto neighbour = size_t(coordToIndex(x + dx, y + dy));
					if (vertexTerrain[neighbour] == clash &&
						!std::binary_search(written.begin(), written.end(), neighbour))
					{
						setVertexTerrain(neighbour, SAND);
						changed.push_back(neighbour);
					}
				}
		}
	}
	return changed;
}

void Map::paintVertexSquare(int x, int y, TerrainType type, int l)
{
	std::vector<std::pair<int, int>> vertices;
	for (int dy = y - (l >> 1); dy <= y + (l >> 1); ++dy)
		for (int dx = x - (l >> 1); dx <= x + (l >> 1); ++dx)
			vertices.push_back({dx, dy});
	paintVertices(vertices, type);
}

void Map::layBeaches()
{
	std::vector<TerrainType> next(vertexTerrain);
	bool changed = false;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const auto type = vertexTerrainAt(x, y);
			if (type != GRASS && type != WATER) continue;
			const auto clash = type == GRASS ? WATER : GRASS;
			bool shore = false;
			for (int dy = -1; dy <= 1 && !shore; ++dy)
				for (int dx = -1; dx <= 1 && !shore; ++dx)
					shore = vertexTerrainAt(x + dx, y + dy) == clash;
			if (shore)
			{
				next[coordToIndex(x, y)] = SAND;
				changed = true;
			}
		}
	if (changed) assignVertexTerrain(next);
}
