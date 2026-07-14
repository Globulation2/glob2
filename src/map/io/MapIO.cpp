// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"

#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

#include <algorithm>
#include <Stream.h>


bool Map::load(GAGCore::InputStream *stream, MapHeader& header, Game *game)
{
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
	cases.resize(size);
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

		cases[i].terrain = stream->readUint16("terrain");
		cases[i].building = stream->readUint16("building");

		stream->read(&(cases[i].ressource), 4, "ressource");
		cases[i].groundUnit = stream->readUint16("groundUnit");
		cases[i].airUnit = stream->readUint16("airUnit");
		cases[i].forbidden = stream->readUint32("forbidden");
		if(versionMinor < 62)
			stream->readUint32("hiddenForbidden");
		cases[i].guardArea = stream->readUint32("guardArea");
		cases[i].clearArea = stream->readUint32("clearArea");
		cases[i].scriptAreas = stream->readUint16("scriptAreas");
		cases[i].canRessourcesGrow = stream->readUint8("canRessourcesGrow");
		if(versionMinor >= 63)
			cases[i].fertility = stream->readUint16("fertility");
		fertilityMaximum = std::max(fertilityMaximum, cases[i].fertility);

		stream->readLeaveSection();
	}
	stream->readLeaveSection();

	for(int n=0; n<9; ++n)
	{
		stream->readEnterSection(n);
		setAreaName(n, stream->readText("areaname"));
		stream->readLeaveSection();
	}
	
	if (game)
	{
                /* Must set game field before following action as they
                   may need it (in particular
                   makeDiscoveredAreasExplored uses it). */
		this->game=game;

		// This is a game, so we do compute gradients
		for (int t=0; t<header.getNumberOfTeams(); t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
				{
					assert(ressourcesGradient[t][r][s]==NULL);
					ressourcesGradient[t][r][s]=new Uint8[size];
					updateRessourcesGradient(t, r, (bool)s);
				}
		for (int t=0; t<Team::MAX_COUNT; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<1; s++)
					gradientUpdated[t][r][s]=false;

		for (int t=0; t<header.getNumberOfTeams(); t++)
			for (int s=0; s<2; s++)
			{
				assert(forbiddenGradient[t][s] == NULL);
				forbiddenGradient[t][s] = new Uint8[size];
				updateForbiddenGradient(t, s);
				
				assert(guardAreasGradient[t][s] == NULL);
				guardAreasGradient[t][s] = new Uint8[size];
				updateGuardAreasGradient(t, s);
			
				assert(clearAreasGradient[t][s] == NULL);
				clearAreasGradient[t][s] = new Uint8[size];
				updateClearAreasGradient(t, s);
				
				guardGradientUpdated[t][s] = false;
				clearGradientUpdated[t][s] = false;
			}
		for (int t=0; t<header.getNumberOfTeams(); t++)
		{
			assert(exploredArea[t] == NULL);
			exploredArea[t] = new Uint8[size];
			initExploredArea(t);
			makeDiscoveredAreasExplored(t);
			
			clearingAreaClaims[t] = new Uint16[size];
			memset(clearingAreaClaims[t], NOGUID, size*sizeof(Uint16));
		}
	}

	// We load sectors:
	wSector = stream->readSint32("wSector");
	hSector = stream->readSint32("hSector");
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
	
	return true;
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

		stream->writeUint16(cases[i].terrain, "terrain");
		stream->writeUint16(cases[i].building, "building");
		
		stream->write(&(cases[i].ressource), 4, "ressource");
		
		stream->writeUint16(cases[i].groundUnit, "groundUnit");
		stream->writeUint16(cases[i].airUnit, "airUnit");
		stream->writeUint32(cases[i].forbidden, "forbidden");
		stream->writeUint32(cases[i].guardArea, "guardArea");
		stream->writeUint32(cases[i].clearArea, "clearArea");
		stream->writeUint16(cases[i].scriptAreas, "scriptAreas");
		stream->writeUint8(cases[i].canRessourcesGrow, "canRessourcesGrow");
		stream->writeUint16(cases[i].fertility, "fertility");
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
	
	for (int t=0; t<oldNumberOfTeam; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				assert(ressourcesGradient[t][r][s]);
	for (int t=oldNumberOfTeam; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				assert(ressourcesGradient[t][r][s]==NULL);
	
	int t=oldNumberOfTeam;
	for (int r=0; r<MAX_RESSOURCES; r++)
		for (int s=0; s<2; s++)
		{
			assert(ressourcesGradient[t][r][s]==NULL);
			ressourcesGradient[t][r][s]=new Uint8[size];
			updateRessourcesGradient(t, r, (bool)s);
		}
	
	for (int s=0; s<2; s++)
	{
		assert(forbiddenGradient[t][s] == NULL);
		forbiddenGradient[t][s] = new Uint8[size];
		updateForbiddenGradient(t, s);
		assert(guardAreasGradient[t][s] == NULL);
		guardAreasGradient[t][s] = new Uint8[size];
		updateGuardAreasGradient(t, s);
		assert(clearAreasGradient[t][s] == NULL);
		clearAreasGradient[t][s] = new Uint8[size];
		updateClearAreasGradient(t, s);
	}
	
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
//	int oldNumberOfTeam=numberOfTeam+1;
	assert(numberOfTeam<Team::MAX_COUNT);
	
//	for (int t=0; t<oldNumberOfTeam; t++)
//		for (int r=0; r<MAX_RESSOURCES; r++)
//			for (int s=0; s<2; s++)
//				assert(ressourcesGradient[t][r][s]);
//	for (int t=oldNumberOfTeam; t<Team::MAX_COUNT; t++)
//		for (int r=0; r<MAX_RESSOURCES; r++)
//			for (int s=0; s<2; s++)
//				assert(ressourcesGradient[t][r][s]==NULL);
	
	int t=numberOfTeam;
	for (int r=0; r<MAX_RESSOURCES; r++)
		for (int s=0; s<2; s++)
		{
//			assert(ressourcesGradient[t][r][s]);
			if(ressourcesGradient[t][r][s])
				delete[] ressourcesGradient[t][r][s];
			ressourcesGradient[t][r][s]=NULL;
		}

	for (int s=0; s<2; s++)
	{
		assert(forbiddenGradient[t][s] != NULL);
		delete[] forbiddenGradient[t][s];
		forbiddenGradient[t][s]=NULL;
		assert(guardAreasGradient[t][s] != NULL);
		delete[] guardAreasGradient[t][s];
		guardAreasGradient[t][s]=NULL;
		assert(clearAreasGradient[t][s] != NULL);
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

