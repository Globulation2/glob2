// SPDX-License-Identifier: GPL-3.0-or-later
// The one place that names every AI implementation. AI.cpp creates and loads
// controllers through it, so code that only drives AI (the game, the pipeline)
// does not link every AI by itself; a test binary can link a smaller registry.
#include "AIRegistry.h"
#include "AICabino.h"
#include "AICastor.h"
#include "AIJavaScript.h"
#include "AIMaxima.h"
#include "AINicowar.h"
#include "AINull.h"
#include "AINumbi.h"
#include "AIWarrush.h"
#include "cortex/AICortex.h"
#include "shared_runtime/Runtime.h"
#include <Stream.h>
#include <cstdio>
#include <stdexcept>

namespace AIRegistry
{
AIImplementation* create(AI::ImplementationID implementationID, Player *player)
{
	switch (implementationID)
	{
		case AI::NONE:
			return new AINull();
		case AI::NUMBI:
			return new AINumbi(player);
		case AI::CASTOR:
			return new AICastor(player);
		case AI::NICOWAR:
			return new AISharedRuntime::Runtime(new NewNicowar, player);
		case AI::WARRUSH:
			return new AIWarrush(player);
		case AI::ECONO:
			return new AISharedRuntime::Runtime(new AISharedRuntime::Econo, player);
		case AI::MAXIMA:
			return new AIMaxima::Maxima(player);
		case AI::CORTEX:
			return new AICortex(player);
		case AI::CABINO:
			return new Cabino::AICabino(player);
		case AI::JAVASCRIPT:
			return new AIJavaScript(player);
		default:
			throw std::runtime_error("Unknown AI implementation");
	}
}

Loaded load(AI::ImplementationID implementationID, GAGCore::InputStream *stream, Player *player, Sint32 versionMinor,
	AIImplementation *&implementation)
{
	switch (implementationID)
	{
		case AI::NONE:
			implementation=new AINull();
		break;
		case AI::NUMBI:
			implementation=new AINumbi(player);
			if (!implementation->load(stream, player, versionMinor))
			{
				return Loaded::FailedInSection;
			}
		break;
		case AI::CASTOR:
			implementation=new AICastor(player);
			if (!implementation->load(stream, player, versionMinor))
			{
				fprintf(stderr, "AI::load: AICastor load failed\n");
				return Loaded::FailedInSection;
			}
		break;
		case AI::NICOWAR:
			implementation=new AISharedRuntime::Runtime(new NewNicowar, player);
			if (!implementation->load(stream, player, versionMinor)) return Loaded::Failed;
		break;
		case AI::ECONO:
			implementation=new AISharedRuntime::Runtime(new AISharedRuntime::Econo, player);
			if (!implementation->load(stream, player, versionMinor)) return Loaded::Failed;
		break;
		case AI::WARRUSH:
			implementation=new AIWarrush(player);
			if (!implementation->load(stream, player, versionMinor)) return Loaded::Failed;
		break;
		case AI::MAXIMA:
			if(versionMinor<115)
				throw std::runtime_error("This Maxima save uses a retired strategy format");
			implementation=new AIMaxima::Maxima(stream,player,versionMinor);
		break;
		case AI::CORTEX:
			implementation=new AICortex(stream, player, versionMinor);
		break;
		case AI::CABINO:
			implementation=new Cabino::AICabino(stream, player, versionMinor);
		break;
		case AI::JAVASCRIPT:
			implementation=new AIJavaScript(player);
			if(!implementation->load(stream,player,versionMinor)) return Loaded::Failed;
		break;
		default:
			fprintf(stderr, "AI id %d does not exist, you probably try to load a map from a more recent version of glob2.\n", implementationID);
			return Loaded::Failed;
	}
	return Loaded::Yes;
}
}
