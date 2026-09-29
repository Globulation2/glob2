// SPDX-License-Identifier: GPL-3.0-or-later
//
// AISharedRuntime::tribool replaced boost::logic::tribool; the Echo AI's decisions
// depend on its three-valued logic, so every operator is checked against the
// Kleene truth tables that Boost implements.

#include "../src/ai/shared_runtime/Tribool.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace AISharedRuntime;

namespace
{
int failures = 0;

// 0 = false, 1 = true, 2 = indeterminate
int code(tribool t)
{
	return indeterminate(t) ? 2 : (t ? 1 : 0);
}

tribool make(int c)
{
	return c == 2 ? tribool(indeterminate) : tribool(c == 1);
}

void check(tribool got, int expected, const std::string& what)
{
	if (code(got) != expected)
	{
		std::cerr << "FAIL: " << what << " gave " << code(got) << ", expected " << expected << '\n';
		++failures;
	}
}
}

int main()
{
	// Rows and columns are false, true, indeterminate.
	const int notTable[3] = {1, 0, 2};
	const int andTable[3][3] = {{0, 0, 0}, {0, 1, 2}, {0, 2, 2}};
	const int orTable[3][3] = {{0, 1, 2}, {1, 1, 1}, {2, 1, 2}};
	const int eqTable[3][3] = {{1, 0, 2}, {0, 1, 2}, {2, 2, 2}};
	const int neTable[3][3] = {{0, 1, 2}, {1, 0, 2}, {2, 2, 2}};

	for (int x = 0; x < 3; ++x)
	{
		const tribool a = make(x);
		const std::string sx = std::to_string(x);
		check(!a, notTable[x], "!" + sx);
		check(tribool(static_cast<bool>(a)), x == 1 ? 1 : 0, "bool(" + sx + ")");
		check(a && indeterminate, andTable[x][2], sx + " && indeterminate");
		check(indeterminate && a, andTable[2][x], "indeterminate && " + sx);
		check(a || indeterminate, orTable[x][2], sx + " || indeterminate");
		check(indeterminate || a, orTable[2][x], "indeterminate || " + sx);
		check(a == indeterminate, 2, sx + " == indeterminate");
		check(a != indeterminate, 2, sx + " != indeterminate");
		for (int y = 0; y < 3; ++y)
		{
			const tribool b = make(y);
			const std::string sxy = sx + " op " + std::to_string(y);
			check(a && b, andTable[x][y], "&& " + sxy);
			check(a || b, orTable[x][y], "|| " + sxy);
			check(a == b, eqTable[x][y], "== " + sxy);
			check(a != b, neTable[x][y], "!= " + sxy);
			if (y < 2)
			{
				const bool yb = y == 1;
				check(a && yb, andTable[x][y], "&& bool " + sxy);
				check(yb && a, andTable[y][x], "bool && " + sxy);
				check(a || yb, orTable[x][y], "|| bool " + sxy);
				check(yb || a, orTable[y][x], "bool || " + sxy);
				check(a == yb, eqTable[x][y], "== bool " + sxy);
				check(yb == a, eqTable[y][x], "bool == " + sxy);
				check(a != yb, neTable[x][y], "!= bool " + sxy);
				check(yb != a, neTable[y][x], "bool != " + sxy);
			}
		}
	}

	check(tribool(), 0, "default is false");
	tribool t = indeterminate;
	check(t, 2, "assign indeterminate");
	t = true;
	check(t, 1, "assign true");

	if (failures)
		return EXIT_FAILURE;
	std::cout << "tribool follows boost::logic::tribool's three-valued logic\n";
}
