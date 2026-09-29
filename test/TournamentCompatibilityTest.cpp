// SPDX-License-Identifier: GPL-3.0-or-later
// Access relaxation is confined to this test translation unit by SCons.
#include "GlobalContainer.h"
#include "Game.h"
#include "Player.h"
#include "GenerationService.h"
#include "GameHeaderMessages.h"
#include "ai/cortex/AICortex.h"
#include "AIMaxima.h"
#include "AIMaximaStrategy.h"
#include "YOGConsts.h"
#include "Version.h"
#include <BinaryStream.h>
#include <StreamBackend.h>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer=nullptr;
static void require(bool value,const char* label)
{
	if(!value) throw std::runtime_error(label);
	std::cout << "PASS " << label << '\n';
}
int main(int, char**)
{
	GlobalContainer globals("glob2-tournament-compatibility");globalContainer=&globals;
	globals.runNoX=true;globals.load();
	try
	{
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
			std::unique_ptr<NetMessage> sent(form==0 ? static_cast<NetMessage*>(new NetSendGameHeader(header))
				: static_cast<NetMessage*>(new NetSendGamePlayerInfo(header)));
			sent->encodeData(&out);out.flush();memory->seekFromStart(0);
			GAGCore::BinaryInputStream in(new GAGCore::MemoryStreamBackend(*memory));
			GameHeader received;
			if(form==0){NetSendGameHeader message;message.decodeData(&in);message.downloadToGameHeader(received);}
			else{NetSendGamePlayerInfo message;message.decodeData(&in);message.downloadToGameHeader(received);}
			for(int p=0;p<4;++p)require(received.getAIConfig(p)==header.getAIConfig(p),"network partial header preserves resolved configuration");
		}
		require(!isSupportedYOGClientVersion(NET_PROTOCOL_VERSION-1),"old client rejected at protocol boundary");
		require(isSupportedYOGClientVersion(NET_PROTOCOL_VERSION),"current client accepted at protocol boundary");
		require(!isSupportedYOGClientVersion(NET_PROTOCOL_VERSION+1),"future client rejected at protocol boundary");
		Cortex::CortexTuning values;std::string error;
		require(!Cortex::applyTuning(values,"tierMidDiv=0",error),"invalid Cortex divisor rejected");
		require(!Cortex::applyTuning(values,"unknown=1",error),"unknown Cortex parameter rejected");
		return 0;
	}
	catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
