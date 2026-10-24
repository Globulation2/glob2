// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

constexpr int UNIT_ANIMATION_FRAME_MULTIPLIER = 4;
constexpr int UNIT_ANIMATION_FRAMES_PER_DIRECTION = 32;
// The shared stock atlas contains seven complete eight-direction animations.
// Authored catalogs validate action bases before rendering; historical migrated
// tables preserve their saved offsets under the private compatibility policy.
constexpr int UNIT_ANIMATION_MOVEMENT_FRAME_COUNT = 1792;
constexpr int UNIT_ANIMATION_TURN_FRAMES = 32;
constexpr int UNIT_ANIMATION_ATLAS_FRAME_COUNT = UNIT_ANIMATION_MOVEMENT_FRAME_COUNT + 7 * UNIT_ANIMATION_TURN_FRAMES;
constexpr int UNIT_ANIMATION_ACTION_FRAME_COUNT = 8 * UNIT_ANIMATION_FRAMES_PER_DIRECTION;
constexpr bool validUnitAnimationBase(std::uint32_t actionBase)
{
	return actionBase <= (UNIT_ANIMATION_MOVEMENT_FRAME_COUNT - UNIT_ANIMATION_ACTION_FRAME_COUNT)
		/ UNIT_ANIMATION_FRAME_MULTIPLIER;
}

// Action bases retain their serialized eight-pose layout.
// Published skin clips retain their eight-heading, 256-frame contract.
constexpr int unitSkinAnimationFrame(int direction, int delta)
{
	return direction == 8
			? (delta >> 5) * UNIT_ANIMATION_FRAMES_PER_DIRECTION
			: direction * UNIT_ANIMATION_FRAMES_PER_DIRECTION + (delta >> 3);
}

constexpr int unitAnimationFrame(int actionBase, int direction, int delta)
{
	// Only the seven stock action bases have dedicated turns. Historical saved
	// catalogs may use unaligned offsets; preserve their original selection.
	if (direction == 8 && actionBase >= 0 && actionBase <= 384 && actionBase % 64 == 0)
		return UNIT_ANIMATION_MOVEMENT_FRAME_COUNT + (actionBase / 64) * UNIT_ANIMATION_TURN_FRAMES + (delta >> 3);
	return actionBase * UNIT_ANIMATION_FRAME_MULTIPLIER + unitSkinAnimationFrame(direction, delta);
}
