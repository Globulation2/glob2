// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "FileFormatVersions.h"
#include "MapInternal.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"

#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

#include <algorithm>
#include <Stream.h>
#include <BinaryStream.h>
#include <limits>
#include <memory>
#include <stdexcept>


bool Map::load(GAGCore::InputStream *stream, MapHeader& header, Game *game)
try
{
	GAGCore::BinaryInputStream::CheckedReads checked(stream);
	assert(header.getVersionMinor()>=16);

	Sint32 versionMinor = header.getVersionMinor();

	clear();

	stream->readEnterSection("Map");

	char signature[4];
	stream->read(signature, 4, "signatureStart");
	if (memcmp(signature, "MapB", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the beginning of Map.\n");
		return false;
	}

	// We load and compute size:
	wDec = stream->readSint32("wDec");
	hDec = stream->readSint32("hDec");
	if (wDec < 0 || hDec < 0 || wDec >= std::numeric_limits<int>::digits ||
		hDec >= std::numeric_limits<int>::digits || wDec + hDec >= std::numeric_limits<int>::digits)
		return false;
	w = 1<<wDec;
	h = 1<<hDec;
	wMask = w-1;
	hMask = h-1;
	size = w*h;

	// We allocate memory:
	mapDiscovered.resize(size);
	fogOfWarA.assign(size, 0);
	fogOfWarB.assign(size, 0);
	fogOfWar = &fogOfWarA[0];
	displayedForbiddenView.resize(size, false);
	displayedGuardAreaView.resize(size, false);
	displayedClearAreaView.resize(size, false);
	tiles.resize(size);
	undermap = new Uint8[size];
	listedAddr = new Uint8*[size];
	aStarPoints=new AStarAlgorithmPoint[size];
	immobileUnits = new Uint8[size];
	memset(immobileUnits, 255, size*sizeof(Uint8));

	// We read what's inside the map:
	stream->read(undermap, size, "undermap");
	stream->readEnterSection("cases");
	for (size_t i=0; i<size; i++)
	{
		stream->readEnterSection(i);
		mapDiscovered[i] = stream->readUint32("mapDiscovered");

		tiles[i].terrain = stream->readUint16("terrain");
		tiles[i].building = stream->readUint16("building");
		if (tiles[i].building != NOGBID && tiles[i].building >= Building::MAX_COUNT * header.getNumberOfTeams())
			return false;

		stream->read(&(tiles[i].resource), 4, "ressource");
		tiles[i].groundUnit = stream->readUint16("groundUnit");
		tiles[i].airUnit = stream->readUint16("airUnit");
		tiles[i].forbidden = stream->readUint32("forbidden");
		if(versionMinor < 62)
			stream->readUint32("hiddenForbidden");
		tiles[i].guardArea = stream->readUint32("guardArea");
		tiles[i].clearArea = stream->readUint32("clearArea");
		tiles[i].scriptAreas = stream->readUint16("scriptAreas");
		tiles[i].canResourcesGrow = stream->readUint8("canRessourcesGrow");
		if(versionMinor >= 63)
			tiles[i].fertility = stream->readUint16("fertility");
		fertilityMaximum = std::max(fertilityMaximum, tiles[i].fertility);

		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	for(int n=0; n<9; ++n)
	{
		stream->readEnterSection(n);
		setAreaName(n, stream->readText("areaname"));
		stream->readLeaveSection();
	}

	const bool restoreExploredArea = header.getIsSavedGame() && versionMinor >= EXPLORED_AREA_SAVED_VERSION_MINOR;
	if (restoreExploredArea)
		loadExploredArea(stream, header.getNumberOfTeams(), game != NULL);

	this->game = game;

	// We load sectors:
	wSector = stream->readSint32("wSector");
	hSector = stream->readSint32("hSector");
	if (wSector < 0 || hSector < 0 || wSector > w || hSector > h)
		return false;
	sizeSector = wSector*hSector;
	assert(sectors == NULL);
	sectors = new Sector[sizeSector];

#ifndef YOG_SERVER_ONLY
	// Map::setGame is bypassed on the loaded-game path (Game::load uses
	// Map::load directly and the game pointer is set inline above), so
	// the per-sector render buckets must be sized here too.
	if (game)
		game->animations->resize(sizeSector);
#endif  // !YOG_SERVER_ONLY

	arraysBuilt = true;

	stream->readEnterSection("sectors");
	for (int i=0; i<sizeSector; i++)
	{
		stream->readEnterSection(i);
		if (!sectors[i].load(stream, this->game, versionMinor))
		{
			stream->readLeaveSection(3);
			return false;
		}
		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	stream->read(signature, 4, "signatureEnd");
	stream->readLeaveSection();

	if (memcmp(signature, "MapE", 4)!=0)
	{
		fprintf(stderr, "Map:: Failed to find signature at the end of Map.\n");
		return false;
	}

	if (game)
	{
                /* Must set game field before following action as they
                   may need it (in particular
                   makeDiscoveredAreasExplored uses it). */
		this->game=game;

		// This is a game; the gradients are built when a unit first asks for them.
		for (int t=0; t<header.getNumberOfTeams(); t++)
		{
			if (!restoreExploredArea)
			{
				assert(exploredArea[t] == NULL);
				exploredArea[t] = new Uint8[size];
				initExploredArea(t);
				makeDiscoveredAreasExplored(t);
			}
			assert(exploredArea[t]);

			clearingAreaClaims[t] = new Uint16[size];
			memset(clearingAreaClaims[t], NOGUID, size*sizeof(Uint16));
		}
	}

	return true;
}
catch (const std::ios_base::failure& error)
{
	std::cerr << "Map::load: " << error.what() << std::endl;
	clear();
	return false;
}


void Map::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Map");
	stream->write("MapB", 4, "signatureStart");
	
	// We save size:
	stream->writeSint32(wDec, "wDec");
	stream->writeSint32(hDec, "hDec");

	// We write what's inside the map:
	stream->write(undermap, size, "undermap");
	stream->writeEnterSection("cases");
	for (size_t i=0; i<size ;i++)
	{
		stream->writeEnterSection(i);
		stream->writeUint32(mapDiscovered[i], "mapDiscovered");

		stream->writeUint16(tiles[i].terrain, "terrain");
		stream->writeUint16(tiles[i].building, "building");
		
		stream->write(&(tiles[i].resource), 4, "ressource");
		
		stream->writeUint16(tiles[i].groundUnit, "groundUnit");
		stream->writeUint16(tiles[i].airUnit, "airUnit");
		stream->writeUint32(tiles[i].forbidden, "forbidden");
		stream->writeUint32(tiles[i].guardArea, "guardArea");
		stream->writeUint32(tiles[i].clearArea, "clearArea");
		stream->writeUint16(tiles[i].scriptAreas, "scriptAreas");
		stream->writeUint8(tiles[i].canResourcesGrow, "canRessourcesGrow");
		stream->writeUint16(tiles[i].fertility, "fertility");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	//Save area names
	for(int n=0; n<9; ++n)
	{
		stream->writeEnterSection(n);
		stream->writeText(getAreaName(n), "areaname");
		stream->writeLeaveSection();
	}

	assert(game);
	if (game->mapHeader.getIsSavedGame())
		saveExploredArea(stream, game->mapHeader.getNumberOfTeams());

	// We save sectors:
	stream->writeSint32(wSector, "wSector");
	stream->writeSint32(hSector, "hSector");
	stream->writeEnterSection("sectors");
	for (int i=0; i<sizeSector; i++)
	{
		stream->writeEnterSection(i);
		sectors[i].save(stream);
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();

	stream->write("MapE", 4, "signatureEnd");
	stream->writeLeaveSection();
}


void Map::addTeam(void)
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	int oldNumberOfTeam=numberOfTeam-1;
	assert(numberOfTeam>0);
	
	int t=oldNumberOfTeam;
	for (int r=0; r<MAX_RESOURCES; r++)
		for (int s=0; s<SWIM_CLASS_COUNT; s++)
			assert(resourcesGradient[t][r][s]==NULL);
	
	assert(exploredArea[t] == NULL);
	exploredArea[t] = new Uint8[size];
	initExploredArea(t);
	
	assert(clearingAreaClaims[t] == NULL);
	clearingAreaClaims[t] = new Uint16[size];
	memset(clearingAreaClaims[t], NOGUID, size*sizeof(Uint16));
}

void Map::removeTeam(void)
{
	int numberOfTeam=game->mapHeader.getNumberOfTeams();
	assert(numberOfTeam<Team::MAX_COUNT);
	
	int t=numberOfTeam;
	for (int s=0; s<SWIM_CLASS_COUNT; s++)
	{
		for (int r=0; r<MAX_RESOURCES; r++)
		{
			delete[] resourcesGradient[t][r][s];
			resourcesGradient[t][r][s]=NULL;
		}
		delete[] forbiddenGradient[t][s];
		forbiddenGradient[t][s]=NULL;
		delete[] guardAreasGradient[t][s];
		guardAreasGradient[t][s]=NULL;
		delete[] clearAreasGradient[t][s];
		clearAreasGradient[t][s]=NULL;
	}

	
	assert(exploredArea[t] != NULL);
	delete[] exploredArea[t];
	exploredArea[t]=NULL;
	
	assert(clearingAreaClaims[t] != NULL);
	delete[] clearingAreaClaims[t];
	clearingAreaClaims[t]=NULL;
}

// TODO: completely recreate:



namespace
{
bool loadFlag(GAGCore::InputStream *stream, const char *name)
{
	const auto value=stream->readUint8(name);
	if (value>1) throw std::runtime_error("Invalid saved routing flag");
	return value != 0;
}
void saveGradient(GAGCore::OutputStream *stream, const Uint16 *field, size_t size)
{
	stream->writeUint8(field != nullptr, "present");
	if (field) for (size_t i=0; i<size; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint16(field[i], "value");
		stream->writeLeaveSection();
	}
}
void loadGradient(GAGCore::InputStream *stream, Uint16 *&field, size_t size)
{
	const bool present=loadFlag(stream,"present");
	std::unique_ptr<Uint16[]> restored;
	if (present)
	{
		restored=std::make_unique<Uint16[]>(size);
		for (size_t i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			restored[i]=stream->readUint16("value");
			stream->readLeaveSection();
		}
	}
	delete[] field;
	field=restored.release();
}
}

// Cached routing fields deliberately lag map edits. Recomputing them on load
// changes decisions before their scheduled refresh, even with an identical RNG.
void Map::saveRuntimeState(GAGCore::OutputStream *stream) const
{
	stream->writeEnterSection("mapRuntime");
	stream->writeUint8(fogOfWar == fogOfWarA.data(), "fogIsA");
	stream->writeEnterSection("cells");
	for (size_t i=0; i<size; ++i)
	{
		stream->writeEnterSection(i);
		stream->writeUint8(immobileUnits[i], "immobileUnit");
		stream->writeUint32(fogOfWarA[i], "fogA");
		stream->writeUint32(fogOfWarB[i], "fogB");
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeEnterSection("teams");
	for (int t=0; t<game->teamsCount(); ++t)
	{
		stream->writeEnterSection(t);
		stream->writeEnterSection("claims");
		for (size_t i=0; i<size; ++i)
		{
			stream->writeEnterSection(i);
			stream->writeUint16(clearingAreaClaims[t][i], "claim");
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
		stream->writeEnterSection("swimClasses");
		for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
		{
			stream->writeEnterSection(sw);
			stream->writeEnterSection("resources");
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
			{
				stream->writeEnterSection(r);
				saveGradient(stream, resourcesGradient[t][r][sw], size);
				stream->writeUint8(gradientUpdated[t][r][sw], "updated");
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
			stream->writeEnterSection("forbidden");
			saveGradient(stream, forbiddenGradient[t][sw], size);
			stream->writeLeaveSection();
			stream->writeEnterSection("guard");
			saveGradient(stream, guardAreasGradient[t][sw], size);
			stream->writeUint8(guardGradientUpdated[t][sw], "updated");
			stream->writeLeaveSection();
			stream->writeEnterSection("clear");
			saveGradient(stream, clearAreasGradient[t][sw], size);
			stream->writeUint8(clearGradientUpdated[t][sw], "updated");
			stream->writeLeaveSection();
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
		stream->writeEnterSection("buildings");
		for (int b=0; b<Building::MAX_COUNT; ++b) if (auto *building=game->teams[t]->myBuildings[b])
		{
			stream->writeEnterSection(b);
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->writeEnterSection(sw);
				saveGradient(stream, building->globalGradient[sw], size);
				stream->writeUint8(building->dirtyGradient[sw], "dirty");
				stream->writeUint32(building->lastGlobalGradientUpdateStepCounter[sw], "lastUpdate");
				stream->writeLeaveSection();
			}
			stream->writeEnterSection("roundTrip");
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->writeEnterSection(sw);
				stream->writeUint32(building->globalGradientUsedStep[sw], "usedStep");
				for (int r=0; r<MAX_NB_RESOURCES; ++r)
				{
					stream->writeEnterSection(r);
					saveGradient(stream, building->roundTripGradient[r][sw], size);
					stream->writeUint32(building->roundTripGradientStep[r][sw], "step");
					stream->writeUint32(building->roundTripGradientUsedStep[r][sw], "usedStep");
					stream->writeLeaveSection();
				}
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
			stream->writeEnterSection("access");
			for (int sw=0; sw<SWIM_VARIANT_COUNT; ++sw)
			{
				stream->writeEnterSection(sw);
				stream->writeUint8(building->locked[sw], "locked");
				stream->writeUint8(building->anyResourceToClear[sw], "resourceState");
				stream->writeLeaveSection();
			}
			stream->writeLeaveSection();
			stream->writeLeaveSection();
		}
		stream->writeLeaveSection();
		stream->writeLeaveSection();
	}
	stream->writeLeaveSection();
	stream->writeLeaveSection();
}

void Map::loadRuntimeState(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	stream->readEnterSection("mapRuntime");
	const bool fogIsA=loadFlag(stream,"fogIsA");
	fogOfWar=fogIsA ? fogOfWarA.data() : fogOfWarB.data();
	stream->readEnterSection("cells");
	for (size_t i=0; i<size; ++i)
	{
		stream->readEnterSection(i);
		immobileUnits[i]=stream->readUint8("immobileUnit");
		fogOfWarA[i]=stream->readUint32("fogA");
		fogOfWarB[i]=stream->readUint32("fogB");
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readEnterSection("teams");
	for (int t=0; t<game->teamsCount(); ++t)
	{
		stream->readEnterSection(t);
		stream->readEnterSection("claims");
		for (size_t i=0; i<size; ++i)
		{
			stream->readEnterSection(i);
			clearingAreaClaims[t][i]=stream->readUint16("claim");
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("swimClasses");
		for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
		{
			stream->readEnterSection(sw);
			stream->readEnterSection("resources");
			for (int r=0; r<MAX_NB_RESOURCES; ++r)
			{
				stream->readEnterSection(r);
				loadGradient(stream, resourcesGradient[t][r][sw], size);
				gradientUpdated[t][r][sw]=loadFlag(stream,"updated");
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
			stream->readEnterSection("forbidden");
			loadGradient(stream, forbiddenGradient[t][sw], size);
			stream->readLeaveSection();
			stream->readEnterSection("guard");
			loadGradient(stream, guardAreasGradient[t][sw], size);
			guardGradientUpdated[t][sw]=loadFlag(stream,"updated");
			stream->readLeaveSection();
			stream->readEnterSection("clear");
			loadGradient(stream, clearAreasGradient[t][sw], size);
			clearGradientUpdated[t][sw]=loadFlag(stream,"updated");
			stream->readLeaveSection();
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readEnterSection("buildings");
		for (int b=0; b<Building::MAX_COUNT; ++b) if (auto *building=game->teams[t]->myBuildings[b])
		{
			stream->readEnterSection(b);
			for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
			{
				stream->readEnterSection(sw);
				loadGradient(stream, building->globalGradient[sw], size);
				building->dirtyGradient[sw]=loadFlag(stream,"dirty");
				building->lastGlobalGradientUpdateStepCounter[sw]=stream->readUint32("lastUpdate");
				stream->readLeaveSection();
			}
			if (versionMinor >= FILE_FORMAT_VERSION_ROUND_TRIP_FIELDS)
			{
				stream->readEnterSection("roundTrip");
				for (int sw=0; sw<SWIM_CLASS_COUNT; ++sw)
				{
					stream->readEnterSection(sw);
					building->globalGradientUsedStep[sw]=stream->readUint32("usedStep");
					for (int r=0; r<MAX_NB_RESOURCES; ++r)
					{
						stream->readEnterSection(r);
						loadGradient(stream, building->roundTripGradient[r][sw], size);
						building->roundTripGradientStep[r][sw]=stream->readUint32("step");
						building->roundTripGradientUsedStep[r][sw]=stream->readUint32("usedStep");
						stream->readLeaveSection();
					}
					stream->readLeaveSection();
				}
				stream->readLeaveSection();
			}
			stream->readEnterSection("access");
			for (int sw=0; sw<SWIM_VARIANT_COUNT; ++sw)
			{
				stream->readEnterSection(sw);
				building->locked[sw]=loadFlag(stream,"locked");
				building->anyResourceToClear[sw]=stream->readUint8("resourceState");
				if (building->anyResourceToClear[sw]>2) throw std::runtime_error("Invalid saved resource state");
				stream->readLeaveSection();
			}
			stream->readLeaveSection();
			stream->readLeaveSection();
		}
		stream->readLeaveSection();
		stream->readLeaveSection();
	}
	stream->readLeaveSection();
	stream->readLeaveSection();
}
