// SPDX-License-Identifier: GPL-3.0-or-later

// The building panel's priority radios must leave room for translated labels.

#include "Glob2Test.h"

#include "PriorityRadioLayout.h"

namespace
{
// No label may reach the next radio on its line, nor the panel's right edge.
void requireNoOverlap(const PriorityRadioLayout &layout, const std::array<int, 3> &widths, int panel = 128)
{
	for (int i = 0; i < 3; ++i)
	{
		const int labelEnd = layout.x[i] + PriorityRadioLayout::LABEL_GAP + widths[i];
		CHECK(labelEnd <= panel);
		for (int j = 0; j < 3; ++j)
			if (j != i && layout.y[j] == layout.y[i] && layout.x[j] > layout.x[i])
				CHECK(labelEnd < layout.x[j]);
	}
}
} // namespace

TEST_SUITE("PriorityRadioLayout")
{
	TEST_CASE("short labels keep the classic thirds")
	{
		const std::array<int, 3> english{16, 18, 20};  // "low", "mid", "high"
		const auto layout = priorityRadioLayout(english);
		CHECK(layout.x == std::array<int, 3>{0, 42, 84});
		CHECK(layout.y == std::array<int, 3>{16, 16, 16});
		CHECK(layout.extraHeight == 0);
		requireNoOverlap(layout, english);
	}

	TEST_CASE("labels wider than a third flow across the row")
	{
		const std::array<int, 3> labels{28, 24, 16};
		const auto layout = priorityRadioLayout(labels);
		CHECK(layout.y == std::array<int, 3>{16, 16, 16});
		CHECK(layout.extraHeight == 0);
		requireNoOverlap(layout, labels);
	}

	TEST_CASE("labels too long for one row stack one per line")
	{
		// French: "basse priorité", "priorité moyenne", "haute priorité".
		const std::array<int, 3> french{70, 86, 72};
		const auto layout = priorityRadioLayout(french);
		CHECK(layout.x == std::array<int, 3>{0, 0, 0});
		CHECK(layout.y[1] - layout.y[0] >= PriorityRadioLayout::RADIO_SIZE);
		CHECK(layout.y[2] - layout.y[1] >= PriorityRadioLayout::RADIO_SIZE);
		CHECK(layout.extraHeight == layout.y[2] - layout.y[0]);
		requireNoOverlap(layout, french);
	}
}
