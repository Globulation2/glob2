// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL_net.h>
#include "AI.h"
#include "Team.h"
#include <string>
/**
 * Player holds the player's state like name, type, id etc.
 */
class BasePlayer
{
public:
	/**
	 * Players can be AI or human players at the local machine or connected via a network.
	 *
	 * RUST PORT: don't replicate this layout. PlayerType conflates two
	 * orthogonal facts — "what kind of slot" (none / lost / network /
	 * local / AI) and "which AI implementation" — by reserving P_AI=5
	 * as a base and treating P_AI+n as "AI implementation n". That's a
	 * sentinel-by-arithmetic encoding with no type safety: nothing
	 * prevents adding a real PlayerType in the trailing range, and
	 * round-tripping requires the helpers below. It survives in C++
	 * only because the value is serialized into saves, replays, and
	 * the network protocol, so renumbering would break wire compat.
	 *
	 * The Rust version should split this into two fields, e.g.:
	 *     enum PlayerKind { None, LostDropping, LostFinal, Network, Local, AI }
	 *     struct BasePlayer {
	 *         kind: PlayerKind,
	 *         ai_type: Option<AI::ImplementitionID>,  // Some iff kind == AI
	 *         ...
	 *     }
	 * Saves get re-versioned in the port anyway, so this is the right
	 * moment to fix it.
	 */
 	enum PlayerType
	{
		///A non existing player //NOTE : we don't need any more because null player are not created
		P_NONE=0,
		///Player will be dropped in any cases, but we still have to exchange orders
		P_LOST_DROPPING=1,
		///Player is no longer taken into account, may be later changed to P_AI. All orders are NULLs.
		P_LOST_FINAL=2,
		///Player connected over a network (YOG/LAN)
		P_IP=3,
		///local Player
		P_LOCAL=4,
		///An AI. Note : P_AI + n is AI type n
		P_AI=5
	};
	//TODO: Explain
	static AI::ImplementitionID implementitionIdFromPlayerType(PlayerType type)
	{
		assert(type>=P_AI);
		return (AI::ImplementitionID)((int)type-(int)P_AI);
	}
	//TODO: Explain
	static PlayerType playerTypeFromImplementitionID(AI::ImplementitionID iid)
	{
		return (PlayerType)((int)iid+(int)P_AI);
	}
	enum {
		///Maximum length of player names
		MAX_NAME_LENGTH = 32
	};

	PlayerType type;
	/// Player slot index. Valid range: [0, Team::MAX_COUNT). Used to index
	/// Game::players[] and as the bit position in numberMask.
	Sint32 number;
	/// Cached 1 << number. Kept in sync via setNumber().
	Uint32 numberMask;
	std::string name;
	/// Index of the Team this player controls. Valid range:
	/// [0, mapHeader.getNumberOfTeams()) — must point at a live Team slot.
	/// BasePlayer::load enforces the wider [0, Team::MAX_COUNT) bound; the
	/// tighter map-aware bound is checked at the Game::setGameHeader call
	/// site, where mapHeader is available.
	Sint32 teamNumber;
	/// Cached 1 << teamNumber. Kept in sync via setTeamNumber().
	Uint32 teamNumberMask;
	///true if this player is to quit but still has orders to process
	bool quitting;
	//TODO: Explain
	Uint32 quitUStep;
	//TODO: Explain
	Uint32 lastUStepToExecute;
	/// YOG player ID copied into the game header. Live YOGPlayerID values are
	/// Uint16; this field stays Uint32 for version-86 saved-game compatibility.
	Uint32 playerID;

public:

	/**
	 *
	 */
	BasePlayer(void);
	/**
      \param number
      \param name
      \param teamn
      \param type
	 */
	BasePlayer(Sint32 number, const std::string& name, Sint32 teamn, PlayerType type);
	//TODO: Explain
	void init();
	virtual ~BasePlayer(void);

	void setNumber(Sint32 number);
	void setTeamNumber(Sint32 teamNumber);
	/// True iff a raw serialized type value is a valid PlayerType. Because of
	/// the P_AI+n encoding (see the PlayerType comment above), the valid range
	/// is [P_NONE, P_AI + AI::SIZE): the base kinds 0..P_AI-1 plus one slot per
	/// AI implementation. Anything else came from a corrupt or hostile stream —
	/// casting it to PlayerType would be undefined behavior, and using it would
	/// index AI dispatch tables out of range.
	static bool isValidSerializedType(Uint32 rawType)
	{
		return rawType < Uint32(P_AI) + Uint32(AI::SIZE);
	}
	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream) const;

	Uint32 checkSum();

	virtual void makeItAI(AI::ImplementitionID aiType);
	//TODO: Explain
	bool disableRecursiveDestruction;
};
