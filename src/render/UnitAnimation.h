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
