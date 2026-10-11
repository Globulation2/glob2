// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

constexpr int UNIT_ANIMATION_FRAME_MULTIPLIER = 4;
constexpr int UNIT_ANIMATION_FRAMES_PER_DIRECTION = 32;
// The shared stock atlas contains seven complete eight-direction animations.
// Authored catalogs validate action bases before rendering; historical migrated
// tables preserve their saved offsets under the private compatibility policy.
constexpr int UNIT_ANIMATION_ATLAS_FRAME_COUNT = 1792;
constexpr int UNIT_ANIMATION_ACTION_FRAME_COUNT = 8 * UNIT_ANIMATION_FRAMES_PER_DIRECTION;
constexpr bool validUnitAnimationBase(std::uint32_t actionBase)
{
	return actionBase <= (UNIT_ANIMATION_ATLAS_FRAME_COUNT - UNIT_ANIMATION_ACTION_FRAME_COUNT)
		/ UNIT_ANIMATION_FRAME_MULTIPLIER;
}

// Action bases retain their serialized eight-pose layout.
constexpr int unitAnimationFrame(int actionBase, int direction, int delta)
{
	return actionBase * UNIT_ANIMATION_FRAME_MULTIPLIER
		+ (direction == 8
			? (delta >> 5) * UNIT_ANIMATION_FRAMES_PER_DIRECTION
			: direction * UNIT_ANIMATION_FRAMES_PER_DIRECTION + (delta >> 3));
}
