// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ToolkitBinding.h"
#include "GraphMaze.h"

namespace MapGeneration::JavaScript
{
inline CellGraph ownedSitesCellGraph(Binding &e, const Torus &torus, const std::vector<Site> &sites,
									 const std::vector<std::vector<int>> &neighbours)
{
	if (neighbours.size() != sites.size())
		throw TypeMismatch("Cell graph neighbours must match sites");
	std::uint64_t edges = 0;
	for (std::size_t a = 0; a < neighbours.size(); ++a)
		for (int b : neighbours[a])
		{
			e.chargeNative(1);
			if (b < 0 || std::size_t(b) >= sites.size())
				throw TypeMismatch("Cell graph neighbour outside site range");
			edges += a < std::size_t(b);
		}
	// Native construction captures sites once by value; replacing that capture
	// briefly retains a second copy. Reserve both before either allocation, plus
	// graph vectors with conservative growth capacity. Exported function copies
	// then share the retained vector instead of repeatedly copying its payload.
	e.allocate(512 + 2 * e.footprint(sites) + sites.size() * 24 + edges * 32);
	auto graph = MapGeneration::cellGraph(torus, sites, neighbours);
	auto ownedSites = std::make_shared<const std::vector<Site>>(sites);
	graph.distance2 = [torus, ownedSites](int a, int b)
	{
		return static_cast<long long>(torus.dist2(ownedSites->at(a).x, ownedSites->at(a).y,
												  ownedSites->at(b).x, ownedSites->at(b).y));
	};
	return graph;
}
} // namespace MapGeneration::JavaScript
