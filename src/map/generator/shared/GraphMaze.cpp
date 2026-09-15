// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphMaze.h"
#include "Topology.h"
#include "GenerationContext.h"
#include <utility>
#include <algorithm>
#include <stdexcept>
namespace MapGeneration
{
CellGraph cellGraph(const Tessellation &tiling)
{
	CellGraph g;
	for (const auto &cell : tiling.cells)
		g.cellEdges.push_back(cell.edges);
	for (const auto &edge : tiling.edges)
		g.edgeCells.push_back({edge.cells[0], edge.cells[1]});
	g.distance2 = [&tiling](int a, int b) { return tiling.distance2(a, b); };
	return g;
}

CellGraph cellGraph(const Torus &t, const std::vector<Site> &sites,
					const std::vector<std::vector<int>> &neighbours)
{
	CellGraph g;
	g.cellEdges.assign(sites.size(), {});
	for (int a = 0; a < int(neighbours.size()); ++a)
		for (int b : neighbours[a])
			if (a < b)
			{
				g.cellEdges[a].push_back(int(g.edgeCells.size()));
				g.cellEdges[b].push_back(int(g.edgeCells.size()));
				g.edgeCells.push_back({a, b});
			}
	g.distance2 = [t, sites](int a, int b)
	{ return (long long)t.dist2(sites[a].x, sites[a].y, sites[b].x, sites[b].y); };
	return g;
}

bool pocketsFit(const CellGraph &g, const std::vector<unsigned char> &pocket)
{
	int start = -1, freeCells = 0;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		if (!pocket[cell])
		{
			++freeCells;
			start = cell;
			continue;
		}
		bool door = false;
		for (int edge : g.cellEdges[cell])
			door = door || !pocket[g.other(edge, cell)];
		if (!door)
			return false;
	}
	if (!freeCells)
		return false;
	std::vector<unsigned char> seen(size_t(g.cellCount()), 0);
	std::vector<int> stack{start};
	seen[start] = 1;
	int reached = 1;
	while (!stack.empty())
	{
		const int cell = stack.back();
		stack.pop_back();
		for (int edge : g.cellEdges[cell])
		{
			const int next = g.other(edge, cell);
			if (!pocket[next] && !seen[next])
			{
				seen[next] = 1;
				++reached;
				stack.push_back(next);
			}
		}
	}
	return reached == freeCells;
}

std::vector<int> spreadPockets(const CellGraph &g, int count)
{
	std::vector<unsigned char> isPocket(size_t(g.cellCount()), 0);
	std::vector<int> pockets;
	std::vector<long long> nearest(size_t(g.cellCount()), 0);
	for (int k = 0; k < count; ++k)
	{
		std::vector<int> order;
		for (int cell = 0; cell < g.cellCount(); ++cell)
			if (!isPocket[cell])
				order.push_back(cell);
		std::stable_sort(order.begin(), order.end(),
						 [&](int a, int b) { return nearest[a] > nearest[b]; });
		int chosen = -1;
		for (int cell : order)
		{
			isPocket[cell] = 1;
			if (pocketsFit(g, isPocket))
			{
				chosen = cell;
				break;
			}
			isPocket[cell] = 0;
		}
		if (chosen < 0)
			return {};
		pockets.push_back(chosen);
		for (int cell = 0; cell < g.cellCount(); ++cell)
			nearest[cell] = pockets.size() == 1
								? g.distance2(cell, chosen)
								: std::min(nearest[cell], g.distance2(cell, chosen));
	}
	return pockets;
}

std::vector<int> closedEdges(const CellGraph &g, const std::vector<unsigned char> &open,
							 const std::vector<unsigned char> &blocked)
{
	if (open.size() != g.edgeCells.size() ||
		(!blocked.empty() && blocked.size() != g.cellEdges.size()))
		throw std::invalid_argument("Maze masks do not match the cell graph");
	std::vector<int> result;
	for (int edge = 0; edge < int(g.edgeCells.size()); ++edge)
		if (!open[edge] &&
			(blocked.empty() || (!blocked[g.edgeCells[edge][0]] && !blocked[g.edgeCells[edge][1]])))
			result.push_back(edge);
	return result;
}

std::vector<int> edgeDetours(const CellGraph &g, const std::vector<unsigned char> &open,
							 const std::vector<int> &edges)
{
	if (open.size() != g.edgeCells.size())
		throw std::invalid_argument("Maze edge mask does not match the cell graph");
	RegionGraph routes(g.cellCount());
	for (int edge = 0; edge < int(g.edgeCells.size()); ++edge)
		if (open[edge])
		{
			const auto &cells = g.edgeCells[edge];
			routes[cells[0]].push_back(cells[1]);
			routes[cells[1]].push_back(cells[0]);
		}
	std::vector<int> result(g.edgeCells.size(), -1);
	// Several candidate edges can have the same first endpoint. One flood per source
	// gives exactly the same distances while avoiding repeated graph walks.
	std::vector<std::vector<int>> distances(g.cellCount());
	for (int edge : edges)
	{
		if (edge < 0 || edge >= int(g.edgeCells.size()))
			throw std::invalid_argument("Shortcut edge outside the cell graph");
		const auto &cells = g.edgeCells[edge];
		if (distances[cells[0]].empty())
			distances[cells[0]] = graphDistances(routes, {cells[0]});
		result[edge] = distances[cells[0]][cells[1]];
	}
	return result;
}

std::vector<int> farthestCells(const CellGraph &g, int count,
							   const std::vector<unsigned char> &eligible)
{
	std::vector<unsigned char> taken(size_t(g.cellCount()), 0);
	std::vector<int> chosen;
	std::vector<long long> nearest(size_t(g.cellCount()), 0);
	for (int k = 0; k < count; ++k)
	{
		int best = -1;
		for (int cell = 0; cell < g.cellCount(); ++cell)
			if (eligible[cell] && !taken[cell] && (best < 0 || nearest[cell] > nearest[best]))
				best = cell;
		if (best < 0)
			return {};
		taken[best] = 1;
		chosen.push_back(best);
		for (int cell = 0; cell < g.cellCount(); ++cell)
			nearest[cell] = chosen.size() == 1 ? g.distance2(cell, best)
											   : std::min(nearest[cell], g.distance2(cell, best));
	}
	return chosen;
}

std::vector<std::vector<int>> claimNeighbourCells(const CellGraph &g, const std::vector<int> &seeds,
												  int wanted,
												  const std::vector<unsigned char> &eligible)
{
	std::vector<std::vector<int>> held(seeds.size());
	std::vector<int> owner(size_t(g.cellCount()), -1);
	for (size_t s = 0; s < seeds.size(); ++s)
		owner[seeds[s]] = int(s);
	for (int round = 0; round < wanted; ++round)
	{
		std::vector<int> taken;
		bool complete = true;
		for (size_t s = 0; s < seeds.size() && complete; ++s)
		{
			int best = -1;
			const auto consider = [&](int cell)
			{
				for (int edge : g.cellEdges[cell])
				{
					const int next = g.other(edge, cell);
					if (owner[next] >= 0 || !eligible[next])
						continue;
					if (best < 0 || g.distance2(seeds[s], next) < g.distance2(seeds[s], best))
						best = next;
				}
			};
			consider(seeds[s]);
			for (int cell : held[s])
				consider(cell);
			if (best < 0)
				complete = false;
			else
			{
				owner[best] = int(s);
				held[s].push_back(best);
				taken.push_back(best);
			}
		}
		if (!complete)
		{
			// The round is undone so no seed holds more than another.
			for (int cell : taken)
			{
				held[owner[cell]].pop_back();
				owner[cell] = -1;
			}
			break;
		}
	}
	return held;
}

bool carveSpanningTree(const CellGraph &g, GenerationContext &context, const std::string &stream,
					   const std::vector<unsigned char> &blocked, std::vector<unsigned char> &open)
{
	std::vector<int> free;
	for (int cell = 0; cell < g.cellCount(); ++cell)
		if (!blocked[cell])
			free.push_back(cell);
	if (free.empty())
		return false;
	std::vector<unsigned char> visited(blocked);
	std::vector<int> stack{free[context.bounded(stream, std::uint32_t(free.size()))]};
	visited[stack.back()] = 1;
	std::vector<int> choices;
	while (!stack.empty())
	{
		const int cell = stack.back();
		choices.clear();
		for (int edge : g.cellEdges[cell])
			if (!visited[g.other(edge, cell)])
				choices.push_back(edge);
		if (choices.empty())
		{
			stack.pop_back();
			continue;
		}
		const int edge = choices[context.bounded(stream, std::uint32_t(choices.size()))];
		const int next = g.other(edge, cell);
		open[edge] = 1;
		visited[next] = 1;
		stack.push_back(next);
	}
	return std::all_of(visited.begin(), visited.end(), [](unsigned char v) { return v != 0; });
}

bool carveNearTree(const CellGraph &g, GenerationContext &context, const std::string &stream,
				   const std::vector<unsigned char> &blocked, int jitterPercent,
				   std::vector<unsigned char> &open)
{
	// Every edge between two free cells, keyed by its stretched distance; the edge index breaks
	// ties, so the order is the same on every platform.
	std::vector<std::pair<long long, int>> edges;
	for (size_t edge = 0; edge < g.edgeCells.size(); ++edge)
	{
		const int a = g.edgeCells[edge][0], b = g.edgeCells[edge][1];
		if (blocked[a] || blocked[b])
			continue;
		const long long stretch = 100 + context.bounded(stream, std::uint32_t(2 * jitterPercent + 1));
		edges.push_back({g.distance2(a, b) * stretch, int(edge)});
	}
	std::sort(edges.begin(), edges.end());
	DisjointSets sets(g.cellCount());
	int joined = 0, free = 0;
	for (int cell = 0; cell < g.cellCount(); ++cell)
		free += !blocked[cell];
	for (const auto &[key, edge] : edges)
		if (sets.unite(g.edgeCells[edge][0], g.edgeCells[edge][1]))
		{
			open[edge] = 1;
			++joined;
		}
	return free > 0 && joined == free - 1;
}

std::vector<int> openPocketDoors(const CellGraph &g, GenerationContext &context,
								 const std::string &stream, const std::vector<int> &pockets,
								 const std::vector<unsigned char> &isPocket,
								 std::vector<unsigned char> &open)
{
	std::vector<int> doors;
	std::vector<int> choices;
	for (int pocket : pockets)
	{
		choices.clear();
		for (int edge : g.cellEdges[pocket])
			if (!isPocket[g.other(edge, pocket)])
				choices.push_back(edge);
		if (choices.empty())
		{
			doors.push_back(-1);
			continue;
		}
		const int edge = choices[context.bounded(stream, std::uint32_t(choices.size()))];
		open[edge] = 1;
		doors.push_back(edge);
	}
	return doors;
}

void openLoops(const CellGraph &g, GenerationContext &context, const std::string &stream,
			   const std::vector<unsigned char> &blocked, int percent,
			   std::vector<unsigned char> &open)
{
	std::vector<int> closed;
	int freeCells = 0;
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		if (blocked[cell])
			continue;
		++freeCells;
		for (int edge : g.cellEdges[cell])
			if (g.edgeCells[edge][0] == cell && !open[edge] && !blocked[g.other(edge, cell)])
				closed.push_back(edge);
	}
	context.shuffle(closed.begin(), closed.end(), stream);
	const size_t extra = std::min(closed.size(), size_t(freeCells * percent / 100));
	for (size_t i = 0; i < extra; ++i)
		open[closed[i]] = 1;
}

std::vector<int> deadEnds(const CellGraph &g, const std::vector<unsigned char> &open,
						  const std::vector<unsigned char> &blocked, std::vector<int> &exitEdge)
{
	std::vector<int> ends;
	exitEdge.assign(size_t(g.cellCount()), -1);
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		if (blocked[cell])
			continue;
		int degree = 0, exit = -1;
		for (int edge : g.cellEdges[cell])
			if (open[edge])
			{
				++degree;
				exit = edge;
			}
		if (degree == 1)
		{
			ends.push_back(cell);
			exitEdge[cell] = exit;
		}
	}
	return ends;
}
bool pocketsFit(const Tessellation &g, const std::vector<unsigned char> &pocket)
{
	return pocketsFit(cellGraph(g), pocket);
}

std::vector<int> spreadPockets(const Tessellation &g, int count)
{
	return spreadPockets(cellGraph(g), count);
}

bool carveSpanningTree(const Tessellation &g, GenerationContext &context, const std::string &stream,
					   const std::vector<unsigned char> &blocked, std::vector<unsigned char> &open)
{
	return carveSpanningTree(cellGraph(g), context, stream, blocked, open);
}

std::vector<int> openPocketDoors(const Tessellation &g, GenerationContext &context,
								 const std::string &stream, const std::vector<int> &pockets,
								 const std::vector<unsigned char> &isPocket,
								 std::vector<unsigned char> &open)
{
	return openPocketDoors(cellGraph(g), context, stream, pockets, isPocket, open);
}

void openLoops(const Tessellation &g, GenerationContext &context, const std::string &stream,
			   const std::vector<unsigned char> &blocked, int percent,
			   std::vector<unsigned char> &open)
{
	openLoops(cellGraph(g), context, stream, blocked, percent, open);
}

std::vector<int> deadEnds(const Tessellation &g, const std::vector<unsigned char> &open,
						  const std::vector<unsigned char> &blocked, std::vector<int> &exitEdge)
{
	return deadEnds(cellGraph(g), open, blocked, exitEdge);
}
} // namespace MapGeneration
