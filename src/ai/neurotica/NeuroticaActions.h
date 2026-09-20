// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL_stdinc.h>
#include <array>
#include <memory>
#include <vector>
class Team;
class Order;
namespace Neurotica
{
	// Versioned semantic orders. These are NOT network/replay order bytes.
	// All integers on this private learning interface are little endian.
	enum ActionOp
	{
		HOLD,
		CREATE,
		DELETE_BUILDING,
		CANCEL_DELETE,
		CONSTRUCT,
		CANCEL_CONSTRUCTION,
		STAFF,
		MIX,
		RADIUS,
		MIN_LEVEL,
		PRIORITY,
		MOVE_FLAG,
		CLEAR_RESOURCES,
		EXCHANGE,
		GUARD_AREA,
		CLEAR_AREA,
		FORBIDDEN_AREA,
		SHARING,
		OP_COUNT
	};
	enum ActionField
	{
		OP,
		SOURCE,
		TYPE,
		X,
		Y,
		WORKERS,
		FUTURE,
		FLAG_RADIUS,
		R0,
		R1,
		R2,
		BUILD_PRIORITY,
		FLAG_LEVEL,
		DROP,
		CLEAR_MASK,
		RECEIVE_MASK,
		SEND_MASK,
		AREA_MODE,
		DELAY,
		FIELD_COUNT
	};
	constexpr int ENTITY_FIELDS = 29;
	struct Action
	{
		std::array<Sint32, FIELD_COUNT> v{};
		std::vector<Uint8> area;
		Action() { v[DELAY] = 1; }
	};
	std::vector<Uint8> actionContext(Team *team);
	Action encodeAction(Team *team, Order &order);
	std::shared_ptr<Order> decodeAction(Team *team, const Action &action);
	std::vector<Uint8> packAction(const Action &action);
	Action unpackAction(const std::vector<Uint8> &bytes);
	void recordAction(Team *team, int player, Order &order);
} // namespace Neurotica
