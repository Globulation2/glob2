// SPDX-License-Identifier: GPL-3.0-or-later
#include "GraphMaze.h"
#include "GenerationContext.h"
#include <algorithm>
namespace MapGeneration
{
bool pocketsFit(const Tessellation &g, const std::vector<unsigned char> &pocket)
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
		for (int edge : g.cells[cell].edges)
			door = door || !pocket[g.other(edge, cell)];
		if (!door)
			return false;
	}
	if (!freeCells)
		return false;
	std::vector<unsigned char> seen(g.cells.size(), 0);
	std::vector<int> stack{start};
	seen[start] = 1;
	int reached = 1;
	while (!stack.empty())
	{
		const int cell = stack.back();
		stack.pop_back();
		for (int edge : g.cells[cell].edges)
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

std::vector<int> spreadPockets(const Tessellation &g, int count)
{
	std::vector<unsigned char> isPocket(g.cells.size(), 0);
	std::vector<int> pockets;
	std::vector<long long> nearest(g.cells.size(), 0);
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
			nearest[cell] = pockets.size() == 1 ? g.distance2(cell, chosen)
												: std::min(nearest[cell], g.distance2(cell, chosen));
	}
	return pockets;
}

bool carveSpanningTree(const Tessellation &g, GenerationContext &context,
					   const std::string &stream, const std::vector<unsigned char> &blocked,
					   std::vector<unsigned char> &open)
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
		for (int edge : g.cells[cell].edges)
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

std::vector<int> openPocketDoors(const Tessellation &g, GenerationContext &context,
								 const std::string &stream, const std::vector<int> &pockets,
								 const std::vector<unsigned char> &isPocket,
								 std::vector<unsigned char> &open)
{
	std::vector<int> doors;
	std::vector<int> choices;
	for (int pocket : pockets)
	{
		choices.clear();
		for (int edge : g.cells[pocket].edges)
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

void openLoops(const Tessellation &g, GenerationContext &context, const std::string &stream,
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
		for (int edge : g.cells[cell].edges)
			if (g.edges[edge].cells[0] == cell && !open[edge] && !blocked[g.other(edge, cell)])
				closed.push_back(edge);
	}
	context.shuffle(closed.begin(), closed.end(), stream);
	const size_t extra = std::min(closed.size(), size_t(freeCells * percent / 100));
	for (size_t i = 0; i < extra; ++i)
		open[closed[i]] = 1;
}

std::vector<int> deadEnds(const Tessellation &g, const std::vector<unsigned char> &open,
						  const std::vector<unsigned char> &blocked, std::vector<int> &exitEdge)
{
	std::vector<int> ends;
	exitEdge.assign(g.cells.size(), -1);
	for (int cell = 0; cell < g.cellCount(); ++cell)
	{
		if (blocked[cell])
			continue;
		int degree = 0, exit = -1;
		for (int edge : g.cells[cell].edges)
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
} // namespace MapGeneration
