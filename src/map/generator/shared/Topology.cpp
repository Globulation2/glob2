#include "GenerationWork.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerOfTwo.h"
#include "Topology.h"
#include <algorithm>
#include <map>
#include <queue>
#include <stdexcept>
namespace MapGeneration
{
namespace
{
void checkGrid(size_t size, int width, int height)
{
	if (width <= 0 || height <= 0 || size != size_t(width) * height)
		throw std::invalid_argument("Invalid topology grid");
}
template <class F> void neighbors(int i, int w, int h, bool wrap, GridNeighbors kind, F visit)
{
	for (int dy = -1; dy <= 1; ++dy)
	{
		::MapGeneration::generationCheckpoint();
		for (int dx = -1; dx <= 1; ++dx)
		{
			::MapGeneration::generationCheckpoint();
			if ((!dx && !dy) || (kind == GridNeighbors::Cardinal && dx && dy))
				continue;
			int x = dimensionRemainder(i, w) + dx, y = i / w + dy;
			if (wrap)
			{
				x = dimensionRemainder(x + w, w);
				y = dimensionRemainder(y + h, h);
			}
			else if (x < 0 || x >= w || y < 0 || y >= h)
				continue;
			visit(y * w + x);
		}
	}
}
} // namespace
std::vector<int> graphDistances(const RegionGraph &graph, const std::vector<int> &sources)
{
	for (const auto &edges : graph)
	{
		::MapGeneration::generationCheckpoint();
		for (int v : edges)
		{
			::MapGeneration::generationCheckpoint();
			if (v < 0 || size_t(v) >= graph.size())
				throw std::invalid_argument("Invalid graph edge");
		}
	}
	std::vector<int> distances(graph.size(), -1);
	std::queue<int> pending;
	for (int v : sources)
	{
		::MapGeneration::generationCheckpoint();
		if (v < 0 || size_t(v) >= graph.size())
			throw std::invalid_argument("Invalid graph source");
		if (distances.at(v) < 0)
		{
			distances.at(v) = 0;
			pending.push(v);
		}
	}
	while (!pending.empty())
	{
		::MapGeneration::generationCheckpoint();
		int u = pending.front();
		pending.pop();
		for (int v : graph.at(u))
		{
			::MapGeneration::generationCheckpoint();
			if (distances.at(v) < 0)
			{
				distances.at(v) = distances.at(u) + 1;
				pending.push(v);
			}
		}
	}
	return distances;
}
std::vector<int> connectedRegions(const std::vector<unsigned char> &passable, int w, int h,
								  bool wrap, GridNeighbors kind)
{
	checkGrid(passable.size(), w, h);
	std::vector<int> regions(passable.size(), -1);
	int next = 0;
	std::vector<int> pending;
	for (size_t i = 0; i < passable.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!passable.at(i) || regions.at(i) >= 0)
			continue;
		regions.at(i) = next;
		pending.push_back(int(i));
		while (!pending.empty())
		{
			::MapGeneration::generationCheckpoint();
			int u = pending.back();
			pending.pop_back();
			neighbors(u, w, h, wrap, kind,
					  [&](int v)
					  {
						  if (passable.at(v) && regions.at(v) < 0)
						  {
							  regions.at(v) = next;
							  pending.push_back(v);
						  }
					  });
		}
		++next;
	}
	return regions;
}
ComponentLabels labelComponents(const std::vector<int> &components, const std::vector<int> &labels)
{
	if (components.size() != labels.size())
		throw std::invalid_argument("Component and label grids differ in size");
	ComponentLabels result;
	for (size_t i = 0; i < components.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		const int component = components.at(i);
		if (component < 0)
			continue;
		if (size_t(component) >= components.size())
			throw std::invalid_argument("Component ID outside dense grid range");
		if (component >= int(result.owners.size()))
			result.owners.resize(component + 1, -1);
		if (labels.at(i) < 0)
			continue;
		int &owner = result.owners.at(component);
		if (owner >= 0 && owner != labels.at(i))
		{
			// Retain the first owner and conflict: callers can inspect a deterministic
			// witness without changing the labels of the remaining components.
			if (result.conflictTile < 0)
				result.conflictTile = int(i);
		}
		else
			owner = labels.at(i);
	}
	return result;
}

RegionGraph regionAdjacency(const std::vector<int> &labels, int w, int h,
							const std::vector<int> &ids, bool wrap)
{
	checkGrid(labels.size(), w, h);
	std::map<int, int> indices;
	for (size_t i = 0; i < ids.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		if (!indices.emplace(ids.at(i), int(i)).second)
			throw std::invalid_argument("Duplicate region ID");
	}
	RegionGraph graph(ids.size());
	for (size_t i = 0; i < labels.size(); ++i)
	{
		::MapGeneration::generationCheckpoint();
		auto u = indices.find(labels.at(i));
		if (u == indices.end())
			continue;
		neighbors(int(i), w, h, wrap, GridNeighbors::Cardinal,
				  [&](int n)
				  {
					  auto v = indices.find(labels.at(n));
					  if (v != indices.end() && u->second != v->second)
						  graph.at(u->second).push_back(v->second);
				  });
	}
	for (auto &edges : graph)
	{
		::MapGeneration::generationCheckpoint();
		std::sort(edges.begin(), edges.end());
		edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
	}
	return graph;
}
} // namespace MapGeneration
