// SPDX-License-Identifier: GPL-3.0-or-later
//
// Standalone check for allyTeamNumberToWidgetIndex (src/AllyTeamWidgetIndex.h).
// Not part of the SCons build. Compile and run by hand from glob2/:
//
//   g++ -std=c++17 $(sdl2-config --cflags) -Isrc tests/ally_team_widget_index_test.cpp -o .tmp/ally_test && .tmp/ally_test

#include "AllyTeamWidgetIndex.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	int failures = 0;

	void expect(Uint8 allyTeamNumber, int teamCount, int want)
	{
		const int got = allyTeamNumberToWidgetIndex(allyTeamNumber, teamCount);
		if (got != want)
		{
			std::printf("FAIL: (%d, %d) -> %d, want %d\n", allyTeamNumber, teamCount, got, want);
			++failures;
		}
	}
}

int main()
{
	// Well-formed 1-based values map to value - 1.
	expect(1, 4, 0);
	expect(2, 4, 1);
	expect(3, 4, 2);
	expect(4, 4, 3);
	expect(1, 1, 0);
	// 0 (uninitialized header) clamps to the first row instead of underflowing.
	expect(0, 4, 0);
	// Values above the populated row count clamp instead of throwing.
	expect(5, 4, 0);
	expect(255, 4, 0);
	expect(2, 1, 0);

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	std::printf("all ally-team widget index cases passed\n");
	return EXIT_SUCCESS;
}
