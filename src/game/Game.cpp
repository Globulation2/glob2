// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <algorithm>
#include <iostream>

#include "AICastor.h"
#include "AIMaxima.h"
#include "AINicowar.h"

#include <assert.h>
#include <string.h>

#include <string>
#include <stdexcept>

#include <FileManager.h>

#include "DatasetWriter.h"
#include "Game.h"
#include "ai/BuildingCapabilities.h"
#include <stdexcept>
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Order.h"
#include "Unit.h"
#include "Utilities.h"
#include "GameGUI.h"
#include <SDL3/SDL.h>

#include "MapEdit.h"

#include "Brush.h"
#include "Bullet.h"
#include "TextStream.h"

#include "ReplayWriter.h"

#include "render/GameAnimations.h"
#include "render/SoftwareTerrainCache.h"
#include "ai/engine/AIPipeline.h"

#define BULLET_IMGID 0

Game::Game(GameGUI *gui, MapEdit* edit):
	buildingsTypes(globalContainer->buildingsTypes),
	mapscript(this, gui)
{
	init(gui, edit);
}

Game::~Game()
{
	clearGame();
}

const AIPlanning::BuildingCapabilityIndex& Game::buildingCapabilities() const
{
    // Published during setup, before any AI workers may inspect the catalog.
    assert(buildingCapabilityIndex);
    return *buildingCapabilityIndex;
}

void Game::configureBuildingCatalog()
{
	const auto routingFlags=[](const BuildingType* type) {
		return Uint8(type->runtimeSuppliesStock | (type->runtimeFetchesStock<<1) |
			(type->runtimeSuppliesDirectStock<<2) | (type->runtimeFetchesDirectStock<<3));
	};
	std::vector<Uint8> previous;
	previous.reserve(buildingsTypes.size());
	for (size_t id=0; id<buildingsTypes.size(); ++id) previous.push_back(routingFlags(buildingsTypes.get(id)));
	buildingsTypes.configureExperiments(gameHeader.getExperiments().keys());
    buildingCapabilityIndex = std::make_unique<const AIPlanning::BuildingCapabilityIndex>(buildingsTypes);
	bool routingChanged=false;
	for (size_t id=0; id<previous.size(); ++id) routingChanged |= previous[id]!=routingFlags(buildingsTypes.get(id));
	if (routingChanged) map.invalidateSupplierLocations();
	for (Team* team : teams)
		if (team)
		{
			team->stockSuppliers.clear();
			team->directStockSuppliers.clear();
			for (int i=0; i<Building::MAX_COUNT; ++i)
				if (Building* building=team->myBuildings[i]; building && building->buildingState==Building::ALIVE)
				{
					if (building->type->runtimeSuppliesStock) team->stockSuppliers.push_back(building);
					if (building->type->runtimeSuppliesDirectStock) team->directStockSuppliers.push_back(building);
				}
			if (routingChanged)
			{
				team->dirtyGlobalGradient();
				for (int resource=0; resource<MAX_RESOURCES; ++resource) map.dirtyMarketGradients(team->teamNumber,resource);
			}
		}
}


void Game::init(GameGUI *gui, MapEdit* edit)
{
	this->gui=gui;
	this->edit=edit;
	clientSink=gui;
	clientEvents=gui ? &gui->clientEvents : nullptr;
	clientRequests=gui ? &gui->clientRequests : nullptr;
	recordingFailingUnits=BuildingRef();
	buildProjects.clear();

	animations = std::make_unique<GameAnimations>(!globalContainer->runNoX, 0);

	for (int i=0; i<Team::MAX_COUNT; ++i)
	{
		teams[i]=nullptr;
		players[i]=nullptr;
	}
	mapHeader.reset();
	gameHeader.reset();
	gameHeader.setBuildingCatalogSnapshot(buildingsTypes.snapshotJson());
	configureBuildingCatalog();

	clearGame();

	stepCounter=0;
	prestigeToReach=0;

	for (int i=0; i<TICK_PROFILE_BUF_LEN; i++)
		ticksGameSum[i]=0;

	maskAwayPlayer = 0;
	// Only setGameHeader and setWaitingOnMask assigned this before; a Game that never
	// receives a header (headless tests) otherwise gates every syncStep on garbage.
	anyPlayerWaited = false;
}


/** Reset player and team lists, game end stuff and selection stuff. */
void Game::clearGame()
{
	clearAI(); // Join all controller work before deleting teams or players.
	scriptGenerations.fill(0);
	recordingFailingUnits=BuildingRef();
	hasSavedRandomState = false;
	// Delete existing teams and players
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (teams[i])
		{
			delete teams[i];
			teams[i]=NULL;
		}
	}
	for (int i=0; i<gameHeader.getNumberOfPlayers(); i++)
	{
		if (players[i])
		{
			delete players[i];
			players[i]=NULL;
		}
	}

	// Clear build projects
	buildProjects.clear();

	///Clears prestige
	totalPrestige=0;
	totalPrestigeReached=false;
	isGameEnded=false;

	highlightBuildingType=0;
	highlightUnitType=0;
}



// Precondition: every players[i]->teamNumber must satisfy
// 0 <= teamNumber < mapHeader.getNumberOfTeams() AND teams[teamNumber] must
// be non-null. The two in-memory callers (MapEditClicks, MapEditDialog)
// build their GameHeader from a getNumberOfTeams() loop, so they're
// well-formed by construction. The loader path (GameGUIPersistence ->
// GameHeader::load -> BasePlayer::load) bounds-checks teamNumber against
// Team::MAX_COUNT before reaching here; the assert catches the residual
// case where teamNumber is in [getNumberOfTeams(), MAX_COUNT) — a stale
// header paired with a smaller-team map.
void Game::setGameHeader(const GameHeader& newGameHeader, bool saveAI)
{
	if (saveAI) {
		drainAI();
		if (aiPipeline && gameHeader.getAIOrderDelay()!=newGameHeader.getAIOrderDelay())
			throw std::logic_error("A saved match cannot change its AI delay");
	} else clearAI();
	const GameHeader previousHeader = gameHeader;
	GameHeader resolvedHeader = newGameHeader;
	if (!resolvedHeader.getBuildingCatalogSnapshot().empty()
		&& resolvedHeader.getBuildingCatalogSnapshot() != buildingsTypes.snapshotJson())
		throw std::runtime_error("Game setup building catalog does not match the map catalog");
	resolvedHeader.setBuildingCatalogSnapshot(buildingsTypes.snapshotJson());
	for (int p=0; p<Team::MAX_COUNT; ++p)
	{
		if (saveAI && gameHeader.getBasePlayer(p).type >= BasePlayer::P_AI)
			resolvedHeader.setAIConfig(p, gameHeader.getAIConfig(p));
		else
			resolvedHeader.setAIConfig(p, newGameHeader.getAIConfig(p));
	}
	gameHeader = resolvedHeader;
	configureBuildingCatalog();
	for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		teams[i]->playersMask=0;
		teams[i]->numberOfPlayer=0;
	}

	for (int i=0; i<newGameHeader.getNumberOfPlayers(); i++)
	{
		//Don't change AI's
		if(!saveAI || previousHeader.getBasePlayer(i).type < BasePlayer::P_AI)
		{
			cancelAI(i);
			delete players[i];
			players[i]=new Player();
			players[i]->setBasePlayer(&newGameHeader.getBasePlayer(i), teams);
		}
		const Sint32 tn = players[i]->teamNumber;
		assert(tn >= 0 && tn < mapHeader.getNumberOfTeams());
		assert(teams[tn] != NULL);
		teams[tn]->numberOfPlayer+=1;
		teams[tn]->playersMask |= Team::teamNumberToMask(i);
	}

	// A loaded saved game already restored the live RNG. New maps and old
	// saves retain the seed-based initialization used by earlier versions.
	const bool gameSeedChanged = newGameHeader.getRandomSeed() != previousHeader.getRandomSeed();
	if (!hasSavedRandomState || !mapHeader.getIsSavedGame() || gameSeedChanged)
		syncRandom.seed(newGameHeader.getRandomSeed());
	if (gameSeedChanged)
		for (int p=0; p<newGameHeader.getNumberOfPlayers(); ++p)
			if (players[p] && players[p]->ai)
				players[p]->ai->resetRandom();

	if(newGameHeader.isMapDiscovered())
		map.setMapDiscovered();

	// Custom-game "stockpile start" rule: seed each team's shared market/
	// exchange resource pool. Only feeds buildings with useTeamResources
	// (markets/exchanges) -- a fresh regular building still starts empty.
	// setGameHeader can run more than once before a match starts (e.g. the
	// lobby's player list changing) AND when loading an existing save
	// (GameGUI::loadFromHeaders calls Game::load(), which already restores
	// each team's real, accumulated teamResources, before calling this).
	// Re-seed only when the level actually changes from what gameHeader
	// (the outgoing header, about to be replaced below) already had: a
	// plain assignment -- like Team::init's own zeroing -- rather than an
	// accumulating "+=", which would otherwise stack the bonus on every
	// lobby re-call, but gated so a load (where the incoming and outgoing
	// headers agree, since both were just read from the same save) leaves
	// the just-restored real resources untouched instead of clobbering
	// them back down to the stockpile amount.
	if (newGameHeader.getStockpileStartLevel() != previousHeader.getStockpileStartLevel())
	{
		static constexpr Sint32 stockpileAmount[] = {0, 50, 150, 300};
		const Sint32 stockpile = stockpileAmount[newGameHeader.getStockpileStartLevel()];
		for (int i=0; i<mapHeader.getNumberOfTeams(); ++i)
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
				teams[i]->teamResources[r] = stockpile;
	}

	for (int p=0; p<Team::MAX_COUNT; ++p)
		resolvedHeader.setAIConfig(p, gameHeader.getAIConfig(p));
	// Resolve Maxima defaults before publishing the header. AI polling must
	// treat it as read-only, including on the first tick of a new game.
	for (int p=0; p<resolvedHeader.getNumberOfPlayers(); ++p)
		if (resolvedHeader.getBasePlayer(p).type ==
			BasePlayer::playerTypeFromImplementationID(AI::MAXIMA))
		{
			// A loaded controller's saved strategy takes precedence over
			// environment defaults, even in older saves with an empty header.
			if (mapHeader.getIsSavedGame() && players[p] && players[p]->ai)
				if (const auto *maxima = dynamic_cast<const AIMaxima::Maxima *>(
						players[p]->ai->aiImplementation))
				{
					resolvedHeader.setAIConfig(p, maxima->canonicalStrategy());
					continue;
				}
			AIMaxima::ResolvedStrategy strategy;
			std::string error;
			if (!AIMaxima::StrategyResolver::resolveForPlayer(
					resolvedHeader, p, strategy, error))
				throw std::runtime_error("Maxima strategy error: " + error);
			resolvedHeader.setAIConfig(p,
				AIMaxima::StrategyResolver::canonicalValues(strategy.values));
		}
	for (const auto& definition : experimentDefinitions())
		if (mapHeader.requiredTerrainExperiments.has(definition.id)) resolvedHeader.getExperiments().set(definition.id);
	gameHeader = resolvedHeader;
	configureBuildingCatalog();
	anyPlayerWaited=false;
}



void Game::setAlliances(void)
{
	for(int i=0; i<mapHeader.getNumberOfTeams(); ++i)
	{
		int allyTeam = gameHeader.getAllyTeamNumber(i);
		teams[i]->allies = 0;
		teams[i]->enemies = 0;
		for(int j=0; j<mapHeader.getNumberOfTeams(); ++j)
		{
			int otherAllyTeam = gameHeader.getAllyTeamNumber(j);
			if(allyTeam == otherAllyTeam)
			{
				teams[i]->allies |= teams[j]->me;
				teams[i]->sharedVisionOther |= teams[j]->me;
			}
			else
			{
				teams[i]->enemies |= teams[j]->me;
			}
		}
	}
}

void Game::applyStartingRules(void)
{
	const int hpDivisor = gameHeader.getGlassCannonScale();
	const bool fearless = gameHeader.isUnitsFearless();
	const int buildingHpMultiplier = gameHeader.getBuildingHpMultiplier();
	for (int t=0; t<mapHeader.getNumberOfTeams(); ++t)
	{
		for (int i=0; i<Unit::MAX_COUNT; ++i)
		{
			Unit *unit = teams[t]->myUnits[i];
			if (!unit)
				continue;
			if (hpDivisor != 1)
			{
				unit->performance[HP] = std::max(1, unit->performance[HP] / hpDivisor);
				unit->hp = std::max(1, unit->hp / hpDivisor);
				unit->trigHP /= hpDivisor;
			}
			if (fearless)
				unit->trigHP = 0;
		}
		if (buildingHpMultiplier != 1)
			for (int i=0; i<Building::MAX_COUNT; ++i)
				if (teams[t]->myBuildings[i])
					teams[t]->myBuildings[i]->hp *= buildingHpMultiplier;
	}
}

void Game::setWaitingOnMask(Uint32 mask)
{
	maskAwayPlayer = mask;
	anyPlayerWaited = (mask != 0);
}



void Game::dumpAllData(const std::string& file)
{
	OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(file));
	if (stream->isEndOfStream())
	{
		std::cerr << "Can't dump full game memory to file "<< file << std::endl;
	}
	else
	{
		std::cerr << "Dumped full game memory to file "<< file << std::endl;
		save(stream, false, file);
	}
	delete stream;
}

Team *Game::getTeamWithMostPrestige(void)
{
	int maxPrestige=0;
	Team *maxPrestigeTeam=NULL;

	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		Team *t=teams[i];
		if (t->prestige > maxPrestige)
		{
			maxPrestigeTeam=t;
			maxPrestige=t->prestige;
		}
	}
	return maxPrestigeTeam;
}

bool Game::isPrestigeWinCondition(void)
{
	std::list<std::shared_ptr<WinningCondition> >& conditions = gameHeader.getWinningConditions();
	for(std::list<std::shared_ptr<WinningCondition> >::iterator i = conditions.begin(); i!=conditions.end(); ++i)
	{
		if((*i)->getType() == WCPrestige)
			return true;
	}
	return false;
}

static_assert(ClientEvents::MaxTeams >= Team::MAX_COUNT, "ClientEvents pulse must cover every team");

BuildingRef Game::refOf(const Building *b)
{
	BuildingRef ref;
	if (b)
	{
		ref.gid = b->gid;
		ref.generation = b->scriptIdentity;
	}
	return ref;
}

UnitRef Game::refOf(const Unit *u)
{
	UnitRef ref;
	if (u)
	{
		ref.gid = u->gid;
		ref.generation = u->scriptIdentity;
	}
	return ref;
}

Building *Game::resolveBuilding(BuildingRef ref) const
{
	if (ref.empty())
		return nullptr;
	const int team = Building::GIDtoTeam(ref.gid);
	const int id = Building::GIDtoID(ref.gid);
	if (team < 0 || team >= Team::MAX_COUNT || !teams[team] || id < 0 || id >= Building::MAX_COUNT)
		return nullptr;
	Building *b = teams[team]->myBuildings[id];
	return (b && b->scriptIdentity == ref.generation) ? b : nullptr;
}

Unit *Game::resolveUnit(UnitRef ref) const
{
	if (ref.empty())
		return nullptr;
	const int team = Unit::GIDtoTeam(ref.gid);
	const int id = Unit::GIDtoID(ref.gid);
	if (team < 0 || team >= Team::MAX_COUNT || !teams[team] || id < 0 || id >= Unit::MAX_COUNT)
		return nullptr;
	Unit *u = teams[team]->myUnits[id];
	return (u && u->scriptIdentity == ref.generation) ? u : nullptr;
}

void Game::publishClientEvent(ClientEventVariant event)
{
	if (clientEvents)
		clientEvents->push(std::move(event));
}

Uint32 Game::allocateScriptIdentity(bool building, Uint16 gid)
{
 const int team=building?Building::GIDtoTeam(gid):Unit::GIDtoTeam(gid);
 const int slot=building?Building::GIDtoID(gid):Unit::GIDtoID(gid);
 if(team<0 || team>=Team::MAX_COUNT)throw std::runtime_error("Invalid scripting entity slot");
 static_assert(SCRIPT_ENTITY_SLOTS_PER_TEAM == Unit::MAX_COUNT &&
     SCRIPT_ENTITY_SLOTS_PER_TEAM == Building::MAX_COUNT);
 auto& generation=scriptGenerations[scriptGenerationIndex(building, team, slot)];
 if(generation==0xffffffffu)throw std::runtime_error("JavaScript entity generation exhausted");
 return ++generation;
}
