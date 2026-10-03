// SPDX-License-Identifier: GPL-3.0-or-later
// Access relaxation is confined to this test translation unit by SCons.
#include "EngineFixtures.h"
#include <string>
#include <memory>
#include <iostream>
#include "GlobalContainer.h"
#include "Game.h"
#include "Player.h"
#include "GenerationService.h"
#include "ai/cortex/AICortex.h"
#include "AIMaxima.h"
#include "AIMaximaStrategy.h"
#include "Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <stdexcept>

namespace
{
static void require(bool value,const char* label)
{
	if(!value) throw std::runtime_error(label);
	std::cout << "PASS " << label << '\n';
}
}

TEST_SUITE("TournamentCompatibility")
{
	TEST_CASE("per-player AI configuration survives construction and partial headers")
	{
		glob2test::HeadlessGlobals globals(glob2test::GlobalsOptions{.loadStrings = true});
		Game game(nullptr);
		GenerationRequest request;request.setMethodDefaults(15);request.seed=42;request.nbTeams=4;
		require(bool(GenerationService().generate(game,request)),"fixture generation");
		GameHeader header;header.setNumberOfPlayers(4);header.setRandomSeed(19);
		for(int p=0;p<4;++p)
		{
			header.getBasePlayer(p)=BasePlayer(p,"test",p,BasePlayer::playerTypeFromImplementationID(p<2?AI::CORTEX:AI::MAXIMA));
			header.setAllyTeamNumber(p,p<2?1:2);
		}
		for(int p=0;p<2;++p)
		{
			Cortex::CortexTuning values;values.swarmWorkerCap=p?7:4;
			header.setAIConfig(p,Cortex::tuningValues(values));
		}
		for(int p=2;p<4;++p)
		{
			AIMaxima::StrategyConfigOptions options;
			options.inlineOverrides="staffing.new_inn_workers="+std::to_string(p+1);
			AIMaxima::ResolvedStrategy values;std::string error;
			require(AIMaxima::StrategyResolver::resolve(options,&header,values,error),"resolve Maxima per player");
			header.setAIConfig(p,AIMaxima::StrategyResolver::canonicalValues(values.values));
		}
		game.setGameHeader(header);
		for(int p=0;p<2;++p)
		{
			auto* cortex=dynamic_cast<AICortex*>(game.players[p]->ai->aiImplementation);
			require(cortex && cortex->runtimeTuning.swarmWorkerCap==(p?7:4),"constructed Cortex instance uses its own configuration");
		}
		for(int p=2;p<4;++p)
		{
			auto* maxima=dynamic_cast<AIMaxima::Maxima*>(game.players[p]->ai->aiImplementation);
			maxima->ensure_strategy();
			require(maxima->strategy.staffing.new_inn_workers==p+1,"Maxima instances retain different resolved values");
		}
		for(int form=0;form<2;++form)
		{
			GAGCore::MemoryStreamBackend* memory=new GAGCore::MemoryStreamBackend;
			GAGCore::BinaryOutputStream out(memory);
			if(form==0) header.saveWithoutPlayerInfo(&out); else header.savePlayerInfo(&out);
			out.flush();memory->seekFromStart(0);
			GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(*memory));
			GameHeader received;
			require(form==0 ? received.loadWithoutPlayerInfo(&in,VERSION_MINOR) : received.loadPlayerInfo(&in,VERSION_MINOR),"partial header loads");
			for(int p=0;p<4;++p)require(received.getAIConfig(p)==header.getAIConfig(p),"partial header preserves resolved configuration");
		}
		Cortex::CortexTuning values;std::string error;
		require(!Cortex::applyTuning(values,"tierMidDiv=0",error),"invalid Cortex divisor rejected");
		require(!Cortex::applyTuning(values,"unknown=1",error),"unknown Cortex parameter rejected");
	}
}
