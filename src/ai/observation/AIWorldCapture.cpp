// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIWorldView.h"
#include <bit>
#include <limits>
namespace AIEngine
{
AIWorldView::AIWorldView(SimulationSnapshot::Handle captured) : lease(std::move(captured)), width(lease.width), height(lease.height), tiles(*this)
{
	if (width <= 0 || height <= 0) throw std::logic_error("AI observation has no map dimensions");
	if (std::size_t(width) > std::numeric_limits<std::size_t>::max() / std::size_t(height))
		throw std::logic_error("AI observation map dimensions overflow");
	const auto cells = cellCount = std::size_t(width) * std::size_t(height);
	xMask = std::has_single_bit(unsigned(width)) ? width - 1 : -1;
	yMask = std::has_single_bit(unsigned(height)) ? height - 1 : -1;
	maskedGeometry = xMask >= 0 && yMask >= 0;
	if (maskedGeometry) widthShift = std::countr_zero(unsigned(width));
	const auto bind = [&](const auto& owner, SimulationSnapshot::Component component, auto& data) {
		if (!owner) {
			if (SimulationSnapshot::needs(lease.requirements, component))
				throw std::logic_error("AI observation is missing a requested map component");
			return;
		}
		if (owner->cells.size() != cells) throw std::logic_error("AI observation map component size mismatch");
		data = owner->cells.data();
	};
	bind(lease.resources, SimulationSnapshot::Component::Resources, resourceCells);
	bind(lease.occupancy, SimulationSnapshot::Component::Occupancy, occupancyCells);
	bind(lease.areas, SimulationSnapshot::Component::Areas, areaCells);
	if (lease.visibility) {
        if (lease.visibility->discovered.size()!=cells || lease.visibility->visible.size()!=cells)
            throw std::logic_error("AI observation visibility size mismatch");
        discoveredCells=lease.visibility->discovered.data(); visibleCells=lease.visibility->visible.data();
    } else if (SimulationSnapshot::needs(lease.requirements, SimulationSnapshot::Component::Visibility))
        throw std::logic_error("AI observation is missing requested visibility");
	if (lease.terrain) {
		if (!lease.terrain->identity || lease.terrain->identity->size() != cells || lease.terrain->legacy.size() != cells)
			throw std::logic_error("AI observation terrain component size mismatch");
		terrainCells = lease.terrain->identity->data(); legacyTerrainCells = lease.terrain->legacy.data();
	} else if (SimulationSnapshot::needs(lease.requirements, SimulationSnapshot::Component::Terrain))
		throw std::logic_error("AI observation is missing requested terrain");
	growth = lease.growth;
	if (growth) for (const auto count : growth->storageSizes())
		if (count != cells) throw std::logic_error("AI observation growth component size mismatch");
	farmInputs = resourceCells && terrainCells && lease.catalogs && growth;
	tick = lease.tick;
	if (lease.catalogs) { observedUnitTypes = &lease.catalogs->unitTypes; resourceSizesCount = lease.catalogs->sizesCount; resourceEternal = lease.catalogs->eternal; catalog = lease.catalogs->buildings; resourceShrinkable = lease.catalogs->shrinkable; resourceVisibleToBeCollected = lease.catalogs->visibleToBeCollected; }
	if (lease.terrain) { terrain = lease.terrain->registry; terrainRevision = lease.terrain->revision; terrainMovementModifiers = lease.terrain->movementModifiers; airTerrainConstraints = lease.terrain->airConstraints; }
	if (lease.rules) { configuration = lease.rules->configuration; rules = lease.rules->values; ruleValues = lease.rules->named; experimentKeys = lease.rules->experiments; }
	if (lease.areas) farmAreasEnabled = lease.areas->farmEnabled;
	if (lease.teams) { teams = lease.teams->values; totalPrestige = lease.teams->totalPrestige; }
	if (lease.entities) { buildings = lease.entities->buildings; units = lease.entities->units; buildProjects = lease.entities->projects; }
}
std::shared_ptr<const AIWorldView::Catalog> AIWorldView::captureCatalog(const Game& game)
{ return SimulationSnapshot::captureCatalog(game); }
std::shared_ptr<const AIWorldView> AIWorldView::capture(const Game& game, std::shared_ptr<const Catalog> catalog)
{ return std::make_shared<AIWorldView>(SimulationSnapshot::capture(game, std::move(catalog))); }
} // namespace AIEngine
