// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#pragma once

#include <SDL_rwops.h>

#include <memory>
namespace GAGCore
{
	class InputStream;
	class OutputStream;
}
class Player;
class Order;
class AIImplementation;
/*
 * AI is the base class for the AI-implementations
 */
class AI
{
public:
	///TODO: Explain
	enum ImplementitionID
	{
		///Reference to AINull
		NONE=0,
		///Reference to AINumbi
		NUMBI=1,
		///Reference to AICastor
		CASTOR=2,
		///Reference to AIWarrush
		WARRUSH=3,
		///Reference to the AIEcho based AIReachToInfinity
		REACHTOINFINITY=4,
		///Reference to the AIEcho based AINicowar
		NICOWAR=5,
		///Reference to AICortex (direct AIImplementation binding)
		CORTEX=6,

		SIZE
	};
	static const ImplementitionID toggleAI=CASTOR;

public:
	//AI(Player *player); //TODO: remove this constructor, and choose the AI the user wants.
	AI(ImplementitionID implementitionID, Player *player);
	AI(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor);
	~AI();
	//void init(ImplementitionID ImplementitionID, Player *player);

	AIImplementation *aiImplementation;
	ImplementitionID implementitionID;

	Player *player;

	bool load(GAGCore::InputStream *stream, Sint32 versionMinor);
	void save(GAGCore::OutputStream *stream);

	static std::string getAIText(int id);

	std::shared_ptr<Order> getOrder(bool paused);

//	Uint32 step;
};

