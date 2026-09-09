// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

constexpr int UNIT_ANIMATION_FRAME_MULTIPLIER = 4;
constexpr int UNIT_ANIMATION_FRAMES_PER_DIRECTION = 32;

// Action bases retain their serialized eight-pose layout.
constexpr int unitAnimationFrame(int actionBase, int direction, int delta)
{
	return actionBase * UNIT_ANIMATION_FRAME_MULTIPLIER
		+ (direction == 8
			? (delta >> 5) * UNIT_ANIMATION_FRAMES_PER_DIRECTION
			: direction * UNIT_ANIMATION_FRAMES_PER_DIRECTION + (delta >> 3));
}

// Full-frame box shutter using ordinary alpha-over compositing.
// Wrap only within the current action and direction. The caller estimates span
// from current unit speed and render interval; no prior action history is stored.
template<typename Draw>
void drawUnitMotionBlur(int base, int direction, int delta, int span, Draw draw)
{
	const int from = delta - span + 1;
	int drawn = 0;
	for (int d = from & ~7; d <= delta; d += 8)
	{
		const int low = d > from ? d : from;
		const int high = d + 7 < delta ? d + 7 : delta;
		const int weight = high - low + 1;
		drawn += weight;
		draw(unitAnimationFrame(base, direction, d & 255), 255 * weight / drawn);
	}
}
