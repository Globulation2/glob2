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
	using Pointer = std::pair<std::uint64_t, std::uint64_t>;
	Pointer pointer;
	GAGCore::ViewPoint origin;
	std::string building;
	bool dragging = false;
	GAGCore::ViewRect sourceBounds;
	GAGCore::ViewPoint pointerPosition;
	std::uint64_t lastUpdate = 0;
	int team = -1;
};
// A finger that lands on or near one of the local team's flags owns that flag
// instead of the map. Past the tap threshold the flag follows the finger, keeping
// the offset it was grabbed at, and lands on release. Cancelling puts it back.
struct TouchFlagSession
{
	TouchPlacementSession::Pointer pointer;
	int gid = -1, team = -1;
	GAGCore::ViewPoint start, position;
	double offsetX = 0, offsetY = 0; // World pixels from the finger to the flag's centre.
	int originX = 0, originY = 0;     // The flag's tile when it was grabbed.
	bool dragging = false, moved = false;
	std::uint64_t lastUpdate = 0;
};
struct TouchStrokeSession
{
	// World pixels keep a stroke anchored when the camera moves. No brush orders
	// or speculative map changes are made until the complete stroke is released.
	std::vector<GAGCore::ViewPoint> points;
	int team = -1, zone = -1, figure = -1, mode = -1;
	void cancel() { points.clear(); }
};
// A single-tap stroke waits one double-tap window before it paints, because
// the tap may be the first half of a one-finger zoom. Drags are never held.
struct TouchDeferredStroke
{
	TouchStrokeSession stroke;
	std::uint64_t ticks = 0; // Wall-clock milliseconds when the tap ended.
};
// Slider edits are previewed locally and committed once on release. Cancelling
// or changing selection therefore never sends a partial allocation command.
struct TouchAllocationSession
{
	TouchPlacementSession::Pointer pointer;
	GAGCore::ViewRect track;
	int building = -1, team = -1, requested = 0;
	// Dial sliders: which value (workers, a unit ratio or a flag range), the
	// sweep the value spans, and the contact for the readout above the finger.
	int kind = 6, value = 0, maximum = 0, ring = 0;
	bool polar = false;
	double sweepFrom = 0, sweepTo = 0;
	GAGCore::ViewPoint position;
};
