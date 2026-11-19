// SPDX-License-Identifier: GPL-3.0-or-later

// The building panel's priority row: three radio buttons (low, medium, high),
// each followed by its label. The labels are translated, and many languages
// need far more than the third of the panel the row used to give each option
// ("basse priorité", "niedrig"), so the text ran under the next radio button.
// The draw path and the semantic controls both place the radios from this one
// layout so their positions cannot drift apart.

#pragma once

#include <array>

struct PriorityRadioLayout
{
	// Radio offsets from the panel's left edge and from the row's top (the
	// "Priority" title line); each label starts LABEL_GAP to the right of its radio.
	std::array<int, 3> x{}, y{};
	// Extra height the row needs beyond the single-line layout.
	int extraHeight = 0;

	static constexpr int RADIO_SIZE = 12;
	static constexpr int LABEL_GAP = 14;  // From the radio's left edge to the label.
	static constexpr int ROW_PITCH = 14;  // Between stacked radios.
	static constexpr int FIRST_ROW = 16;  // Below the "Priority" title.
};

// The classic layout puts the three options in equal thirds of the panel. When a
// label does not fit its third, the options flow across the row by their measured
// widths; when even that overflows the panel, they stack one per line.
inline PriorityRadioLayout priorityRadioLayout(const std::array<int, 3> &labelWidths, int panelWidth = 128)
{
	PriorityRadioLayout layout;
	constexpr int padding = 4;  // Between a label's end and the next radio.
	const int slot = panelWidth / 3;
	bool thirds = true;
	for (const int width : labelWidths)
		thirds = thirds && PriorityRadioLayout::LABEL_GAP + width + padding <= slot;
	if (thirds)
	{
		for (int i = 0; i < 3; ++i)
			layout.x[i] = i * slot, layout.y[i] = PriorityRadioLayout::FIRST_ROW;
		return layout;
	}
	int x = 0;
	for (int i = 0; i < 3; ++i)
	{
		layout.x[i] = x;
		layout.y[i] = PriorityRadioLayout::FIRST_ROW;
		x += PriorityRadioLayout::LABEL_GAP + labelWidths[i] + padding;
	}
	if (x - padding <= panelWidth)
		return layout;
	for (int i = 0; i < 3; ++i)
	{
		layout.x[i] = 0;
		layout.y[i] = PriorityRadioLayout::FIRST_ROW + i * PriorityRadioLayout::ROW_PITCH;
	}
	layout.extraHeight = 2 * PriorityRadioLayout::ROW_PITCH;
	return layout;
}
