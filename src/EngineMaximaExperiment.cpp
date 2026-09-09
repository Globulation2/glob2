#include "Engine.h"
#include "MaximaExperimentAudit.h"
#include <sstream>
#include "AI.h"
#include "AINames.h"
#include "AIMaximaFarming.h"
#include "AIMaximaFoodSupply.h"
#include "Building.h"
#include "BuildingType.h"
#include "GlobalContainer.h"
#include "GameGUI.h"
#include "Player.h"
#include "Unit.h"
#include "Utilities.h"
#include "FileManager.h"
#include "Toolkit.h"
#include "BinaryStream.h"
#include "StringTable.h"
#include "MapHeader.h"
#include "IntBuildingType.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <map>
#include <memory>
using namespace GAGCore;
using namespace Utilities;
void Engine::createNicowarVersionTestGame()
{
	MapHeader map;
	do
	{
		const auto selected=chooseRandomMap();
		if(!selected) throw std::runtime_error("No eligible tournament map");
		map=*selected;
	}
	while(map.getNumberOfTeams() != 4);

	std::cout<<"Nicowar version test map: "<<map.getMapName()<<std::endl;

	GameHeader game = createNicowarVersionTestGame(map.getNumberOfTeams());
	std::cout<<"Random Seed gameheader: "<<game.getRandomSeed()<<std::endl;
	for (int p=0; p<game.getNumberOfPlayers(); p++)
	{
		std::cout<<"    Player: "<<game.getBasePlayer(p).name<<" for team "<<game.getBasePlayer(p).teamNumber<<std::endl;
	}

	// Maxima is used as the observed team when a GUI is present.
	gui.localPlayer=0;
	gui.localTeamNo=game.getBasePlayer(0).teamNumber;

	initGame(map, game);
}



int Engine::createNicowarVersionTournamentGame(const std::string& mapFile, Uint32 seed, int rotation)
{
	MapHeader map=loadMapHeader(mapFile);
	if(map.getNumberOfTeams()!=4)
	{
		std::cerr<<"Tournament map must contain exactly four teams: "<<mapFile<<std::endl;
		return EE_CANT_LOAD_MAP;
	}

	GameHeader game=createNicowarVersionTestGame(map.getNumberOfTeams(), rotation);
	game.setRandomSeed(seed);
	// Visible farming-policy inspection needs the same fully discovered map for
	// the AIs that the reveal switch presents to the spectator.  Otherwise the
	// GUI can show coastlines that Maxima is not yet allowed to classify.
	game.setMapDiscovered(globalContainer->nicowarTournamentRevealMap);
	gui.localPlayer=0;
	gui.localTeamNo=game.getBasePlayer(0).teamNumber;
	return initGame(map, game, true, false, false, mapFile);
}


namespace
{
	bool isResearchAI(int ai)
	{
		return ai==AI::NUMBI || ai==AI::CASTOR
			|| ai==AI::NICOWAR || ai==AI::MAXIMA;
	}

	bool nicowar2v2IsSideA(int team, int partition, int swap)
	{
		static const int sideAPositions[3][2] = {
			{0, 1}, {0, 2}, {0, 3}
		};
		bool sideA=team==sideAPositions[partition][0]
			|| team==sideAPositions[partition][1];
		return swap ? !sideA : sideA;
	}
}


int Engine::createNicowar2v2TournamentGame(const std::string& mapFile,
	Uint32 seed, int aiA, int aiB, int partition, int swap)
{
	MapHeader map=loadMapHeader(mapFile);
	if(map.getNumberOfTeams()!=4 || !isResearchAI(aiA)
	   || !isResearchAI(aiB)
	   || partition<0 || partition>=3 || (swap!=0 && swap!=1))
	{
		std::cerr<<"Invalid independent-agent 2v2 tournament setup: "
			<<mapFile<<std::endl;
		return EE_CANT_LOAD_MAP;
	}

	GameHeader game;
	for(int team=0; team<4; ++team)
	{
		const bool sideA=nicowar2v2IsSideA(team, partition, swap);
		AI::ImplementationID iid=static_cast<AI::ImplementationID>(sideA ? aiA : aiB);
		game.getBasePlayer(team)=BasePlayer(team, AINames::getAIText(iid), team,
			Player::playerTypeFromImplementationID(iid));
		game.setAllyTeamNumber(team, sideA ? 1 : 2);
	}
	game.setAllyTeamsFixed(true);
	game.setNumberOfPlayers(4);
	game.setRandomSeed(seed);

	gui.localPlayer=0;
	gui.localTeamNo=game.getBasePlayer(0).teamNumber;
	// initGame applies the same allied normal-vision sharing as a regular team
	// game. The players still keep separate AI instances and strategy state.
	return initGame(map, game, true, false, false, mapFile);
}


int Engine::createNicowarScenarioGame(const std::string& mapFile, Uint32 seed,
	int players, int candidateAi, int opponentAi, int candidateSeat,
	int positionOffset)
{
	MapHeader map=loadMapHeader(mapFile);
	const int mapTeams=map.getNumberOfTeams();
	if(players<2 || players>5 || mapTeams<players
	   || !isResearchAI(candidateAi)
	   || !isResearchAI(opponentAi)
	   || candidateSeat<0 || candidateSeat>=players
	   || positionOffset<0 || positionOffset>=mapTeams)
	{
		std::cerr<<"Invalid focal Nicowar scenario setup: "<<mapFile<<std::endl;
		return EE_CANT_LOAD_MAP;
	}

	GameHeader game;
	for(int player=0; player<players; ++player)
	{
		// Spread a subset evenly across maps with more starts than players. Offset
		// rotations expose the candidate to different local neighborhoods.
		const int team=(positionOffset+(player*mapTeams)/players)%mapTeams;
		AI::ImplementationID iid=static_cast<AI::ImplementationID>(
			player==candidateSeat ? candidateAi : opponentAi);
		game.getBasePlayer(player)=BasePlayer(player, AINames::getAIText(iid), team,
			Player::playerTypeFromImplementationID(iid));
		// Match GameHeader's one-based defaults for unused map starts. Using
		// team here could accidentally ally an active seat with an unused one.
		game.setAllyTeamNumber(team, team+1);
	}
	game.setAllyTeamsFixed(true);
	game.setNumberOfPlayers(players);
	game.setRandomSeed(seed);
	// A visible revealed-map scenario is also a topology inspection run: expose
	// the same terrain to the AIs that the renderer shows to the observer.
	game.setMapDiscovered(globalContainer->nicowarTournamentRevealMap);

	gui.localPlayer=0;
	gui.localTeamNo=game.getBasePlayer(0).teamNumber;
	return initGame(map, game, true, false, false, mapFile);
}


int Engine::createMaximaCastorGame(const std::string& mapFile, Uint32 seed, int maximaTeam, int castorTeam)
{
	MapHeader map=loadMapHeader(mapFile);
	if(maximaTeam<0 || castorTeam<0 || maximaTeam>=map.getNumberOfTeams()
	   || castorTeam>=map.getNumberOfTeams() || maximaTeam==castorTeam)
	{
		std::cerr<<"Maxima/Castor smoke teams must be distinct valid map teams: "<<mapFile<<std::endl;
		return EE_CANT_LOAD_MAP;
	}

	GameHeader game;
	game.getBasePlayer(0)=BasePlayer(0, AINames::getAIText(AI::MAXIMA),
		maximaTeam, Player::playerTypeFromImplementationID(AI::MAXIMA));
	game.getBasePlayer(1)=BasePlayer(1, AINames::getAIText(AI::CASTOR),
		castorTeam, Player::playerTypeFromImplementationID(AI::CASTOR));
	game.setAllyTeamNumber(maximaTeam, maximaTeam+1);
	game.setAllyTeamNumber(castorTeam, castorTeam+1);
	game.setNumberOfPlayers(2);
	game.setRandomSeed(seed);

	gui.localPlayer=0;
	gui.localTeamNo=maximaTeam;
	return initGame(map, game);
}


void Engine::printMaximaCastorResult()
{
	Game& game=gui.game;
	Team* prestigeWinner=game.totalPrestigeReached ? game.getTeamWithMostPrestige() : NULL;
	const bool completed=game.isGameEnded || game.totalPrestigeReached;
	std::cout<<"MAXIMA_CASTOR_RESULT\t"
		<<game.gameHeader.getRandomSeed()<<"\t"
		<<game.mapHeader.getMapName()<<"\t"
		<<game.stepCounter<<"\t"
		<<(completed ? "completed" : "step_limit")<<std::endl;
	for(int playerIndex=0; playerIndex<game.gameHeader.getNumberOfPlayers(); ++playerIndex)
	{
		const BasePlayer& player=game.gameHeader.getBasePlayer(playerIndex);
		Team* team=game.teams[player.teamNumber];
		TeamStat* stats=team->stats.getLatestStat();
		const bool won=team->hasWon || team==prestigeWinner;
		std::cout<<"MAXIMA_CASTOR_PLAYER\t"
			<<player.name<<"\t"<<player.teamNumber<<"\t"
			<<(won ? 1 : 0)<<"\t"<<(team->isAlive ? 1 : 0)<<"\t"
			<<stats->totalUnit<<"\t"
			<<stats->numberUnitPerType[WORKER]<<"\t"
			<<stats->numberUnitPerType[EXPLORER]<<"\t"
			<<stats->numberUnitPerType[WARRIOR]<<"\t"
			<<stats->totalBuilding<<"\t"
			<<team->prestige<<std::endl;
	}
}



void Engine::printNicowarVersionTournamentResult(int rotation)
{
	Game& game=gui.game;
	Team* prestigeWinner=NULL;
	if(game.totalPrestigeReached)
		prestigeWinner=game.getTeamWithMostPrestige();

	const bool completed=game.isGameEnded || game.totalPrestigeReached;
	std::cout<<"NICOWAR_MATCH_RESULT\t"
		<<game.gameHeader.getRandomSeed()<<"\t"
		<<game.mapHeader.getMapName()<<"\t"
		<<game.stepCounter<<"\t"
		<<rotation<<"\t"
		<<(completed ? "completed" : "timeout")<<std::endl;

	for(int p=0; p<game.gameHeader.getNumberOfPlayers(); ++p)
	{
		const BasePlayer& player=game.gameHeader.getBasePlayer(p);
		Team* team=game.teams[player.teamNumber];
		TeamStat* stats=team->stats.getLatestStat();
		const bool won=team->hasWon || team==prestigeWinner;
		std::cout<<"NICOWAR_PLAYER_RESULT\t"
			<<player.name<<"\t"
			<<player.teamNumber<<"\t"
			<<(won ? 1 : 0)<<"\t"
			<<(team->hasLost ? 1 : 0)<<"\t"
			<<(team->isAlive ? 1 : 0)<<"\t"
			<<stats->totalUnit<<"\t"
			<<stats->totalBuilding<<"\t"
			<<team->prestige<<std::endl;
	}
}


void Engine::printNicowar2v2TournamentResult(int aiA, int aiB,
	int partition, int swap)
{
	Game& game=gui.game;
	Team* prestigeWinner=game.totalPrestigeReached
		? game.getTeamWithMostPrestige() : NULL;
	bool sideAWon=false;
	bool sideBWon=false;
	for(int team=0; team<4; ++team)
	{
		const bool won=game.teams[team]->hasWon || game.teams[team]==prestigeWinner;
		if(won && nicowar2v2IsSideA(team, partition, swap))
			sideAWon=true;
		else if(won)
			sideBWon=true;
	}
	const bool completed=game.isGameEnded || game.totalPrestigeReached;
	const char* winner=sideAWon && !sideBWon ? "A"
		: (sideBWon && !sideAWon ? "B" : "draw");
	std::cout<<"NICOWAR_2V2_MATCH_RESULT\t"
		<<game.gameHeader.getRandomSeed()<<"\t"
		<<game.mapHeader.getMapName()<<"\t"
		<<game.stepCounter<<"\t"<<partition<<"\t"<<swap<<"\t"
		<<(completed ? "completed" : "timeout")<<"\t"
		<<aiA<<"\t"<<aiB<<"\t"<<winner<<std::endl;

	for(int p=0; p<game.gameHeader.getNumberOfPlayers(); ++p)
	{
		const BasePlayer& player=game.gameHeader.getBasePlayer(p);
		Team* team=game.teams[player.teamNumber];
		TeamStat* stats=team->stats.getLatestStat();
		const bool sideA=nicowar2v2IsSideA(player.teamNumber, partition, swap);
		const bool won=sideA ? sideAWon : sideBWon;
		std::cout<<"NICOWAR_2V2_PLAYER_RESULT\t"
			<<(sideA ? "A" : "B")<<"\t"
			<<player.name<<"\t"<<player.teamNumber<<"\t"
			<<(won ? 1 : 0)<<"\t"<<(team->hasLost ? 1 : 0)<<"\t"
			<<(team->isAlive ? 1 : 0)<<"\t"<<stats->totalUnit<<"\t"
			<<stats->totalBuilding<<"\t"<<team->prestige<<std::endl;
	}
}


void Engine::printNicowarScenarioResult(int candidateAi, int opponentAi,
	int candidateSeat, int positionOffset)
{
	Game& game=gui.game;
	Team* prestigeWinner=game.totalPrestigeReached
		? game.getTeamWithMostPrestige() : NULL;
	const bool completed=game.isGameEnded || game.totalPrestigeReached;
	std::cout<<"NICOWAR_SCENARIO_MATCH_RESULT\t"
		<<game.gameHeader.getRandomSeed()<<"\t"
		<<game.mapHeader.getMapName()<<"\t"
		<<game.stepCounter<<"\t"<<game.gameHeader.getNumberOfPlayers()<<"\t"
		<<candidateAi<<"\t"<<opponentAi<<"\t"<<candidateSeat<<"\t"
		<<positionOffset<<"\t"<<(completed ? "completed" : "timeout")
		<<std::endl;

	for(int player=0; player<game.gameHeader.getNumberOfPlayers(); ++player)
	{
		const BasePlayer& base=game.gameHeader.getBasePlayer(player);
		Team* team=game.teams[base.teamNumber];
		TeamStat* stats=team->stats.getLatestStat();
		const bool won=team->hasWon || team==prestigeWinner;
		std::cout<<"NICOWAR_SCENARIO_PLAYER_RESULT\t"
			<<(player==candidateSeat ? "candidate" : "opponent")<<"\t"
			<<base.name<<"\t"<<player<<"\t"<<base.teamNumber<<"\t"
			<<(won ? 1 : 0)<<"\t"<<(team->hasLost ? 1 : 0)<<"\t"
			<<(team->isAlive ? 1 : 0)<<"\t"<<stats->totalUnit<<"\t"
			<<stats->totalBuilding<<"\t"<<team->prestige<<std::endl;
	}
}



void Engine::printNicowarScoreTelemetry()
{
	Game& game=gui.game;
	for(int p=0; p<game.gameHeader.getNumberOfPlayers(); ++p)
	{
		const BasePlayer& player=game.gameHeader.getBasePlayer(p);
		Team* team=game.teams[player.teamNumber];
		TeamStat* stat=team->stats.getLatestStat();
		int buildingSites=0;
		for(int i=0; i<Building::MAX_COUNT; ++i)
		{
			Building* building=team->myBuildings[i];
			if(building && building->type->isBuildingSite)
				buildingSites+=1;
		}
		std::cout<<"NICOWAR_SCORE_TELEMETRY\t"<<game.stepCounter
			<<"\t"<<player.teamNumber<<"\t"<<player.name
			<<"\talive="<<(team->isAlive ? 1 : 0)
			<<"\twon="<<(team->hasWon ? 1 : 0)
			<<"\tlost="<<(team->hasLost ? 1 : 0)
			<<"\tpopulation="<<stat->totalUnit
			<<"\tworkers="<<stat->numberUnitPerType[WORKER]
			<<"\texplorers="<<stat->numberUnitPerType[EXPLORER]
			<<"\twarriors="<<stat->numberUnitPerType[WARRIOR]
			<<"\tbuildings="<<stat->totalBuilding
			<<"\tbuilding_sites="<<buildingSites
			<<"\tfood="<<stat->totalFood
			<<"\tfood_capacity="<<stat->totalFoodCapacity
			<<"\ttotal_hp="<<stat->totalHP
			<<"\tattack_power="<<stat->totalAttackPower
			<<"\tdefense_power="<<stat->totalDefensePower
			<<"\tprestige="<<team->prestige
			<<"\tmap_width="<<game.map.getW()
			<<"\tmap_height="<<game.map.getH()<<std::endl;
	}
}



void Engine::printNicowarObserverTelemetry()
{
	Game& game=gui.game;
	// Capacity is sampled every 1,000 ticks; -1 below means not sampled.
	// The same path/fertility calculation serves every AI, without RNG or orders.
	const bool sampleFood=game.stepCounter==1 || game.stepCounter%1000==0;
	if(sampleFood && (game.stepCounter==1
	   || !observerFoodFertility.validFor(game.map.getW(),game.map.getH()))) {
		const int w=game.map.getW(),h=game.map.getH();
		std::vector<Uint8> water(w*h),sand(w*h);
		for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
			const Uint16 terrain=game.map.getTile(x,y).terrain;
			water[y*w+x]=terrain>=256 && terrain<272;
			sand[y*w+x]=terrain>=128 && terrain<144;
		}
		observerFoodFertility.rebuild(w,h,water,sand);
	}
	for(int p=0; p<game.gameHeader.getNumberOfPlayers(); ++p)
	{
		const BasePlayer& player=game.gameHeader.getBasePlayer(p);
		Team* team=game.teams[player.teamNumber];
		TeamStat* stat=team->stats.getLatestStat();
		const bool collectMaximaDetail=player.type>=BasePlayer::P_AI
			&& BasePlayer::implementationIdFromPlayerType(player.type)==AI::MAXIMA;
		int buildingSites=0;
		int assignedWorkerSlots=0;
		int workingUnits=0;
		int constructionAssigned=0;
		int constructionWorking=0;
		int innAssigned=0;
		int innWorking=0;
		int innCorn=0;
		int swarmAssigned=0;
		int swarmWorking=0;
		int swarmCorn=0;
		int swarmEmpty=0;
		int swarmProductionTimeout=0;
		int swarmCornDistance=0;
		int swarmCornReachable=0;
		int technologyAssigned=0;
		int technologyWorking=0;
		int militaryAssigned=0;
		int militaryWorking=0;
		int warFlagAssigned=0;
		int warFlagUnits=0;
		int explorationFlagAssigned=0;
		int explorationFlagUnits=0;
		int clearingFlagAssigned=0;
		int clearingFlagUnits=0;
		int buildingsUnderAttack=0;
		std::vector<Building*> colonyBuildings;
		std::vector<Building*> teamWarFlags;
		std::vector<Building*> teamTowers;
		for(int i=0; i<Building::MAX_COUNT; ++i)
		{
			Building* building=team->myBuildings[i];
			if(!building)
				continue;
			if(building->type->isBuildingSite)
			{
				buildingSites+=1;
				constructionAssigned+=building->maxUnitWorking;
				constructionWorking+=building->unitsWorking.size();
			}
			if(building->underAttackTimer)
				buildingsUnderAttack+=1;
			workingUnits+=building->unitsWorking.size();
			const int type=building->type->shortTypeNum;
			if(type==IntBuildingType::WAR_FLAG)
			{
				warFlagAssigned+=building->maxUnitWorking;
				warFlagUnits+=building->unitsWorking.size();
				teamWarFlags.push_back(building);
			}
			else if(type==IntBuildingType::EXPLORATION_FLAG)
			{
				explorationFlagAssigned+=building->maxUnitWorking;
				explorationFlagUnits+=building->unitsWorking.size();
			}
			else if(type==IntBuildingType::CLEARING_FLAG)
			{
				clearingFlagAssigned+=building->maxUnitWorking;
				clearingFlagUnits+=building->unitsWorking.size();
			}
			else if(!building->type->isVirtual && !building->type->isBuildingSite)
			{
				assignedWorkerSlots+=building->maxUnitWorking;
				colonyBuildings.push_back(building);
				if(type==IntBuildingType::FOOD_BUILDING)
				{
					innAssigned+=building->maxUnitWorking;
					innWorking+=building->unitsWorking.size();
					innCorn+=building->resources[CORN];
				}
				else if(type==IntBuildingType::SWARM_BUILDING)
				{
					swarmAssigned+=building->maxUnitWorking;
					swarmWorking+=building->unitsWorking.size();
					swarmCorn+=building->resources[CORN];
					swarmEmpty+=building->resources[CORN]
						<building->type->resourceForOneUnit ? 1 : 0;
					swarmProductionTimeout+=building->productionTimeout;
					int cornDistance=0;
					if(game.map.resourceAvailable(player.teamNumber,CORN,false,
						building->posX,building->posY,&cornDistance))
					{
						swarmCornDistance+=cornDistance;
						swarmCornReachable+=1;
					}
				}
				else if(type==IntBuildingType::WALKSPEED_BUILDING
					|| type==IntBuildingType::SWIMSPEED_BUILDING
					|| type==IntBuildingType::SCIENCE_BUILDING)
				{
					technologyAssigned+=building->maxUnitWorking;
					technologyWorking+=building->unitsWorking.size();
				}
				else if(type==IntBuildingType::HEAL_BUILDING
					|| type==IntBuildingType::ATTACK_BUILDING
					|| type==IntBuildingType::DEFENSE_BUILDING)
				{
					militaryAssigned+=building->maxUnitWorking;
					militaryWorking+=building->unitsWorking.size();
				}
				if(type==IntBuildingType::DEFENSE_BUILDING)
					teamTowers.push_back(building);
			}
		}
		long long localFoodCapacity=-1;
		if(sampleFood) {
			bool canSwim=false;
			for(int i=0;i<Unit::MAX_COUNT;++i) {
				Unit* unit=team->myUnits[i];
				if(unit && unit->typeNum==WORKER && unit->performance[SWIM]>0)
					canSwim=true;
			}
			// Nicowar's forbidden areas are its crop-protection mask (see
			// update_farming). Include their productive land just as Maxima's
			// supply calculation includes its own protected crops.
			std::vector<Uint8> cropProtection(game.map.getW()*game.map.getH(),0);
			const bool nicowar=player.type>=BasePlayer::P_AI
				&& BasePlayer::implementationIdFromPlayerType(player.type)==AI::NICOWAR;
			if(nicowar)
				for(int y=0;y<game.map.getH();++y) for(int x=0;x<game.map.getW();++x)
					cropProtection[y*game.map.getW()+x]=
						(game.map.getTile(x,y).forbidden&team->me)!=0;
			std::set<int> seen;
			localFoodCapacity=0;
			for(auto building:colonyBuildings)
				if(building->type->shortTypeNum==IntBuildingType::FOOD_BUILDING
				   || building->type->shortTypeNum==IntBuildingType::SWARM_BUILDING)
					localFoodCapacity+=AIMaxima::reachableFoodCapacity(&game.map,
						building,team->me,canSwim,12,observerFoodFertility,&cropProtection,&seen);
			localFoodCapacity/=65536;
		}
		int towerCoveredBuildings=-1;
		if(collectMaximaDetail)
		{
			towerCoveredBuildings=0;
			for(std::vector<Building*>::const_iterator building=colonyBuildings.begin();
				building!=colonyBuildings.end(); ++building)
			{
				if((*building)->type->shortTypeNum==IntBuildingType::DEFENSE_BUILDING)
					continue;
				for(std::vector<Building*>::const_iterator tower=teamTowers.begin();
					tower!=teamTowers.end(); ++tower)
					if(game.map.warpDistSquare((*building)->posX, (*building)->posY,
						(*tower)->posX, (*tower)->posY)<=100)
					{
						towerCoveredBuildings+=1;
						break;
					}
			}
		}

		int enemyWarriorsNearColony=-1;
		int enemyWarriorsNearWarFlags=-1;
		int enemyWarriorsNearTowers=-1;
		if(collectMaximaDetail)
		{
			enemyWarriorsNearColony=0;
			enemyWarriorsNearWarFlags=0;
			enemyWarriorsNearTowers=0;
			for(int enemyTeam=0; enemyTeam<Team::MAX_COUNT; ++enemyTeam)
			{
				if(enemyTeam==player.teamNumber || !game.teams[enemyTeam]
				   || !(team->enemies & (1<<enemyTeam)))
					continue;
				for(int i=0; i<Unit::MAX_COUNT; ++i)
				{
					Unit* enemy=game.teams[enemyTeam]->myUnits[i];
					if(!enemy || enemy->typeNum!=WARRIOR)
						continue;
					for(std::vector<Building*>::const_iterator building=colonyBuildings.begin();
						building!=colonyBuildings.end(); ++building)
						if(game.map.warpDistSquare(enemy->posX, enemy->posY,
							(*building)->posX, (*building)->posY)<=144)
						{
							enemyWarriorsNearColony+=1;
							break;
						}
					for(std::vector<Building*>::const_iterator flag=teamWarFlags.begin();
						flag!=teamWarFlags.end(); ++flag)
						if(game.map.warpDistSquare(enemy->posX, enemy->posY,
							(*flag)->posX, (*flag)->posY)<=100)
						{
							enemyWarriorsNearWarFlags+=1;
							break;
						}
					for(std::vector<Building*>::const_iterator tower=teamTowers.begin();
						tower!=teamTowers.end(); ++tower)
						if(game.map.warpDistSquare(enemy->posX, enemy->posY,
							(*tower)->posX, (*tower)->posY)<=100)
						{
							enemyWarriorsNearTowers+=1;
							break;
						}
				}
			}
		}

		int unitsUnderAttack=0;
		int workersUnderAttack=0;
		int warriorsUnderAttack=0;
		int warriorsAttacking=0;
		int warriorsFlagged=0;
		int combatReadyWarriors=0;
		int swimUpgradedExplorers=0;
		int amphibiousAttackExplorers=0;
		for(int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit* unit=team->myUnits[i];
			if(!unit)
				continue;
			if(unit->underAttackTimer)
			{
				unitsUnderAttack+=1;
				if(unit->typeNum==WORKER)
					workersUnderAttack+=1;
				else if(unit->typeNum==WARRIOR)
					warriorsUnderAttack+=1;
			}
			if(unit->typeNum==EXPLORER)
			{
				if(unit->level[SWIM]>=1)
					swimUpgradedExplorers+=1;
				if(unit->performance[SWIM]>0
				   && unit->performance[MAGIC_ATTACK_GROUND]>0)
					amphibiousAttackExplorers+=1;
			}
			if(unit->typeNum!=WARRIOR)
				continue;
			if(std::min(unit->level[ATTACK_SPEED], unit->level[ATTACK_STRENGTH])>=1)
				combatReadyWarriors+=1;
			if(unit->movement==Unit::MOV_ATTACKING_TARGET)
				warriorsAttacking+=1;
			if(unit->activity==Unit::ACT_FLAG)
				warriorsFlagged+=1;
		}

		int attackUpgradedWarriors=0;
		int swimUpgradedWarriors=0;
		int buildUpgradedWorkers=0;
		int groundAttackExplorers=0;
		for(int level=1; level<NB_UNIT_LEVELS; ++level)
		{
			attackUpgradedWarriors+=stat->upgradeStatePerType[WARRIOR][ATTACK_STRENGTH][level];
			swimUpgradedWarriors+=stat->upgradeStatePerType[WARRIOR][SWIM][level];
			buildUpgradedWorkers+=stat->upgradeStatePerType[WORKER][BUILD][level];
			groundAttackExplorers+=stat->upgradeStatePerType[EXPLORER][MAGIC_ATTACK_GROUND][level];
		}
		int level1Buildings=0;
		int level2Buildings=0;
		int level3Buildings=0;
		for(int type=0; type<IntBuildingType::NB_BUILDING; ++type)
		{
			level1Buildings+=stat->numberBuildingPerTypePerLevel[type][1];
			level2Buildings+=stat->numberBuildingPerTypePerLevel[type][3];
			level3Buildings+=stat->numberBuildingPerTypePerLevel[type][5];
		}

		std::cout<<"NICOWAR_OBSERVER_TELEMETRY\t"<<game.stepCounter
			<<"\t"<<player.teamNumber
			<<"\t"<<player.name
			<<"\talive="<<(team->isAlive ? 1 : 0)
			<<"\twon="<<(team->hasWon ? 1 : 0)
			<<"\tlost="<<(team->hasLost ? 1 : 0)
			<<"\tpopulation="<<stat->totalUnit
			<<"\tworkers="<<stat->numberUnitPerType[WORKER]
			<<"\texplorers="<<stat->numberUnitPerType[EXPLORER]
			<<"\twarriors="<<stat->numberUnitPerType[WARRIOR]
			<<"\tfree_workers="<<stat->isFree[WORKER]
			<<"\tfree_explorers="<<stat->isFree[EXPLORER]
			<<"\tfree_warriors="<<stat->isFree[WARRIOR]
			<<"\tworker_jobs_open="<<stat->totalNeeded
			<<"\tassigned_worker_slots="<<assignedWorkerSlots
			<<"\tworking_units="<<workingUnits
			<<"\tconstruction_assigned="<<constructionAssigned
			<<"\tconstruction_working="<<constructionWorking
			<<"\tinn_assigned="<<innAssigned
			<<"\tinn_working="<<innWorking
			<<"\tinn_corn="<<innCorn
			<<"\tswarm_assigned="<<swarmAssigned
			<<"\tswarm_working="<<swarmWorking
			<<"\tswarm_corn="<<swarmCorn
			<<"\tswarm_empty="<<swarmEmpty
			<<"\tswarm_production_timeout="<<swarmProductionTimeout
			<<"\tswarm_corn_distance="<<swarmCornDistance
			<<"\tswarm_corn_reachable="<<swarmCornReachable
			<<"\tlocal_food_capacity="<<localFoodCapacity
			<<"\ttechnology_assigned="<<technologyAssigned
			<<"\ttechnology_working="<<technologyWorking
			<<"\tmilitary_assigned="<<militaryAssigned
			<<"\tmilitary_working="<<militaryWorking
			<<"\tattack_upgraded_warriors="<<attackUpgradedWarriors
			<<"\tbuild_upgraded_workers="<<buildUpgradedWorkers
			<<"\tground_attack_explorers="<<groundAttackExplorers
			<<"\tswim_upgraded_explorers="<<swimUpgradedExplorers
			<<"\tamphibious_attack_explorers="<<amphibiousAttackExplorers
			<<"\tcombat_ready_warriors="<<combatReadyWarriors
			<<"\tswim_upgraded_warriors="<<swimUpgradedWarriors
			<<"\twarriors_attacking="<<warriorsAttacking
			<<"\twarriors_flagged="<<warriorsFlagged
			<<"\tenemy_warriors_near_colony="<<enemyWarriorsNearColony
			<<"\tenemy_warriors_near_war_flags="<<enemyWarriorsNearWarFlags
			<<"\tenemy_warriors_near_towers="<<enemyWarriorsNearTowers
			<<"\ttower_covered_buildings="<<towerCoveredBuildings
			<<"\tunits_under_attack="<<unitsUnderAttack
			<<"\tworkers_under_attack="<<workersUnderAttack
			<<"\twarriors_under_attack="<<warriorsUnderAttack
			<<"\thungry="<<stat->needFood
			<<"\tcritical_food="<<stat->needFoodCritical
			<<"\tunserved_food="<<stat->needFoodNoInns
			<<"\tneed_heal="<<stat->needHeal
			<<"\tfood="<<stat->totalFood
			<<"\tfood_capacity="<<stat->totalFoodCapacity
			<<"\tbuildings="<<stat->totalBuilding
			<<"\tbuilding_sites="<<buildingSites
			<<"\tlevel1_buildings="<<level1Buildings
			<<"\tlevel2_buildings="<<level2Buildings
			<<"\tlevel3_buildings="<<level3Buildings
			<<"\tswarms="<<stat->numberBuildingPerType[IntBuildingType::SWARM_BUILDING]
			<<"\tinns="<<stat->numberBuildingPerType[IntBuildingType::FOOD_BUILDING]
			<<"\tinn_level1="<<stat->numberBuildingPerTypePerLevel[IntBuildingType::FOOD_BUILDING][1]
			<<"\tinn_level2="<<stat->numberBuildingPerTypePerLevel[IntBuildingType::FOOD_BUILDING][3]
			<<"\tinn_level3="<<stat->numberBuildingPerTypePerLevel[IntBuildingType::FOOD_BUILDING][5]
			<<"\thospitals="<<stat->numberBuildingPerType[IntBuildingType::HEAL_BUILDING]
			<<"\tracetracks="<<stat->numberBuildingPerType[IntBuildingType::WALKSPEED_BUILDING]
			<<"\tpools="<<stat->numberBuildingPerType[IntBuildingType::SWIMSPEED_BUILDING]
			<<"\tbarracks="<<stat->numberBuildingPerType[IntBuildingType::ATTACK_BUILDING]
			<<"\tschools="<<stat->numberBuildingPerType[IntBuildingType::SCIENCE_BUILDING]
			<<"\tschool_level1="<<stat->numberBuildingPerTypePerLevel[IntBuildingType::SCIENCE_BUILDING][1]
			<<"\tschool_level2="<<stat->numberBuildingPerTypePerLevel[IntBuildingType::SCIENCE_BUILDING][3]
			<<"\tschool_level3="<<stat->numberBuildingPerTypePerLevel[IntBuildingType::SCIENCE_BUILDING][5]
			<<"\ttowers="<<stat->numberBuildingPerType[IntBuildingType::DEFENSE_BUILDING]
			<<"\texploration_flags="<<stat->numberBuildingPerType[IntBuildingType::EXPLORATION_FLAG]
			<<"\twar_flags="<<stat->numberBuildingPerType[IntBuildingType::WAR_FLAG]
			<<"\tclearing_flags="<<stat->numberBuildingPerType[IntBuildingType::CLEARING_FLAG]
			<<"\texploration_flag_assigned="<<explorationFlagAssigned
			<<"\texploration_flag_units="<<explorationFlagUnits
			<<"\twar_flag_assigned="<<warFlagAssigned
			<<"\twar_flag_units="<<warFlagUnits
			<<"\tclearing_flag_assigned="<<clearingFlagAssigned
			<<"\tclearing_flag_units="<<clearingFlagUnits
			<<"\tbuildings_under_attack="<<buildingsUnderAttack
			<<"\ttotal_hp="<<stat->totalHP
			<<"\tattack_power="<<stat->totalAttackPower
			<<"\tdefense_power="<<stat->totalDefensePower
			<<"\tprestige="<<team->prestige
			<<std::endl;
	}
}



void Engine::printNicowarTournamentMaps(std::ostream& output)
{
	std::vector<std::string> maps;
	if(Toolkit::getFileManager()->initDirectoryListing("maps", "map", false))
	{
		std::string fileName;
		while(!(fileName=Toolkit::getFileManager()->getNextDirectoryEntry()).empty())
			maps.push_back(std::string("maps")+DIR_SEPARATOR+fileName);
	}
	std::sort(maps.begin(), maps.end());
	for(std::vector<std::string>::const_iterator map=maps.begin(); map!=maps.end(); ++map)
	{
		MapHeader header=loadMapHeader(*map);
		if(header.getNumberOfTeams()==4)
			output<<"NICOWAR_TOURNAMENT_MAP\t"<<*map<<"\t"<<header.getMapName()<<std::endl;
	}
}


void Engine::printNicowarScenarioMaps(std::ostream& output)
{
	std::vector<std::string> maps;
	if(Toolkit::getFileManager()->initDirectoryListing("maps", "map", false))
	{
		std::string fileName;
		while(!(fileName=Toolkit::getFileManager()->getNextDirectoryEntry()).empty())
			maps.push_back(std::string("maps")+DIR_SEPARATOR+fileName);
	}
	std::sort(maps.begin(), maps.end());
	for(std::vector<std::string>::const_iterator map=maps.begin(); map!=maps.end(); ++map)
	{
		MapHeader header=loadMapHeader(*map);
		if(header.getNumberOfTeams()>=2)
			output<<"NICOWAR_SCENARIO_MAP\t"<<*map<<"\t"
				<<header.getMapName()<<"\t"<<header.getNumberOfTeams()<<std::endl;
	}
}




GameHeader Engine::createNicowarVersionTestGame(int numberOfTeams, int rotation)
{
	assert(numberOfTeams == 4);

	GameHeader gameHeader;
	if(rotation<0)
		rotation=syncRand()%4;
	const int teams[2] = {rotation, (rotation+2)%4};
	const AI::ImplementationID versions[2] = {AI::MAXIMA, AI::NICOWAR};
	for(int i=0; i<2; ++i)
	{
		AI::ImplementationID iid=versions[i];
		int teamColor=teams[i];
		gameHeader.getBasePlayer(i) = BasePlayer(i, AINames::getAIText(iid), teamColor, Player::playerTypeFromImplementationID(iid));
		gameHeader.setAllyTeamNumber(teamColor, teamColor);
	}
	gameHeader.setNumberOfPlayers(2);
	return gameHeader;
}


void Engine::updateMaximaExperiment()
{
    const bool active=globalContainer->runNicowarTournamentMatch || globalContainer->runNicowar2v2TournamentMatch
        || globalContainer->runNicowarScenarioMatch || globalContainer->runMaximaCheckpoint;
    if(!active) return;
    const int tick=gui.game.stepCounter;
    if(tick==1 || tick%1000==0) printNicowarScoreTelemetry();
    if(globalContainer->nicowarTelemetry && (tick==1 || tick%100==0)) printNicowarObserverTelemetry();
    std::string path;
    if(tick==globalContainer->maximaCheckpointTick) path=globalContainer->maximaCheckpointOutput;
    if(globalContainer->maximaCheckpointHarvestInterval>0 && tick%globalContainer->maximaCheckpointHarvestInterval==0)
        path=globalContainer->maximaCheckpointHarvestDirectory+"/checkpoint-"+std::to_string(tick)+".game";
    if(path.empty() || maximaLastCheckpointTick==tick) return;
    GAGCore::BinaryOutputStream checkpoint(Toolkit::getFileManager()->openOutputStreamBackend(path));
    if(!checkpoint.isValid() || checkpoint.isEndOfStream()) throw std::runtime_error("Cannot write Maxima checkpoint");
    gui.save(&checkpoint,"Maxima experiment checkpoint");
    MaximaExperimentAudit::state(gui.game,"checkpoint");
    maximaLastCheckpointTick=tick;
}
