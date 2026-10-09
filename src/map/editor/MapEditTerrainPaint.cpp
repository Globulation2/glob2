// SPDX-License-Identifier: GPL-3.0-or-later

// Terrain and resource brushes. Terrain lives on map vertices, so a terrain
// brush is centred on the vertex nearest the pointer and stamps vertices, down
// to a single one; a resource brush is centred on the cell under the pointer.
// The preview draws the same squares the stroke changes: a cell, or the square
// a vertex colours, centred on it. Painting grass or water lays a sand beach
// where they would meet. After a stroke only the resources, units and buildings
// the new terrain disallows go.

#include "PowerOfTwo.h"
#include "BrushCoverage.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "Ressource.h"
#include <SDL3/SDL.h>
#include <StringTable.h>
#include <Toolkit.h>
#include <algorithm>
#include <set>

namespace
{
std::string terrainLabel(const TerrainPresentation &presentation)
{
	const std::string label = presentation.label ? presentation.label : presentation.name;
	const auto *strings = GAGCore::Toolkit::getStringTable();
	if (strings && !label.empty() && label.front() == '[' && strings->doesStringExist(label))
		return strings->getString(label);
	return label;
}
} // namespace

bool MapEdit::brushOnVertices() const
{
	return TerrainSelector::isBaseTerrain(terrainType);
}

MapEdit::BrushCell MapEdit::brushCellAt(int mx, int my) const
{
	int x, y;
	if (brushOnVertices())
		game.map.displayToMapCaseUnaligned(mx, my, &x, &y, viewportX, viewportY);
	else
		game.map.displayToMapCaseAligned(mx, my, &x, &y, viewportX, viewportY);
	return {x, y};
}

std::vector<MapEdit::BrushCell> MapEdit::terrainBrushCells(int mapX, int mapY) const
{
	// The stroke's first stamp is its own origin, also while only hovering.
	const BrushCell origin = firstPlacement ? BrushCell{firstPlacement->x, firstPlacement->y} : BrushCell{mapX, mapY};
	const auto cells = BrushCoverage::stamp(brush.getFigure(), {mapX, mapY}, origin);
	return {cells.begin(), cells.end()};
}

std::vector<MapEdit::BrushCell> MapEdit::invalidResourceCells(const std::vector<BrushCell> &footprint)
{
	std::vector<BrushCell> invalid;
	if (!TerrainSelector::isResource(terrainType))
		return invalid;
	const auto id = TerrainSelector::resourceType(terrainType, game.map.resourceRegistry());
	if (!game.map.resourceRegistry().valid(id))
		return footprint;
	for (const auto &[x, y] : footprint)
		if (!game.map.isResourceAllowed(x, y, int(resourceIndex(id))))
			invalid.push_back({x, y});
	return invalid;
}

std::string MapEdit::resourcePlacementHint() const
{
	if (!TerrainSelector::isResource(terrainType))
		return {};
	const auto &registry = game.map.resourceRegistry();
	const auto id = TerrainSelector::resourceType(terrainType, registry);
	if (!registry.valid(id))
		return {};
	std::vector<std::string> terrains;
	for (unsigned t = 0; t < game.map.terrainRegistry().size(); ++t)
	{
		const auto type = static_cast<::TerrainType>(t);
		const auto &presentation = game.map.terrainPresentation(type);
		if (!presentation.editorSelectable || !game.map.terrainSupportsResourceType(type, id))
			continue;
		auto label = terrainLabel(presentation);
		if (std::find(terrains.begin(), terrains.end(), label) == terrains.end())
			terrains.push_back(std::move(label));
	}
	std::string text = getResourceDisplayName(registry.presentation(id).name) + " can only be placed on: ";
	if (terrains.empty())
		return text + "none of the map's terrains";
	for (size_t i = 0; i < terrains.size(); ++i)
		text += (i ? ", " : "") + terrains[i];
	return text;
}

void MapEdit::showStatus(std::string text, Uint32 durationMs)
{
	statusText = std::move(text);
	statusUntil = SDL_GetTicks() + durationMs;
}

void MapEdit::handleTerrainClick(int mx, int my)
{
	const auto [mapX, mapY] = brushCellAt(mx, my);
	if (lastPlacementX == mapX && lastPlacementY == mapY)
		return;
	if (lastPlacementX == -1)
		firstPlacement = FirstPlacement{mapX, mapY};
	lastPlacementX = mapX;
	lastPlacementY = mapY;
	const int fig = brush.getFigure();
	brushAccumulator.applyBrush(BrushApplication(mapX, mapY, fig), &game.map);
	const auto cells = terrainBrushCells(mapX, mapY);
	const bool adding = brush.getType() == BrushTool::MODE_ADD;
	if (!adding && brush.getType() != BrushTool::MODE_DEL)
		return;
	// Commit terrain-dependent invalidation once for the entire brush stamp.
	auto terrainBatch = game.map.editTerrain();
	auto &map = game.map;

	if (TerrainSelector::isResource(terrainType))
	{
		const auto id = TerrainSelector::resourceType(terrainType, map.resourceRegistry());
		if (!map.resourceRegistry().valid(id))
			return;
		const int index = int(resourceIndex(id));
		for (const auto &[x, y] : cells)
		{
			if (adding)
			{
				++strokeCoveredCells;
				if (map.isResourceAllowed(x, y, index))
				{
					map.setResourceByIndex(x, y, index, 1);
					++strokePlacedResources;
				}
			}
			else if (map.getResource(x, y).type == resourceIndex(id))
				map.replaceResource(x, y, Resource{});
		}
		return;
	}
	if (!TerrainSelector::isBaseTerrain(terrainType))
		return;

	const auto material = TerrainSelector::baseTerrain(terrainType);
	std::vector<size_t> changed;
	if (adding)
		changed = map.paintVertices(cells, material);
	else if (material != GRASS)
	{
		// Del turns the stamped vertices of the selected terrain back to grass.
		std::vector<BrushCell> matching;
		for (const auto &[x, y] : cells)
			if (map.vertexTerrainAt(x, y) == material)
				matching.push_back({x, y});
		changed = map.paintVertices(matching, GRASS);
	}
	if (changed.empty())
		return;
	// Remove only what the new terrain disallows, in every cell touching a
	// changed vertex: cells x-1..x and y-1..y of vertex (x,y). Work in the
	// stamp's unwrapped frame so a stroke across the map edge stays one box.
	const int width = map.getW(), height = map.getH();
	auto unwrap = [](int value, int centre, int size) { return centre + powerOfTwoRemainder(powerOfTwoRemainder(value - centre, size) + size + size / 2, size) - size / 2; };
	int minX = mapX, maxX = mapX, minY = mapY, maxY = mapY;
	for (const auto vertex : changed)
	{
		const int x = unwrap(dimensionRemainder(int(vertex), width), mapX, width), y = unwrap(int(vertex) / width, mapY, height);
		minX = std::min(minX, x); maxX = std::max(maxX, x);
		minY = std::min(minY, y); maxY = std::max(maxY, y);
	}
	const int x = minX - 1, y = minY - 1, w = maxX - minX + 2, h = maxY - minY + 2;
	map.removeUnallowedResources(x, y, w, h);
	game.removeUnallowedUnitsAndBuildings(x, y, w, h);
}

void MapEdit::finishTerrainStroke()
{
	// A resource stroke that covered cells but placed nothing explains why.
	if (selectionMode == PlaceTerrain && TerrainSelector::isResource(terrainType) &&
		strokeCoveredCells > 0 && strokePlacedResources == 0)
		showStatus(resourcePlacementHint());
	strokeCoveredCells = strokePlacedResources = 0;
}

void MapEdit::drawTerrainBrushPreview()
{
	auto *gfx = globalContainer->gfx;
	const int mx = int(MapCamera::wrap(mapMouseX(mouseX), view.scene->map.getW() * 32));
	const int my = int(MapCamera::wrap(mapMouseY(mouseY), view.scene->map.getH() * 32));
	// The same lattice point a click takes (brushCellAt): the nearest vertex
	// for a terrain brush, so checkerboard figures keep the stroke's parity.
	const int offset = brushSquareOffset();
	int centreX, centreY;
    view.scene->map.displayToMapCaseAligned(mx - offset, my - offset, &centreX, &centreY, viewportX, viewportY);
	const bool adding = brush.getType() != BrushTool::MODE_DEL;
	const auto cells = terrainBrushCells(centreX, centreY);
	// Same layout as BrushTool::drawBrush: the pointer's lattice point, then
	// offsets. A vertex's square is centred on it, half a cell up and left.
	const int baseX = ((mx - offset) & ~0x1f) + offset, baseY = ((my - offset) & ~0x1f) + offset;
	constexpr int cellSize = 32, inset = 2;
	auto cellRect = [&](const BrushCell &cell, auto draw)
	{
		draw(baseX + (cell.first - centreX) * cellSize + inset, baseY + (cell.second - centreY) * cellSize + inset,
			 cellSize - inset, cellSize - inset);
	};
	if (adding)
		for (const auto &cell : [&] {
            std::vector<BrushCell> invalid;
            if (!TerrainSelector::isResource(terrainType)) return invalid;
            const auto& world = view.scene->world;
            const auto& registry = view.scene->map.resourceRegistry();
            const auto resource = TerrainSelector::resourceType(terrainType, registry);
            if (!registry.valid(resource)) return cells;
            const auto& properties = registry.properties(resource);
            for (const auto& cell : cells) {
                const auto [x, y] = cell;
                if (!MapState::terrainSupportsResource(world.view(), view.scene->map.coordToIndex(x, y), resource)
                    || (properties.blocksBuilding && view.scene->map.getBuilding(x, y) != NOGBID)
                    || (properties.blocksGround && view.scene->map.getGroundUnit(x, y) != NOGUID)
                    || (properties.blocksAir && view.scene->map.getAirUnit(x, y) != NOGUID)) invalid.push_back(cell);
            }
            return invalid;
        }())
			cellRect(cell, [&](int x, int y, int w, int h) { gfx->drawFilledRect(x, y, w, h, Color(220, 40, 40, 110)); });
	const int intensity = adding ? 255 : 170;
	for (const auto &cell : cells)
		cellRect(cell, [&](int x, int y, int w, int h) { gfx->drawRect(x, y, w, h, Color(intensity, intensity, intensity)); });
}
