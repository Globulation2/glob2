// SPDX-License-Identifier: GPL-3.0-or-later
#include "AIWorldView.h"
namespace AIEngine
{
AIWorldView::AIWorldView(SimulationSnapshot::Handle captured) : lease(std::move(captured)), tiles(lease)
{
	growth = lease.growth; tick = lease.tick; width = lease.width; height = lease.height;
	if (lease.catalogs) { resourceSizesCount = lease.catalogs->sizesCount; resourceEternal = lease.catalogs->eternal; catalog = lease.catalogs->buildings; resourceShrinkable = lease.catalogs->shrinkable; resourceVisibleToBeCollected = lease.catalogs->visibleToBeCollected; }
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
