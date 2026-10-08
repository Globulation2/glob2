// SPDX-License-Identifier: GPL-3.0-or-later

// Terrain and resource brushes. Every brush is centred on the map cell under
// the pointer, and the preview draws the same cells the stroke changes. Legacy
// corner terrains (grass, sand, water) paint whole cells with
// Map::paintLegacyCells; catalogue and imported terrains set each cell. After a
// stroke only the resources, units and buildings the new terrain disallows go.

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
bool legacySelector(TerrainSelector::TerrainType type)
{
	return type >= TerrainSelector::Grass && type <= TerrainSelector::Water;
}

std::string terrainLabel(const TerrainPresentation &presentation)
{
	const std::string label = presentation.label ? presentation.label : presentation.name;
	const auto *strings = GAGCore::Toolkit::getStringTable();
	if (strings && !label.empty() && label.front() == '[' && strings->doesStringExist(label))
		return strings->getString(label);
	return label;
}
} // namespace

MapEdit::BrushCell MapEdit::brushCellAt(int mx, int my) const
{
	int x, y;
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

std::vector<MapEdit::BrushCell> MapEdit::terrainStrokeCells(int mapX, int mapY) const
{
	auto cells = terrainBrushCells(mapX, mapY);
	if (!legacySelector(terrainType))
		return cells;
	const auto closed = BrushCoverage::cornerClosure({cells.begin(), cells.end()});
	return {closed.begin(), closed.end()};
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
	const bool legacy = legacySelector(terrainType);
	std::vector<BrushCell> changed;
	if (adding)
	{
		if (legacy)
		{
			changed = terrainStrokeCells(mapX, mapY);
			map.paintLegacyCells(changed, material);
		}
		else
		{
			changed = cells;
			for (const auto &[x, y] : cells)
				map.setCellTerrain(x, y, material);
		}
	}
	else if (material != GRASS)
	{
		// Del reverts the cells of the selected terrain to grass: for a corner
		// terrain, the corner-drawn cells with any corner of it.
		for (const auto &[x, y] : cells)
		{
			const auto corners = map.cellCorners(x, y);
			const bool matches = std::find(corners.begin(), corners.end(), material) != corners.end();
			if (matches)
				changed.push_back({x, y});
		}
		map.paintLegacyCells(changed, GRASS);
	}
	if (changed.empty())
		return;
	// Remove only what the new terrain disallows. Corner painting also turns
	// the cells up to two away into shore, so their contents are checked too.
	const int ring = legacy || !adding ? 2 : 0;
	int minX = changed.front().first, maxX = minX, minY = changed.front().second, maxY = minY;
	for (const auto &[x, y] : changed)
	{
		minX = std::min(minX, x); maxX = std::max(maxX, x);
		minY = std::min(minY, y); maxY = std::max(maxY, y);
	}
	const int x = minX - ring, y = minY - ring, w = maxX - minX + 1 + 2 * ring, h = maxY - minY + 1 + 2 * ring;
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
	const int mx = int(MapCamera::wrap(mapMouseX(mouseX), game.map.getW() * 32));
	const int my = int(MapCamera::wrap(mapMouseY(mouseY), game.map.getH() * 32));
	const auto [centreX, centreY] = brushCellAt(mx, my);
	const bool adding = brush.getType() != BrushTool::MODE_DEL;
	const auto cells = adding ? terrainStrokeCells(centreX, centreY) : terrainBrushCells(centreX, centreY);
	// Same cell layout as BrushTool::drawBrush: the pointer's cell, then offsets.
	const int baseX = mx & ~0x1f, baseY = my & ~0x1f;
	constexpr int cellSize = 32, inset = 2;
	auto cellRect = [&](const BrushCell &cell, auto draw)
	{
		draw(baseX + (cell.first - centreX) * cellSize + inset, baseY + (cell.second - centreY) * cellSize + inset,
			 cellSize - inset, cellSize - inset);
	};
	if (adding)
		for (const auto &cell : invalidResourceCells(cells))
			cellRect(cell, [&](int x, int y, int w, int h) { gfx->drawFilledRect(x, y, w, h, Color(220, 40, 40, 110)); });
	const int intensity = adding ? 255 : 170;
	for (const auto &cell : cells)
		cellRect(cell, [&](int x, int y, int w, int h) { gfx->drawRect(x, y, w, h, Color(intensity, intensity, intensity)); });
}
