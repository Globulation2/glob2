// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ViewportTransform.h>
#include <optional>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

// Gesture state contains neither drawing code nor construction rules. The owner
// translates screen points to world points, validates permissions, and commits
// through GameGUIToolManager. Cancelling a session cannot emit game orders.
struct TouchPlacementSession
{
	using Pointer = std::pair<std::int64_t, std::int64_t>;
	Pointer pointer;
	GAGCore::ViewPoint origin;
	std::string building;
	bool dragging = false;
	GAGCore::ViewRect sourceBounds;
	GAGCore::ViewPoint pointerPosition;
	std::uint64_t lastUpdate = 0;
	int team = -1;
};
struct TouchStrokeSession
{
	// World pixels keep a stroke anchored when the camera moves. No brush orders
	// or speculative map changes are made until the complete stroke is released.
	std::vector<GAGCore::ViewPoint> points;
	int team = -1, zone = -1, figure = -1, mode = -1;
	void cancel() { points.clear(); }
};
// Slider edits are previewed locally and committed once on release. Cancelling
// or changing selection therefore never sends a partial allocation command.
struct TouchAllocationSession
{
	TouchPlacementSession::Pointer pointer;
	GAGCore::ViewRect track;
	int building = -1, team = -1, requested = 0;
};
