// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

constexpr int UNIT_ANIMATION_FRAME_MULTIPLIER = 4;
constexpr int UNIT_ANIMATION_FRAMES_PER_DIRECTION = 32;
constexpr int UNIT_ANIMATION_ACTION_COUNT = 7;
constexpr int UNIT_ANIMATION_ACTION_FRAME_COUNT = 8 * UNIT_ANIMATION_FRAMES_PER_DIRECTION;
constexpr int UNIT_ANIMATION_ACTION_BASE_STRIDE = UNIT_ANIMATION_ACTION_FRAME_COUNT / UNIT_ANIMATION_FRAME_MULTIPLIER;
constexpr int UNIT_ANIMATION_MAX_ACTION_BASE = (UNIT_ANIMATION_ACTION_COUNT - 1) * UNIT_ANIMATION_ACTION_BASE_STRIDE;
// Saved action bases address only the original seven movement blocks. Appended
// turn frames belong to presentation and must not expand catalog validation.
// Authored catalogs validate action bases before rendering; historical migrated
// tables preserve their saved offsets under the private compatibility policy.
constexpr int UNIT_ANIMATION_MOVEMENT_FRAME_COUNT = UNIT_ANIMATION_ACTION_COUNT * UNIT_ANIMATION_ACTION_FRAME_COUNT;
constexpr int UNIT_ANIMATION_TURN_FRAMES = 32;
constexpr int UNIT_ANIMATION_ATLAS_FRAME_COUNT = UNIT_ANIMATION_MOVEMENT_FRAME_COUNT + UNIT_ANIMATION_ACTION_COUNT * UNIT_ANIMATION_TURN_FRAMES;
constexpr bool validUnitAnimationBase(std::uint32_t actionBase)
{
	return actionBase <= UNIT_ANIMATION_MAX_ACTION_BASE;
}

// Published skin clips retain their eight-heading, 256-frame contract: a
// stationary turn borrows phase zero of each heading instead of stock turn art.
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
	if (direction == 8 && actionBase >= 0 && actionBase <= UNIT_ANIMATION_MAX_ACTION_BASE
		&& actionBase % UNIT_ANIMATION_ACTION_BASE_STRIDE == 0)
		return UNIT_ANIMATION_MOVEMENT_FRAME_COUNT
			+ (actionBase / UNIT_ANIMATION_ACTION_BASE_STRIDE) * UNIT_ANIMATION_TURN_FRAMES + (delta >> 3);
	return actionBase * UNIT_ANIMATION_FRAME_MULTIPLIER + unitSkinAnimationFrame(direction, delta);
}
