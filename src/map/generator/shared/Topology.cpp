// SPDX-License-Identifier: GPL-3.0-or-later
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
		for (int dx = -1; dx <= 1; ++dx)
		{
			if ((!dx && !dy) || (kind == GridNeighbors::Cardinal && dx && dy))
				continue;
			int x = i % w + dx, y = i / w + dy;
			if (wrap)
			{
				x = (x + w) % w;
				y = (y + h) % h;
			}
			else if (x < 0 || x >= w || y < 0 || y >= h)
				continue;
			visit(y * w + x);
		}
}
} // namespace
std::vector<int> graphDistances(const RegionGraph &graph, const std::vector<int> &sources)
{
	for (const auto &edges : graph)
		for (int v : edges)
			if (v < 0 || size_t(v) >= graph.size())
				throw std::invalid_argument("Invalid graph edge");
	std::vector<int> distances(graph.size(), -1);
	std::queue<int> pending;
	for (int v : sources)
	{
		if (v < 0 || size_t(v) >= graph.size())
			throw std::invalid_argument("Invalid graph source");
		if (distances[v] < 0)
		{
			distances[v] = 0;
			pending.push(v);
		}
	}
	while (!pending.empty())
	{
		int u = pending.front();
		pending.pop();
		for (int v : graph[u])
			if (distances[v] < 0)
			{
				distances[v] = distances[u] + 1;
				pending.push(v);
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
		if (!passable[i] || regions[i] >= 0)
			continue;
		regions[i] = next;
		pending.push_back(int(i));
		while (!pending.empty())
		{
			int u = pending.back();
			pending.pop_back();
			neighbors(u, w, h, wrap, kind,
					  [&](int v)
					  {
						  if (passable[v] && regions[v] < 0)
						  {
							  regions[v] = next;
							  pending.push_back(v);
						  }
					  });
		}
		++next;
	}
	return regions;
}
RegionGraph regionAdjacency(const std::vector<int> &labels, int w, int h,
							const std::vector<int> &ids, bool wrap)
{
	checkGrid(labels.size(), w, h);
	std::map<int, int> indices;
	for (size_t i = 0; i < ids.size(); ++i)
		if (!indices.emplace(ids[i], int(i)).second)
			throw std::invalid_argument("Duplicate region ID");
	RegionGraph graph(ids.size());
	for (size_t i = 0; i < labels.size(); ++i)
	{
		auto u = indices.find(labels[i]);
		if (u == indices.end())
			continue;
		neighbors(int(i), w, h, wrap, GridNeighbors::Cardinal,
				  [&](int n)
				  {
					  auto v = indices.find(labels[n]);
					  if (v != indices.end() && u->second != v->second)
						  graph[u->second].push_back(v->second);
				  });
	}
	for (auto &edges : graph)
	{
		std::sort(edges.begin(), edges.end());
		edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
	}
	return graph;
}
} // namespace MapGeneration
