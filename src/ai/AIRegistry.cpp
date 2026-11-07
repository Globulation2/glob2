// SPDX-License-Identifier: GPL-3.0-or-later
// The one place that names every AI implementation. AI.cpp creates and loads
// controllers through it, so code that only drives AI (the game, the pipeline)
// does not link every AI by itself; a test binary can link a smaller registry.
#include "AIRegistry.h"
#include "AIJavaScript.h"
#include "AIMaxima.h"
#include "AINames.h"
#include "AINicowar.h"
#include "AINull.h"
#include "AINumbi.h"
#include "AIWarrush.h"
#include "CompileProfile.h"
#include "shared_runtime/Runtime.h"
#include <Stream.h>
#include <cstdio>
#include <stdexcept>
// compile_profile=core leaves some AIs out (scons/sources.py); ids never change.
#if __has_include(<glob2/CompileProfile.h>)
#include <glob2/CompileProfile.h>
#else
#define GLOB2_PROFILE_HAS_AI_CASTOR 1
#define GLOB2_PROFILE_HAS_AI_CORTEX 1
#define GLOB2_PROFILE_HAS_AI_CABINO 1
#endif
#if GLOB2_PROFILE_HAS_AI_CABINO
#include "AICabino.h"
#endif
#if GLOB2_PROFILE_HAS_AI_CASTOR
#include "AICastor.h"
#endif
#if GLOB2_PROFILE_HAS_AI_CORTEX
#include "cortex/AICortex.h"
#include "cortex/CortexFoodSources.h"
#include "cortex/CortexTuning.h"
#endif

namespace AIRegistry
{
bool available(int implementationID)
{
	switch (implementationID)
	{
		case AI::NONE: case AI::NUMBI: case AI::WARRUSH: case AI::ECONO: case AI::NICOWAR: case AI::MAXIMA:
		case AI::JAVASCRIPT:
			return true;
		case AI::CASTOR: return GLOB2_PROFILE_HAS_AI_CASTOR;
		case AI::CORTEX: return GLOB2_PROFILE_HAS_AI_CORTEX;
		case AI::CABINO: return GLOB2_PROFILE_HAS_AI_CABINO;
		default: return false;
	}
}

std::string unavailableMessage(int implementationID)
{
	return CompileProfile::missing("The " + AINames::getCLIName(implementationID) + " AI (id " +
								   std::to_string(implementationID) + ")");
}

namespace
{
void requireAvailable(AI::ImplementationID implementationID)
{
	if (implementationID >= 0 && implementationID < AI::SIZE && !available(implementationID))
		throw std::runtime_error(unavailableMessage(implementationID));
}
}

AIImplementation* create(AI::ImplementationID implementationID, Player *player)
{
	requireAvailable(implementationID);
	switch (implementationID)
	{
		case AI::NONE:
			return new AINull();
		case AI::NUMBI:
			return new AINumbi(player);
#if GLOB2_PROFILE_HAS_AI_CASTOR
		case AI::CASTOR:
			return new AICastor(player);
#endif
		case AI::NICOWAR:
			return new AISharedRuntime::Runtime(new NewNicowar, player);
		case AI::WARRUSH:
			return new AIWarrush(player);
		case AI::ECONO:
			return new AISharedRuntime::Runtime(new AISharedRuntime::Econo, player);
		case AI::MAXIMA:
			return new AIMaxima::Maxima(player);
#if GLOB2_PROFILE_HAS_AI_CORTEX
		case AI::CORTEX:
			return new AICortex(player);
#endif
#if GLOB2_PROFILE_HAS_AI_CABINO
		case AI::CABINO:
			return new Cabino::AICabino(player);
#endif
		case AI::JAVASCRIPT:
			return new AIJavaScript(player);
		default:
			throw std::runtime_error("Unknown AI implementation");
	}
}

Loaded load(AI::ImplementationID implementationID, GAGCore::InputStream *stream, Player *player, Sint32 versionMinor,
	AIImplementation *&implementation)
{
	requireAvailable(implementationID);
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
#if GLOB2_PROFILE_HAS_AI_CASTOR
		case AI::CASTOR:
			implementation=new AICastor(player);
			if (!implementation->load(stream, player, versionMinor))
			{
				fprintf(stderr, "AI::load: AICastor load failed\n");
				return Loaded::FailedInSection;
			}
		break;
#endif
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
#if GLOB2_PROFILE_HAS_AI_CORTEX
		case AI::CORTEX:
			implementation=new AICortex(stream, player, versionMinor);
		break;
#endif
#if GLOB2_PROFILE_HAS_AI_CABINO
		case AI::CABINO:
			implementation=new Cabino::AICabino(stream, player, versionMinor);
		break;
#endif
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

bool applyTuning(int implementationID, const std::string &overrides, std::string &config)
{
	if (implementationID != AI::CORTEX)
		return false;
#if GLOB2_PROFILE_HAS_AI_CORTEX
	Cortex::CortexTuning values;
	std::string error;
	if (!Cortex::applyTuning(values, overrides, error))
		throw std::invalid_argument(error);
	config = Cortex::tuningValues(values);
	return true;
#else
	(void)overrides; (void)config;
	throw std::runtime_error(unavailableMessage(implementationID));
#endif
}

std::string tuningSchemaJson(int implementationID)
{
#if GLOB2_PROFILE_HAS_AI_CORTEX
	if (implementationID == AI::CORTEX)
		return Cortex::tuningSchemaJson();
#else
	(void)implementationID;
#endif
	return "";
}

int dumpWheatPlan(const std::string &mapName, int team)
{
#if GLOB2_PROFILE_HAS_AI_CORTEX
	return Cortex::dumpWheatPlan(mapName, team);
#else
	(void)mapName; (void)team;
	throw std::runtime_error(unavailableMessage(AI::CORTEX));
#endif
}
}
