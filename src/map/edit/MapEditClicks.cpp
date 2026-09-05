// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
// Copyright (C) 2006 Bradley Arsenault

#include <FormatableString.h>
#include "Game.h"
#include "GlobalContainer.h"
#include "MapEdit.h"
#include "ScriptEditorScreen.h"
#include "Unit.h"
#include "Utilities.h"
#include "FertilityCalculatorDialog.h"
#include "SDLCompat.h"

void MapEdit::addWidget(MapEditorWidget* widget)
{
	mew.push_back(widget);
}

bool MapEdit::findAction(int x, int y)
{
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		MapEditorWidget* mi=*i;
		if(mi->is_in(x, y) && mi->enabled)
		{
			// Make the widget relative to the same point we hit-tested. Using the
			// mouseX/mouseY members here instead would take the last *motion*
			// position, which a warped or synthetic click can leave elsewhere —
			// hitting one widget while telling it about a coordinate outside itself.
			mi->handleClick(x-mi->area.x, y-mi->area.y);
			return true;
		}
	}
	return false;
}



void MapEdit::enableOnlyGroup(const std::string& group)
{
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		if((*i)->group == group || (*i)->group=="any")
		{
			(*i)->enable();
		}
		else
			(*i)->disable();
	}
}



void MapEdit::drawWidgets()
{
	for(std::vector<MapEditorWidget*>::iterator i=mew.begin(); i!=mew.end(); ++i)
	{
		(*i)->drawSelf();
	}
}


void MapEdit::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
{
	// get data for minimap
	int mMax;
	int szX, szY;
	int decX, decY;
	Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);

	mx-=14+decX;
	my-=14+decY;
	*cx=((mx*game.map.getW())/szX);
	*cy=((my*game.map.getH())/szY);
	*cx+=game.teams[team]->startPosX-(game.map.getW()/2);
	*cy+=game.teams[team]->startPosY-(game.map.getH()/2);
	if (forScreenViewport)
	{
		*cx-=((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}

	*cx&=game.map.getMaskW();
	*cy&=game.map.getMaskH();
}



void MapEdit::handleBrushClick(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
// 	if (brushAccumulator.getAreaSurface() > 32*32)
// 	{
// 		sendBrushOrders();
// 	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
		
	if(lastPlacementX == -1)
		firstPlacement = FirstPlacement{mapX, mapY};
	
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(BrushApplication(mapX, mapY, fig), &game.map);
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimXMinus(fig);
	int startY = mapY-BrushTool::getBrushDimYMinus(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
	const int firstX = firstPlacement ? firstPlacement->x : -1;
	const int firstY = firstPlacement ? firstPlacement->y : -1;
	// we update local values
	const unsigned brushMode = brush.getType();
	if (brushMode == BrushTool::MODE_ADD || brushMode == BrushTool::MODE_DEL)
	{
		const bool add = (brushMode == BrushTool::MODE_ADD);
		const AreaBrushTarget target = areaBrushTarget();
		const Uint32 teamBit = 1u << team;
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstX, firstY))
				{
					Uint32& caseMask = game.map.getCase(x, y).*target.caseMask;
					if (add)
						caseMask |= teamBit;
					else
						caseMask &= ~teamBit; // clears the team bit, same as the old `mask ^= mask & teamBit`
					target.view.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), add);
				}
	}
	else
		assert(false);
	lastPlacementX=mapX;
	lastPlacementY=mapY;
}



MapEdit::AreaBrushTarget MapEdit::areaBrushTarget()
{
	switch (brushType)
	{
	case ForbiddenBrush:
		return {&Case::forbidden, game.map.displayedForbiddenView};
	case GuardAreaBrush:
		return {&Case::guardArea, game.map.displayedGuardAreaView};
	case ClearAreaBrush:
		return {&Case::clearArea, game.map.displayedClearAreaView};
	default:
		assert(false);
		return {&Case::forbidden, game.map.displayedForbiddenView};
	}
}



void MapEdit::handleTerrainClick(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
// 	if (brushAccumulator.getAreaSurface() > 32*32)
// 	{
// 		sendBrushOrders();
// 	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx+(terrainType>TerrainSelector::Water ? 0 : 16), my+(terrainType>TerrainSelector::Water ? 0 : 16), &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;
		
	if(lastPlacementX == -1)
		firstPlacement = FirstPlacement{mapX, mapY};
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(BrushApplication(mapX, mapY, fig), &game.map);
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimXMinus(fig);
	int startY = mapY-BrushTool::getBrushDimYMinus(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
	const int firstX = firstPlacement ? firstPlacement->x : -1;
	const int firstY = firstPlacement ? firstPlacement->y : -1;
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
		{
			for (int x=startX; x<startX+width; x++)
			{
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstX, firstY))
				{
					int resToSet=-1;
					switch(terrainType)
					{
					case TerrainSelector::Grass:
						game.removeUnitAndBuildingAndFlags(x, y, 3, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setUMatPos(x, y, GRASS, 1);
						// a tile is drawn from the undermap corners at (x..x+1, y..y+1), so cells
						// painted at (x-1..x+1) change the tiles from (x-2, y-2) on; only the
						// ressources those tiles no longer allow go
						game.map.removeUnallowedRessources(x-2, y-2, 4, 4);
						// grass is also the brush that clears: the tiles the cell touches go bare
						for (int ty=y-1; ty<=y; ty++)
							for (int tx=x-1; tx<=x; tx++)
								game.map.getRessource(tx, ty).clear();
						break;
					case TerrainSelector::Sand:
						game.removeUnitAndBuildingAndFlags(x, y, 2, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setUMatPos(x, y, SAND, 1);
						// a tile is drawn from the undermap corners at (x..x+1, y..y+1), so cells
						// painted at (x-1..x+1) change the tiles from (x-2, y-2) on; only the
						// ressources those tiles no longer allow go
						game.map.removeUnallowedRessources(x-2, y-2, 4, 4);
						break;
					case TerrainSelector::Water:
						game.removeUnitAndBuildingAndFlags(x, y, 5, Game::DEL_BUILDING | Game::DEL_UNIT);
						game.map.setUMatPos(x, y, WATER, 1);
						// a tile is drawn from the undermap corners at (x..x+1, y..y+1), so cells
						// painted at (x-1..x+1) change the tiles from (x-2, y-2) on; only the
						// ressources those tiles no longer allow go
						game.map.removeUnallowedRessources(x-2, y-2, 4, 4);
						break;
					case TerrainSelector::Wheat:
						resToSet=CORN;
						break;
					case TerrainSelector::Trees:
						resToSet=WOOD;
						break;
					case TerrainSelector::Stone:
						resToSet=STONE;
						break;
					case TerrainSelector::Algae:
						resToSet=ALGA;
						break;
					case TerrainSelector::Papyrus:
						resToSet=PAPYRUS;
						break;
					case TerrainSelector::CherryTree:
						resToSet=CHERRY;
						break;
					case TerrainSelector::OrangeTree:
						resToSet=ORANGE;
						break;
					case TerrainSelector::PruneTree:
						resToSet=PRUNE;
						break;
					case TerrainSelector::NoTerrain:
						break;
					}
					if(resToSet!=-1 && game.map.isRessourceAllowed(x, y, resToSet))
					{
						game.map.setRessource(x, y, resToSet, 1);
					}
				}
			}
		}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstX, firstY))
				{
					switch(terrainType)
					{
					case TerrainSelector::Sand:
					case TerrainSelector::Water:
						game.map.setUMatPos(x, y, GRASS, 1);
						game.map.removeUnallowedRessources(x-2, y-2, 4, 4);
						for (int ty=y-1; ty<=y; ty++)
							for (int tx=x-1; tx<=x; tx++)
								game.map.getRessource(tx, ty).clear();
						break;
					case TerrainSelector::Wheat:
						if(game.map.isRessourceTakeable(x, y, CORN))
							game.map.setNoRessource(x, y, 1);
						break;
					case TerrainSelector::Trees:
						if(game.map.isRessourceTakeable(x, y, WOOD))
							game.map.setNoRessource(x, y, 1);
						break;
					case TerrainSelector::Stone:
						if(game.map.isRessourceTakeable(x, y, STONE))
							game.map.setNoRessource(x, y, 1);
						break;
					case TerrainSelector::Algae:
						if(game.map.isRessourceTakeable(x, y, ALGA))
							game.map.setNoRessource(x, y, 1);
						break;
					case TerrainSelector::Papyrus:
						if(game.map.isRessourceTakeable(x, y, PAPYRUS))
							game.map.setNoRessource(x, y, 1);
						break;
					case TerrainSelector::CherryTree:
					case TerrainSelector::OrangeTree:
					case TerrainSelector::PruneTree:
						if(game.map.isRessourceTakeable(x, y, CHERRY)
						|| game.map.isRessourceTakeable(x, y, ORANGE)
						|| game.map.isRessourceTakeable(x, y, PRUNE))
							game.map.setNoRessource(x, y, 1);
						break;
					case TerrainSelector::Grass:
					case TerrainSelector::NoTerrain:
						break;
					}
				}
	}
	else
		assert(false);
	lastPlacementX=mapX;
	lastPlacementY=mapY;
}

void MapEdit::handleClick(int mx, int my, BrushTool::ClickType clickType)
{
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	if(lastPlacementX==mapX && lastPlacementY==mapY)
		return;

	if(lastPlacementX == -1)
		firstPlacement = FirstPlacement{mapX, mapY};
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(BrushApplication(mapX, mapY, fig), &game.map);
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimXMinus(fig);
	int startY = mapY-BrushTool::getBrushDimYMinus(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// BrushTool treats -1 as "no stroke origin" for checkerboard parity alignment
	const int firstX = firstPlacement ? firstPlacement->x : -1;
	const int firstY = firstPlacement ? firstPlacement->y : -1;
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstX, firstY))
				{
					switch(clickType)
					{
					case BrushTool::CT_DELETE:
						game.removeUnitAndBuildingAndFlags(x, y, 1, Game::DEL_BUILDING | Game::DEL_UNIT | Game::DEL_FLAG);
						game.regenerateDiscoveryMap();
						break;
					case BrushTool::CT_AREA:
						game.map.setPoint(areaNumber->getIndex(), x, y);
						break;
					case BrushTool::CT_NO_RESOURCE_GROWTH:
						game.map.getCase(x, y).canRessourcesGrow=false;
						break;
					}
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY, mapX, mapY, firstX, firstY))
				{
					switch(clickType)
					{
					case BrushTool::CT_AREA:
						game.map.unsetPoint(areaNumber->getIndex(), x, y);
						break;
					case BrushTool::CT_NO_RESOURCE_GROWTH:
						game.map.getCase(x, y).canRessourcesGrow=true;
						break;
					default:break;
					}
				}
	}
	lastPlacementX=mapX;
	lastPlacementY=mapY;
	game.regenerateDiscoveryMap();
}
void MapEdit::handleDeleteClick(int mx, int my)
{
	handleClick(mx,my,BrushTool::CT_DELETE);
}



void MapEdit::handleAreaClick(int mx, int my)
{
	handleClick(mx,my,BrushTool::CT_AREA);
}



void MapEdit::handleNoRessourceGrowthClick(int mx, int my)
{
	handleClick(mx,my,BrushTool::CT_NO_RESOURCE_GROWTH);
}


void MapEdit::regenerateGameHeader()
{
	GameHeader gameHeader;
	MapHeader& mapHeader = game.mapHeader;
	
	int playerNumber=0;
	for (int i=0; i<mapHeader.getNumberOfTeams(); i++)
	{
		if (i==0)
		{
			std::string name = FormatableString("Player %0").arg(playerNumber);
			gameHeader.getBasePlayer(i) = BasePlayer(playerNumber, name.c_str(), i, BasePlayer::P_LOCAL);
		}
		else
		{
			std::string name = FormatableString("AI Player %0").arg(playerNumber);
			gameHeader.getBasePlayer(i) = BasePlayer(playerNumber, name.c_str(), i, BasePlayer::P_AI);
		}
		playerNumber+=1;
	}
	gameHeader.setNumberOfPlayers(playerNumber);
	game.setGameHeader(gameHeader);
}


