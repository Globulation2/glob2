// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "Map.h"
#include "Game.h"
#include "Utilities.h"
#include "Unit.h"
#include "MapInternal.h"

#ifndef YOG_SERVER_ONLY
#include "render/GameAnimations.h"
#endif  // !YOG_SERVER_ONLY

#include <algorithm>
#include <valarray>
#include <Stream.h>
#include <queue>


// Definitions of shared direction tables declared in MapInternal.h.
// All Map*.cpp TUs link against these single definitions.

const int deltaOne[8][2]={
	{ 0, -1},
	{ 1,  0},
	{ 0,  1},
	{-1,  0},
	{-1, -1},
	{ 1, -1},
	{ 1,  1},
	{-1,  1}};

const int tabClose[8][2]={
	{-1, -1},
	{ 0, -1},
	{ 1, -1},
	{ 1,  0},
	{ 1,  1},
	{ 0,  1},
	{-1,  1},
	{-1,  0}};

const int tabFar[16][2]={
	{-2, -2},
	{-1, -2},
	{ 0, -2},
	{ 1, -2},
	{ 2, -2},
	{ 2, -1},
	{ 2,  0},
	{ 2,  1},
	{ 2,  2},
	{ 1,  2},
	{ 0,  2},
	{-1,  2},
	{-2,  2},
	{-2,  1},
	{-2,  0},
	{-2, -1}};

Map::Map()
{
	game=NULL;

	arraysBuilt=false;
	
	aStarPoints = NULL;
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_NB_RESSOURCES; r++)
			for (int s=0; s<2; s++)
			{
				ressourcesGradient[t][r][s] = NULL;
				gradientUpdated[t][r][s] = false;
			}
	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int s=0; s<2; s++)
		{
			forbiddenGradient[t][s] = NULL;
			guardAreasGradient[t][s] = NULL;
			clearAreasGradient[t][s] = NULL;
			guardGradientUpdated[t][s] = false;
			clearGradientUpdated[t][s] = false;
		}
	for (int t = 0; t < Team::MAX_COUNT; t++)
		exploredArea[t] = NULL;
	
	undermap=NULL;
	sectors=NULL;
	listedAddr=NULL;
	
	for (int t = 0; t < Team::MAX_COUNT; t++)
		clearingAreaClaims[t] = NULL;
	w=0;
	h=0;
	size=0;
	wMask=0;
	hMask=0;
	wDec=0;
	hDec=0;
	wSector=0;
	hSector=0;
	sizeSector=0;
	
	immobileUnits=NULL;

	areaNames.resize(9);
	
	fertilityMaximum = 0;
}

Map::~Map(void)
{
	clear();
}

void Map::clear()
{
	if (arraysBuilt)
	{
		for (int t=0; t<Team::MAX_COUNT; t++)
			if (ressourcesGradient[t][0][0])
				for (int r=0; r<MAX_RESSOURCES; r++)
					for (int s=0; s<2; s++)
					{
						assert(ressourcesGradient[t][r][s]);
						delete[] ressourcesGradient[t][r][s];
						ressourcesGradient[t][r][s] = NULL;
					}
		
		for (int t=0; t<Team::MAX_COUNT; t++)
			if (forbiddenGradient[t][0])
				for (int s=0; s<2; s++)
				{
					assert(forbiddenGradient[t][s]);
					delete[] forbiddenGradient[t][s];
					forbiddenGradient[t][s] = NULL;
					assert(guardAreasGradient[t][s]);
					delete[] guardAreasGradient[t][s];
					guardAreasGradient[t][s] = NULL;
					assert(clearAreasGradient[t][s]);
					delete[] clearAreasGradient[t][s];
					clearAreasGradient[t][s] = NULL;
					
					guardGradientUpdated[t][s] = false;
					clearGradientUpdated[t][s] = false;
				}
		
		for (int t=0; t<Team::MAX_COUNT; t++)
			if (exploredArea[t])
			{
				assert(exploredArea[t]);
				delete[] exploredArea[t];
				exploredArea[t] = NULL;
			}
		
		assert(undermap);
		delete[] undermap;
		undermap=NULL;
		
		assert(sectors);
		delete[] sectors;
		sectors=NULL;

		assert(listedAddr);
		delete[] listedAddr;
		listedAddr=NULL;
		
		assert(aStarPoints);
		delete[] aStarPoints;
		aStarPoints=NULL;

		for (int t=0; t<Team::MAX_COUNT; t++)
		{
			if (clearingAreaClaims[t])
			{
				assert(clearingAreaClaims[t]);
				delete[] clearingAreaClaims[t];
				clearingAreaClaims[t]=NULL;
			}
		}
		
		assert(immobileUnits);
		delete[] immobileUnits;
		immobileUnits=NULL;

		arraysBuilt=false;
	}
	else
	{
		for (int t=0; t<Team::MAX_COUNT; t++)
			for (int r=0; r<MAX_RESSOURCES; r++)
				for (int s=0; s<2; s++)
					assert(ressourcesGradient[t][r][s]==NULL);
		for (int t=0; t<Team::MAX_COUNT; t++)
			for (int s=0; s<2; s++)
			{
				assert(forbiddenGradient[t][s] == NULL);
				assert(guardAreasGradient[t][s] == NULL);
				assert(clearAreasGradient[t][s] == NULL);
			}
		for (int t=0; t<Team::MAX_COUNT; t++)
			assert(exploredArea[t] == NULL);
		for (int t=0; t<Team::MAX_COUNT; t++)
			assert(clearingAreaClaims[t] == NULL);
		
		assert(undermap==NULL);
		assert(sectors==NULL);
		assert(listedAddr==NULL);

		assert(w==0);
		assert(h==0);
		assert(size==0);
		assert(wMask==0);
		assert(hMask==0);
		assert(wDec==0);
		assert(hDec==0);
		assert(wSector==0);
		assert(hSector==0);
		assert(sizeSector==0);
	}
	
	w=h=0;
	size=0;
	wMask=hMask=0;
	wDec=hDec=0;
	wSector=hSector=0;
	sizeSector=0;
	displayedTeam = NO_DISPLAYED_TEAM;

	for (int t=0; t<Team::MAX_COUNT; t++)
		for (int r=0; r<MAX_RESSOURCES; r++)
			for (int s=0; s<2; s++)
				gradientUpdated[t][r][s]=false;
}


void Map::setSize(int wDec, int hDec, TerrainType terrainType)
{
	clear();

	assert(wDec<16);
	assert(hDec<16);
	this->wDec=wDec;
	this->hDec=hDec;
	w=1<<wDec;
	h=1<<hDec;
	wMask=w-1;
	hMask=h-1;
	size=w*h;

	fogOfWarA.assign(size, 0);
	fogOfWarB.assign(size, 0);
	fogOfWar = &fogOfWarA[0];
	
	displayedForbiddenView.resize(size, false);
	displayedGuardAreaView.resize(size, false);
	displayedClearAreaView.resize(size, false);
	
	cases.assign(size, Case());

	mapDiscovered.assign(size, 0);
	
	undermap=new Uint8[size];
	memset(undermap, terrainType, size);
	
	listedAddr = new Uint8*[size];

	//numberOfTeam=0, then ressourcesGradient[][][] is empty. This is done by clear();

	regenerateMap(0, 0, w, h);

	wSector=w>>Sector::SECTOR_SHIFT;
	hSector=h>>Sector::SECTOR_SHIFT;
	sizeSector=wSector*hSector;

	if(sectors)
		delete[] sectors;
	sectors=new Sector[sizeSector];

	aStarPoints=new AStarAlgorithmPoint[w*h];


	immobileUnits = new Uint8[w*h];
	for (int i=0; i<w*h; i++) 
	{
		immobileUnits[i]=0;
	}

	arraysBuilt=true;
}


void Map::setGame(Game *game)
{
	assert(game);
	this->game=game;
	assert(arraysBuilt);
	assert(sectors);
	for (int i=0; i<sizeSector; i++)
		sectors[i].setGame(game);
#ifndef YOG_SERVER_ONLY
	game->animations->resize(sizeSector);
#endif  // !YOG_SERVER_ONLY
}


