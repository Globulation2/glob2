// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <algorithm>

constexpr int UNIT_ANIMATION_FRAME_MULTIPLIER = 4;
constexpr int UNIT_ANIMATION_FRAMES_PER_DIRECTION = 32;
constexpr int UNIT_ANIMATION_ACTION_COUNT = 7;
constexpr int UNIT_ANIMATION_ACTION_FRAME_COUNT = 8 * UNIT_ANIMATION_FRAMES_PER_DIRECTION;
constexpr int UNIT_ANIMATION_ACTION_BASE_STRIDE =
	UNIT_ANIMATION_ACTION_FRAME_COUNT / UNIT_ANIMATION_FRAME_MULTIPLIER;
constexpr int UNIT_ANIMATION_MAX_ACTION_BASE =
	(UNIT_ANIMATION_ACTION_COUNT - 1) * UNIT_ANIMATION_ACTION_BASE_STRIDE;
// Saved action bases address only the original seven movement blocks. Appended
// turn frames belong to presentation and must not expand catalog validation.
// Authored catalogs validate action bases before rendering; historical migrated
// tables preserve their saved offsets under the private compatibility policy.
constexpr int UNIT_ANIMATION_MOVEMENT_FRAME_COUNT =
	UNIT_ANIMATION_ACTION_COUNT * UNIT_ANIMATION_ACTION_FRAME_COUNT;
constexpr int UNIT_ANIMATION_TURN_FRAMES = 32;
constexpr int UNIT_ANIMATION_ATLAS_FRAME_COUNT =
	UNIT_ANIMATION_MOVEMENT_FRAME_COUNT + UNIT_ANIMATION_ACTION_COUNT * UNIT_ANIMATION_TURN_FRAMES;
constexpr bool validUnitAnimationBase(std::uint32_t actionBase)
{
	return actionBase <= UNIT_ANIMATION_MAX_ACTION_BASE;
}

// Movement, mesh turns and sprite turns share the same 32-phase clock.
constexpr int unitAnimationPhase(int delta)
{
	return delta >> 3;
}

// Movement clips contain eight directions; stationary turns have their own clip.
constexpr int unitMovementFrame(int direction, int delta)
{
	return direction * UNIT_ANIMATION_FRAMES_PER_DIRECTION + unitAnimationPhase(delta);
}

constexpr int unitSkinMeshFrame(int direction, int delta)
{
	return direction == 8 ? unitAnimationPhase(delta) : unitMovementFrame(direction, delta);
}

constexpr int unitSkinSpriteFrame(int direction, int delta)
{
	return direction == 8 ? UNIT_ANIMATION_ACTION_FRAME_COUNT + unitAnimationPhase(delta)
						  : unitMovementFrame(direction, delta);
}

constexpr int unitAnimationFrame(int actionBase, int direction, int delta)
{
	// Historical catalogs may start inside an action block. Their movement
	// offsets stay intact; turns use that block's dedicated stationary clip.
	// Imported Race tables also allow offsets beyond the current atlas. Keep
	// those saved values, but bound their cosmetic turn to the nearest clip.
	return direction == 8
			   ? UNIT_ANIMATION_MOVEMENT_FRAME_COUNT +
					 std::clamp(actionBase / UNIT_ANIMATION_ACTION_BASE_STRIDE, 0,
								UNIT_ANIMATION_ACTION_COUNT - 1) *
						 UNIT_ANIMATION_TURN_FRAMES +
					 unitAnimationPhase(delta)
			   : actionBase * UNIT_ANIMATION_FRAME_MULTIPLIER + unitMovementFrame(direction, delta);
}
