/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at nct@ysagoon.com or nuage@ysagoon.com

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "GAG.h"
#include "Game.h"
#include "MapEdit.h"
#include "UnitType.h"
#include "Utilities.h"
#include "GameGUILoadSave.h"
#include "ScriptEditorScreen.h"
#include "GlobalContainer.h"
#include <math.h>


MapEdit::MapEdit()
{
	// default value;
	viewportX=0;
	viewportY=0;
	for (int i=0; i<9; i++)
	{
		viewportSpeedX[i]=0;
		viewportSpeedY[i]=0;
	}
	centeredTeam=0;

	// set the editor default values
	team=0;
	terrainSize=1; // terrain size 1
	level=0;
	type=WATER; // water

	editMode=EM_TERRAIN; // terrain
	wasClickInMap=false;
	minimapPushed=false;
	pushedBrush=EM_NONE;

	// load menu
	globalContainer->gfx->loadSprite("data/gui/editor", "editor");
	menu=Toolkit::getSprite("editor");
	// TODO FIXME here:
	font=globalContainer->littleFont; //globalContainer->gfx->loadFont("data/fonts/arial8black.png");

	// static-like variables:
	savedMx=0;
	savedMy=0;
	oldBrush=EM_NONE;
	orX=orY=orW=orH=0;

	// editor facilities
	hasMapBeenModiffied=false;
}

MapEdit::~MapEdit()
{
	Toolkit::releaseSprite("editor");
	//delete font;
}

void MapEdit::drawMap(int sx, int sy, int sw, int sh, bool needUpdate, bool doPaintEditMode)
{
	Utilities::rectClipRect(sx, sy, sw, sh, mapClip);

	globalContainer->gfx->setClipRect(sx, sy, sw, sh);

	game.drawMap(sx, sy, sw, sh, viewportX, viewportY, team, false, false, true, true);
	if (doPaintEditMode)
		paintEditMode(false, false);

	globalContainer->gfx->setClipRect(screenClip.x, screenClip.y, screenClip.w, screenClip.h);

	if (needUpdate)
		globalContainer->gfx->updateRect(sx, sy, sw, sh);
}

void MapEdit::drawMiniMap(void)
{
	game.drawMiniMap(globalContainer->gfx->getW()-128, 0, 128, 128, viewportX, viewportY);
	paintCoordinates();
	globalContainer->gfx->updateRect(globalContainer->gfx->getW()-128, 0, 128, 128);
}

void MapEdit::renderMiniMap(void)
{
	game.renderMiniMap(-1);
	drawMiniMap();
}

void MapEdit::drawMenu(void)
{
	int menuStartW=globalContainer->gfx->getW()-128;

	// draw buttons
	globalContainer->gfx->drawSprite(menuStartW+0, 128, menu, 0);
	globalContainer->gfx->drawSprite(menuStartW+0, 166, menu, 0);
	globalContainer->gfx->drawSprite(menuStartW+0, 268, menu, 0);
	globalContainer->gfx->drawSprite(menuStartW+0, 454, menu, 0);
	globalContainer->gfx->drawSprite(menuStartW+0, 134, menu, 1);
	globalContainer->gfx->drawSprite(menuStartW+32, 134, menu, 2);
	globalContainer->gfx->drawSprite(menuStartW+64, 134, menu, 3);
	globalContainer->gfx->drawSprite(menuStartW+96, 134, menu, 4);
	globalContainer->gfx->drawSprite(menuStartW+0, 172, menu, 5);
	globalContainer->gfx->drawSprite(menuStartW+32, 172, menu, 6);
	globalContainer->gfx->drawSprite(menuStartW+64, 172, menu, 7);
	globalContainer->gfx->drawSprite(menuStartW+96, 172, menu, 8);

	// ressources
	globalContainer->gfx->drawFilledRect(menuStartW+0, 204, 128, 32, 0, 0, 0);
	unsigned resCount=globalContainer->ressourcesTypes.size();
	unsigned halfResCount=(unsigned)ceil(resCount*0.5f);
	unsigned i;
	unsigned resWidth=126/halfResCount;
	for (i=0; i<halfResCount; i++)
	{
		const RessourceType *rt=globalContainer->ressourcesTypes.get(i);
		unsigned t=rt->terrain;
		unsigned img=rt->gfxId+rt->sizesCount-1;
		globalContainer->gfx->setClipRect(menuStartW+1+i*resWidth, 205, resWidth, 15);
		globalContainer->gfx->drawSprite(menuStartW+1+i*resWidth, 204, menu, t+5);
		globalContainer->gfx->drawSprite(menuStartW+1+i*resWidth-8, 204-8, globalContainer->ressources, img);
	}
	for (;i<resCount; i++)
	{
		const RessourceType *rt=globalContainer->ressourcesTypes.get(i);
		unsigned t=rt->terrain;
		unsigned img=rt->gfxId+rt->sizesCount-1;
		globalContainer->gfx->setClipRect(menuStartW+1+(i-halfResCount)*resWidth, 205+16, resWidth, 15);
		globalContainer->gfx->drawSprite(menuStartW+1+(i-halfResCount)*resWidth, 204+16, menu, t+5);
		globalContainer->gfx->drawSprite(menuStartW+1+(i-halfResCount)*resWidth-8, 204+8, globalContainer->ressources, img);
	}
	globalContainer->gfx->setClipRect();

	/*globalContainer->gfx->drawSprite(menuStartW+0, 205, menu, 9);
	globalContainer->gfx->drawSprite(menuStartW+32, 205, menu, 10);
	globalContainer->gfx->drawSprite(menuStartW+64, 205, menu, 11);
	globalContainer->gfx->drawSprite(menuStartW+96, 205, menu, 12);*/

	globalContainer->gfx->drawSprite(menuStartW+0, 236, menu, 13);
	globalContainer->gfx->drawSprite(menuStartW+32, 236, menu, 14);
	globalContainer->gfx->drawSprite(menuStartW+64, 236, menu, 15);
	globalContainer->gfx->drawSprite(menuStartW+96, 236, menu, 16);
	//globalContainer->gfx->drawSprite(menu(17), menuStartW+0, 275);
	//globalContainer->gfx->drawSprite(menu(18), menuStartW+32, 275);
	//globalContainer->gfx->drawSprite(menu(19), menuStartW+64, 275);
	//globalContainer->gfx->drawSprite(menu(20), menuStartW+96, 275);
	/*globalContainer->gfx->drawSprite(menu(21), menuStartW+0, 307);
	globalContainer->gfx->drawSprite(menu(22), menuStartW+32, 307);
	globalContainer->gfx->drawSprite(menu(23), menuStartW+64, 307);
	globalContainer->gfx->drawSprite(menu(24), menuStartW+96, 307);
	globalContainer->gfx->drawSprite(menu(25), menuStartW+0, 339);
	globalContainer->gfx->drawSprite(menu(26), menuStartW+32, 339);
	globalContainer->gfx->drawSprite(menu(27), menuStartW+64, 339);
	globalContainer->gfx->drawSprite(menu(28), menuStartW+96, 339);*/
	globalContainer->gfx->drawSprite(menuStartW+0, 402, menu, 29);
	globalContainer->gfx->drawSprite(menuStartW+0, 454, menu, 30);
	globalContainer->gfx->drawSprite(menuStartW+32, 454, menu, 31);
	globalContainer->gfx->drawSprite(menuStartW+64, 454, menu,  32);
	globalContainer->gfx->drawSprite(menuStartW+96, 454, menu, 33);

	// draw units and buildings
	globalContainer->gfx->drawFilledRect(menuStartW, 274, 128, 128, 0, 0, 0);

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);

	globalContainer->gfx->drawSprite(menuStartW+0, 274, unitSprite, 64);
	globalContainer->gfx->drawSprite(menuStartW+32, 274, unitSprite, 0);
	globalContainer->gfx->drawSprite(menuStartW+64, 274, unitSprite, 256);

	// draw buildings
	for (int i=0; i<13; i++)
	{
		int typeNum;
		if ((i!=0) && (i<8))
			typeNum=globalContainer->buildingsTypes.getTypeNum(i, ((level>2) ? 2 : level) , false);
		else
			typeNum=globalContainer->buildingsTypes.getTypeNum(i, 0, false);
		assert(typeNum!=-1);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
		assert(bt);
		int imgid=bt->startImage;
		int x=((i&0x3)<<5)+menuStartW;
		int y=((i>>2)<<5)+306;
		if (i==12)
		{
			x=96+menuStartW;
			y=274;
		}

		globalContainer->gfx->setClipRect( x+1, y+1, 30, 30);
		Sprite *buildingSprite=globalContainer->buildings;
		int w=buildingSprite->getW(imgid);
		int h=buildingSprite->getH(imgid);
		int decW=20, decH=20;
		if (w<=32)
			decW=0;
		if (h<=32)
			decH=0;
		buildingSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);
		globalContainer->gfx->drawSprite(x-decW, y-decH, buildingSprite, imgid);
	}
	globalContainer->gfx->setClipRect(screenClip.x, screenClip.y, screenClip.w, screenClip.h);

	// draw selections
	drawSelRect(menuStartW+((terrainSize-1)*16), 236, 32, 32);
	drawSelRect(menuStartW+(level*32), 454, 32, 26);
	if (editMode==EM_TERRAIN)
		drawSelRect(menuStartW+(type*32), 172, 32, 32);
	else if (editMode==EM_RESSOURCE)
	{
		//drawSelRect(menuStartW+(type*25)+2, 204, 25, 32);
		if (type<(int)halfResCount)
			drawSelRect(menuStartW+(type*resWidth)+2, 204, resWidth, 16);
		else
			drawSelRect(menuStartW+((type-halfResCount)*resWidth)+2, 204+16, resWidth, 16);
	}
	else if (editMode==EM_UNIT)
		drawSelRect(menuStartW+(type*32), 274, 32, 32);
	else if (editMode==EM_BUILDING)
		if (type<4)
			drawSelRect(menuStartW+(type*32), 306, 32, 32);
		else
			drawSelRect(menuStartW+((type-4)*32), 338, 32, 32);
	else if (editMode==EM_DELETE)
		drawSelRect(menuStartW+64, 134, 32, 32);

	// draw teams
	if (game.session.numberOfTeam<=8)
	{
		for (int i=0; i<game.session.numberOfTeam; i++)
		{
			int line=i/4;
			int dec=i%4;
			globalContainer->gfx->drawFilledRect(menuStartW+12+1+dec*26, 402+1+line*26, 24, 24, game.teams[i]->colorR, game.teams[i]->colorG, game.teams[i]->colorB);
		}
	}
	else
	{
		for (int i=0; i<game.session.numberOfTeam; i++)
		{
			int line=i/8;
			int dec=i%8;
			globalContainer->gfx->drawFilledRect(menuStartW+12+1+dec*13, 402+1+line*13, 11, 11, game.teams[i]->colorR, game.teams[i]->colorG, game.teams[i]->colorB);
		}
	}

	// draw team selection
	if (game.session.numberOfTeam<=8)
	{
		int line, dec;
		for (int i=0; i<game.session.numberOfTeam; i++)
		{
			line=i/4;
			dec=i%4;
			if (game.teams[team]->allies & game.teams[i]->me)
				globalContainer->gfx->drawFilledRect(menuStartW+20+dec*26, 410+line*26, 10, 10, game.teams[team]->colorR, game.teams[team]->colorG,  game.teams[team]->colorB);
		}

		line=team/4;
		dec=team%4;
		globalContainer->gfx->drawRect(menuStartW+12+dec*26, 402+line*26, 26, 26, 255, 0, 0);
		globalContainer->gfx->drawRect(menuStartW+13+dec*26, 403+line*26, 24, 24, 0, 0, 0);

	}
	else
	{
		int line, dec;
		for (int i=0; i<game.session.numberOfTeam; i++)
		{
			line=i/8;
			dec=i%8;
			if (game.teams[team]->allies & game.teams[i]->me)
				globalContainer->gfx->drawFilledRect(menuStartW+16+dec*13, 406+line*13, 5, 5, game.teams[team]->colorR, game.teams[team]->colorG,  game.teams[team]->colorB);
		}

		line=team/8;
		dec=team%8;
		globalContainer->gfx->drawRect(menuStartW+12+dec*13, 402+line*13, 13, 13, 255, 0, 0);
		globalContainer->gfx->drawRect(menuStartW+13+dec*13, 403+line*13, 11, 11, 0, 0, 0);

	}

	globalContainer->gfx->updateRect(menuStartW, 128, 128, globalContainer->gfx->getH()-128);
}

void MapEdit::draw(void)
{
	drawMap(screenClip.x, screenClip.y, screenClip.w-128, screenClip.h, true, true);
	renderMiniMap();
	drawMenu();
}

void MapEdit::handleMapClick(void)
{
	handleMapClick(savedMx, savedMy);
}

void MapEdit::handleMapClick(int mx, int my)
{
	int winX, winW, winY, winH;
	static int ax=0, ay=0, atype=0;
	int x=ax, y=ay;
	bool needRedraw=false;

	if (editMode==EM_TERRAIN)
	{
		game.map.displayToMapCaseUnaligned(mx, my, &x, &y, viewportX, viewportY);
		if ((ax!=x)||(ay!=y)||(atype!=type))
		{
			game.map.setUMatPos(x, y, (TerrainType)type, terrainSize);
			needRedraw=true;

			winX=((mx+16)&0xFFFFFFE0)-((terrainSize>>1)<<5)-64;
			winW=(terrainSize+3)<<5;
			winY=((my+16)&0xFFFFFFE0)-((terrainSize>>1)<<5)-64;
			winH=(terrainSize+3)<<5;

			int dec;
			if ((type==WATER) || (type==SAND))
			{
				if (type==WATER)
					dec=3;
				else
					dec=1;

				int delX, delY, delW, delH;
				delX=0;
				delY=0;
				delW=0;
				delH=0;

				SDL_Rect r;
				if (game.removeUnitAndBuilding(x, y, terrainSize+dec, &r, Game::DEL_BUILDING))
				{
					game.map.mapCaseToDisplayable(r.x, r.y, &delX, &delY, viewportX, viewportY);
					delW=r.w<<5;
					delH=r.h<<5;
				}
				Utilities::rectExtendRect(delX, delY, delW, delH,  &winX, &winY, &winW, &winH);
			}
			if (type==SAND)
				game.map.setNoRessource(x, y, terrainSize+1);
			else
				game.map.setNoRessource(x, y, terrainSize+3);
			
			updateUnits(x-(terrainSize>>1)-2, y-(terrainSize>>1)-2, terrainSize+3, terrainSize+3);
		}
	}
	else if (editMode==EM_RESSOURCE)
	{
		game.map.displayToMapCaseAligned(mx, my, &x, &y, viewportX, viewportY);
 		if ((ax!=x)||(ay!=y)||(atype!=type))
		{
			game.map.setRessource(x, y, type, terrainSize);
			needRedraw=true;

			winX=((mx)&0xFFFFFFE0)-((terrainSize>>1)<<5);
			winW=(terrainSize+1)<<5;
			winY=((my)&0xFFFFFFE0)-((terrainSize>>1)<<5);
			winH=(terrainSize+1)<<5;

			int delX, delY, delW, delH;

			SDL_Rect r;
			if (game.removeUnitAndBuilding(x, y, terrainSize, &r))
			{
				game.map.mapCaseToDisplayable(r.x, r.y, &delX, &delY, viewportX, viewportY);
				delW=r.w<<5;
				delH=r.h<<5;
				Utilities::rectExtendRect(delX, delY, delW, delH,  &winX, &winY, &winW, &winH);
			}
		}
	}
	else if (editMode==EM_UNIT)
	{
		game.map.displayToMapCaseAligned(mx, my, &x, &y, viewportX, viewportY);

		Unit *unit=game.addUnit(x, y, team, type, level, rand()%256, 0, 0);
		if (unit)
		{
			game.regenerateDiscoveryMap();

			winX=mx&0xFFFFFFE0;
			winY=my&0xFFFFFFE0;
			winW=32;
			winH=32;
			needRedraw=true;
		}
	}
	else if (editMode==EM_BUILDING)
	{
		//game.map.displayToMapCaseUnaligned(mx, my, &x, &y, viewportX, viewportY);
		int typeNum=globalContainer->buildingsTypes.getTypeNum(type, level, false);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);

		int tempX, tempY;
		game.map.cursorToBuildingPos(mx, my, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);

		if (game.checkRoomForBuilding(tempX, tempY, typeNum, &x, &y, -1))
		{
			game.addBuilding(x, y, typeNum, team);
			if ((type==0) && (level==0))
			{
				game.teams[team]->startPosX=tempX;
				game.teams[team]->startPosY=tempY;
			}
			game.regenerateDiscoveryMap();
			winX=(x-viewportX)&game.map.getMaskW();
			winY=(y-viewportY)&game.map.getMaskH();
			winW=bt->width<<5;
			winH=bt->height<<5;
			needRedraw=true;
		}
	}
	else if (editMode==EM_DELETE)
	{
		game.map.displayToMapCaseAligned(mx, my, &x, &y, viewportX, viewportY);
		SDL_Rect r;
		if (game.removeUnitAndBuilding(x, y, terrainSize, &r))
		{
			game.regenerateDiscoveryMap();
			game.map.mapCaseToDisplayable(r.x, r.y, &winX, &winY, viewportX, viewportY);
			winW=r.w<<5;
			winH=r.h<<5;

			needRedraw=true;
		}
	}

	if (needRedraw)
	{
		drawMap(winX, winY, winW, winH, true, false);
		mapHasBeenModiffied();
		//renderMiniMap();
	}

	atype=type;
	ax=x;
	ay=y;
}

void MapEdit::paintCoordinates(void)
{
	return this->paintCoordinates(savedMx, savedMy);
}

void MapEdit::paintCoordinates(int mx, int my)
{
	int baseX = globalContainer->gfx->getW()-128;
	int h=font->getStringHeight("(888,888)");
	int y=128-h;
	globalContainer->gfx->drawFilledRect(baseX, y, 128, h, 0, 0, 0);
	if ((mx < baseX) || (my < 128))
	{
		int px, py;
		if (mx > baseX) // display coordinates according to minimap
		{
			int mMax;
			int szX, szY;
			int decX, decY;
			Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);
			mx-=baseX+14+decX;
			my-=14+decY;
			px=((mx*game.map.getW())/szX)&game.map.getMaskW();
			py=((my*game.map.getH())/szY)&game.map.getMaskH();
		}
		else
		{
			if (editMode==EM_TERRAIN)
				game.map.displayToMapCaseUnaligned(mx, my, &px, &py, viewportX, viewportY);
			else
				game.map.displayToMapCaseAligned(mx, my, &px, &py, viewportX, viewportY);

		}
		std::string s(GAG::nsprintf("(%d,%d)", px, py));
		int w=font->getStringWidth(s.c_str());
		int x=baseX+64-w/2;
		globalContainer->gfx->drawString(x, y, font, s.c_str());
	}
	globalContainer->gfx->updateRect(baseX, y, 128, h);
}

void MapEdit::paintEditMode(bool clearOld, bool mayUpdate)
{
	paintEditMode(savedMx, savedMy, clearOld, mayUpdate);
}

void MapEdit::paintEditMode(int mx, int my, bool clearOld, bool mayUpdate)
{
	
	// We show the case coordodinates
	const int maxNbRefreshZones=2;
	SDL_Rect refreshZones[maxNbRefreshZones];
	int nbRefreshZones=0;
	
	if (clearOld && oldBrush!=EM_NONE && mayUpdate)
	{
		drawMap(orX, orY, orW, orH, false, false);

		refreshZones[nbRefreshZones].x=orX;
		refreshZones[nbRefreshZones].y=orY;
		refreshZones[nbRefreshZones].w=orW;
		refreshZones[nbRefreshZones].h=orH;
		nbRefreshZones++;
	}
	
	if ( (editMode==EM_TERRAIN) || (editMode==EM_RESSOURCE) || (editMode==EM_DELETE) )
	{
		//terrainSize
		int x, y, w, h;
		if (editMode==EM_TERRAIN)
		{
			x=((mx+16)&0xFFFFFFE0)-((terrainSize>>1)<<5)-16;
			w=(terrainSize)<<5;
			y=((my+16)&0xFFFFFFE0)-((terrainSize>>1)<<5)-16;
			h=(terrainSize)<<5;
		}
		else
		{
			x=((mx)&0xFFFFFFE0)-((terrainSize>>1)<<5);
			y=((my)&0xFFFFFFE0)-((terrainSize>>1)<<5);
			w=(terrainSize)<<5;
			h=(terrainSize)<<5;
		}

		Utilities::rectClipRect(x, y, w, h, mapClip);

		globalContainer->gfx->drawRect(x, y, w, h, 255, 255, 255, 128);

		refreshZones[nbRefreshZones].x=x;
		refreshZones[nbRefreshZones].y=y;
		refreshZones[nbRefreshZones].w=w;
		refreshZones[nbRefreshZones].h=h;
		nbRefreshZones++;

		orX=x;
		orY=y;
		orW=w;
		orH=h;

		oldBrush=editMode;
	}
	else if (editMode==EM_UNIT)
	{

		int cx=(mx>>5)+viewportX;
		int cy=(my>>5)+viewportY;

		int px=mx&0xFFFFFFE0;
		int py=my&0xFFFFFFE0;
		int pw=32;
		int ph=32;

		bool isRoom;
		if (type==EXPLORER)
			isRoom=game.map.isFreeForAirUnit(cx, cy);
		else
		{
			UnitType *ut=game.teams[team]->race.getUnitType(type, level);
			isRoom=game.map.isFreeForGroundUnit(cx, cy, ut->performance[SWIM], Team::teamNumberToMask(team));
		}

		int imgid;
		if (type==WORKER)
			imgid=64;
		else if (type==EXPLORER)
			imgid=0;
		else if (type==WARRIOR)
			imgid=256;
		else
		{
			imgid=0;
			assert(false);
		}

		Sprite *unitSprite=globalContainer->units;
		unitSprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);

		globalContainer->gfx->setClipRect(mapClip.x, mapClip.y, mapClip.w, mapClip.h);

		globalContainer->gfx->drawSprite(px, py, unitSprite, imgid);

		Utilities::rectClipRect(px, py, pw, ph, mapClip);
		if (isRoom)
			globalContainer->gfx->drawRect(px, py, pw, ph, 255, 255, 255, 128);
		else
			globalContainer->gfx->drawRect(px, py, pw, ph, 255, 0, 0, 128);

		globalContainer->gfx->setClipRect(screenClip.x, screenClip.y, screenClip.w, screenClip.h);

		refreshZones[nbRefreshZones].x=px;
		refreshZones[nbRefreshZones].y=py;
		refreshZones[nbRefreshZones].w=pw;
		refreshZones[nbRefreshZones].h=ph;
		nbRefreshZones++;

		orX=px;
		orY=py;
		orW=pw;
		orH=ph;
		oldBrush=EM_UNIT;
	}
	else if (editMode==EM_BUILDING)
	{
		int mapX, mapY;
		int batX, batY, batW, batH;

		// we get the type of building
		int typeNum=globalContainer->buildingsTypes.getTypeNum(type, level, false);
		assert(typeNum!=-1);
		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);

		// we check for room
		int tempX, tempY;
		if (bt->width&0x1)
			tempX=((mx)>>5)+viewportX;
		else
			tempX=((mx+16)>>5)+viewportX;

		if (bt->height&0x1)
			tempY=((my)>>5)+viewportY;
		else
			tempY=((my+16)>>5)+viewportY;
		bool isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, -1);

		// we get the datas
		Sprite *sprite=globalContainer->buildings;
		sprite->setBaseColor(game.teams[team]->colorR, game.teams[team]->colorG, game.teams[team]->colorB);

		batX=(mapX-viewportX)<<5;
		batY=(mapY-viewportY)<<5;
		batW=(bt->width)<<5;
		batH=(bt->height)<<5;

		globalContainer->gfx->setClipRect(mapClip.x, mapClip.y, mapClip.w, mapClip.h);
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->startImage);

		Utilities::rectClipRect(batX, batY, batW, batH, mapClip);
		assert(batW>=0);
		assert(batH>=0);

		if (isRoom)
			globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 128);
		else
			globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 128);

		if (isRoom)
		{
			BuildingType *nnbt=bt;
			int max=0;
			while(nnbt->nextLevelTypeNum!=-1)
			{
				nnbt=globalContainer->buildingsTypes.get(nnbt->nextLevelTypeNum);
				if (max++>200)
				{
					printf("MapEdit: Error: nextLevelTypeNum architecture is broken.\n");
					assert(false);
					break;
				}
			}
			int typeNum=nnbt->typeNum;

			isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, -1);

			batX=(mapX-viewportX)<<5;
			batY=(mapY-viewportY)<<5;
			batW=(nnbt->width)<<5;
			batH=(nnbt->height)<<5;

			Utilities::rectClipRect(batX, batY, batW, batH, mapClip);
			assert(batW>=0);
			assert(batH>=0);

			if (isRoom)
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 128);
			else
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 128);

		}

		refreshZones[nbRefreshZones].x=batX;
		refreshZones[nbRefreshZones].y=batY;
		refreshZones[nbRefreshZones].w=batW;
		refreshZones[nbRefreshZones].h=batH;
		nbRefreshZones++;
		orX=batX;
		orY=batY;
		orW=batW;
		orH=batH;

		globalContainer->gfx->setClipRect(screenClip.x, screenClip.y, screenClip.w, screenClip.h);
		oldBrush=EM_BUILDING;
	}

	assert(nbRefreshZones<=maxNbRefreshZones);
	if (nbRefreshZones>0 && mayUpdate)
		globalContainer->gfx->updateRects(refreshZones, nbRefreshZones);
}

void MapEdit::mapHasBeenModiffied(void)
{
	hasMapBeenModiffied=true;
	if (game.session.mapGenerationDescriptor)
	{
		// If the map is modiffied, this is no longer a RandomGeneratedMap.
		printf("because you modified the map, it can't be directly generated automatically any more.\n");
		delete game.session.mapGenerationDescriptor;
		game.session.mapGenerationDescriptor=NULL;
	}
	
}

void MapEdit::regenerateClipRect(void)
{
	screenClip.x=0;
	screenClip.y=0;
	screenClip.w=globalContainer->gfx->getW();
	screenClip.h=globalContainer->gfx->getH();
	mapClip.x=0;
	mapClip.y=0;
	mapClip.w=screenClip.w-128;
	mapClip.h=screenClip.h;
	viewportW=mapClip.w>>5;
	viewportH=mapClip.h>>5;
}

void MapEdit::drawSelRect(int x, int y, int w, int h)
{
	globalContainer->gfx->drawRect(x, y, w, h, 255, 0, 0);
	globalContainer->gfx->drawRect(x+1, y+1, w-2, h-2, 255, 0, 0);
}

void MapEdit::loadSave(bool isLoad)
{
	// create dialog box
	LoadSaveScreen *loadSaveScreen=new LoadSaveScreen("maps", "map", isLoad, game.session.getMapName(), glob2FilenameToName, glob2NameToFilename);
	loadSaveScreen->dispatchPaint(loadSaveScreen->getSurface());

	// save screen
	globalContainer->gfx->setClipRect();

	SDL_Event event;
	while(loadSaveScreen->endValue<0)
	{
		while (SDL_PollEvent(&event))
		{
			loadSaveScreen->translateAndProcessEvent(&event);
		}
		globalContainer->gfx->drawSurface(loadSaveScreen->decX, loadSaveScreen->decY, loadSaveScreen->getSurface());
		globalContainer->gfx->updateRect(loadSaveScreen->decX, loadSaveScreen->decY, loadSaveScreen->getW(), loadSaveScreen->getH());
	}

	if (loadSaveScreen->endValue==0)
	{
		if (isLoad)
		{
			load(loadSaveScreen->getFileName());
		}
		else
		{
			if (save(loadSaveScreen->getFileName(), loadSaveScreen->getName()))
				hasMapBeenModiffied=false;
		}
	}

	// clean up
	delete loadSaveScreen;

	draw();
}

void MapEdit::scriptEditor(void)
{
	// create dialog box
	ScriptEditorScreen *scriptEditorScreen=new ScriptEditorScreen(&(game.script), &game);
	scriptEditorScreen->dispatchPaint(scriptEditorScreen->getSurface());

	// save screen
	globalContainer->gfx->setClipRect();

	SDL_Event event;
	while(scriptEditorScreen->endValue<0)
	{
		while (SDL_PollEvent(&event))
		{
			scriptEditorScreen->translateAndProcessEvent(&event);
		}
		globalContainer->gfx->drawSurface(scriptEditorScreen->decX, scriptEditorScreen->decY, scriptEditorScreen->getSurface());
		globalContainer->gfx->updateRect(scriptEditorScreen->decX, scriptEditorScreen->decY, scriptEditorScreen->getW(), scriptEditorScreen->getH());
	}
	if (scriptEditorScreen->endValue == ScriptEditorScreen::OK)
		mapHasBeenModiffied();

	// clean up
	delete scriptEditorScreen;

	draw();
}

void MapEdit::askConfirmationToQuit()
{
	if (hasMapBeenModiffied)
	{
		const char *reallyquit = Toolkit::getStringTable()->getString("[save before quit?]");
		const char *yes = Toolkit::getStringTable()->getString("[Yes]");
		const char *no = Toolkit::getStringTable()->getString("[No]");
		const char *save = Toolkit::getStringTable()->getString("[Cancel]");
		int res=(int)MessageBox(globalContainer->gfx, "standard", MB_THREEBUTTONS, reallyquit, yes, no, save);

		if (res==1) // no, quit
			isRunning=false;
		else if (res==2) // cancel, don't quit
			draw();
		else if (res==0) // save if needed
		{
			loadSave(false);
			if (!hasMapBeenModiffied)
				isRunning=false;
		}
		else
			assert(false);
	}
	else
		isRunning=false;
}

void MapEdit::handleMenuClick(int mx, int my, int button)
{
	mx-=globalContainer->gfx->getW()-128;
	if ((my>134) && (my<166))
	{
		if (mx<32)
			loadSave(true);
		else if (mx<64)
			loadSave(false);
		else if (mx<96)
			editMode=EM_DELETE;
		else
			askConfirmationToQuit();
	}
	else if ((my>172) && (my<204))
	{
		if (mx>=96)
		{
			scriptEditor();
			return;
		}
		editMode=EM_TERRAIN;
		type=mx/32;
	}
	else if ((my>204) && (my<236))
	{
		editMode=EM_RESSOURCE;

		unsigned resCount=globalContainer->ressourcesTypes.size();
		unsigned halfResCount=(unsigned)ceil(resCount*0.5f);
		unsigned resWidth=126/halfResCount;

		if ((my-204)<16)
			type=(mx-2)/resWidth;
		else
			type=halfResCount+(mx-2)/resWidth;

		if (type<0)
			type=0;
		else if (type>(int)resCount-1)
			type=resCount-1;

	}
	else if ((my>236) && (my<268))
	{
		int table[4]={1,3,5,7};
		terrainSize=table[mx/32];
	}
	else if ((my>274) && (my<306))
	{
		editMode=EM_UNIT;
		type=mx/32;
		if (type>2)// we have only 3 units.
		{
			editMode=EM_BUILDING;
			level=0;
			type=12;
		}
	}
	else if ((my>306) && (my<402))
	{
		editMode=EM_BUILDING;
		type=((my-306)/32)*4+mx/32;
		if ((type==0) || (type>7))
			level=0;
		if (level>2)
			level=2;
	}
	else if ((my>402) && (my<454))
	{
		if (mx<12)
		{
			// remove a team
			if (game.session.numberOfTeam>1)
			{
				game.removeTeam();
				if (team>=game.session.numberOfTeam)
					team--;
				draw();
			}
		}
		else if (mx>=116)
		{
			// add a team
			if (game.session.numberOfTeam<32)
			{
				game.addTeam();
				draw();
			}
		}
		else
		{
			// select a team
			if (game.session.numberOfTeam<=8)
			{
				int px=(mx-12)/26;
				int py=(my-402)/26;
				int newteam=(py*4)+px;
				if (newteam<game.session.numberOfTeam)
				{
					if (button==SDL_BUTTON_LEFT)
					{
						team=newteam;
						drawMap(screenClip.x, screenClip.y, screenClip.w-128,screenClip.h, true, true);
					}
					else if ((button==SDL_BUTTON_RIGHT) && (team!=newteam))
					{
						game.teams[team]->allies^=(1<<newteam);
						game.teams[team]->enemies^=(1<<newteam);
						game.teams[team]->sharedVisionExchange^=(1<<newteam);
						game.teams[team]->sharedVisionFood^=(1<<newteam);
						game.teams[team]->sharedVisionOther^=(1<<newteam);
					}
					else if (button==SDL_BUTTON_MIDDLE)
					{
						if (game.teams[team]->type == BaseTeam::T_AI)
						{
							game.teams[team]->type = BaseTeam::T_HUMAN;
							printf("MapEdit : switching team %d to human\n", team);
						}
						else
						{
							game.teams[team]->type = BaseTeam::T_AI;
							printf("MapEdit : switching team %d to null AI\n", team);
						}
					}
				}
			}
			else
			{
				int px=(mx-12)/13;
				int py=(my-402)/13;
				int newteam=(py*8)+px;
				if (newteam<game.session.numberOfTeam)
				{
					if (button==SDL_BUTTON_LEFT)
					{
						team=newteam;
						drawMap(screenClip.x, screenClip.y, screenClip.w-128, screenClip.h, true, true);
					}
					else if ((button==SDL_BUTTON_RIGHT) && (team!=newteam))
					{
						game.teams[team]->allies^=(1<<newteam);
						game.teams[team]->enemies^=(1<<newteam);
						game.teams[team]->sharedVisionExchange^=(1<<newteam);
						game.teams[team]->sharedVisionFood^=(1<<newteam);
						game.teams[team]->sharedVisionOther^=(1<<newteam);
					}
					else if (button==SDL_BUTTON_MIDDLE)
					{
						if (game.teams[team]->type == BaseTeam::T_AI)
						{
							game.teams[team]->type = BaseTeam::T_HUMAN;
							printf("MapEdit : switching team %d to human\n", team);
						}
						else
						{
							game.teams[team]->type = BaseTeam::T_AI;
							printf("MapEdit : switching team %d to null AI\n", team);
						}
					}
				}
			}

			// set Team alliance
		}
	}
	else if ((my>454) && (my<480))
	{
		level=mx/32;
		if (editMode==EM_BUILDING)
		{
			if ((type==0) || (type>7))
				level=0;
			if (level>2)
				level=2;
		}
	}

	drawMenu();
}

bool MapEdit::load(const char *filename)
{
	assert(filename);

	SDL_RWops *stream=globalContainer->fileManager->open(filename,"rb");
	if (!stream)
		return false;
	
	bool rv=game.load(stream);
	SDL_RWclose(stream);
	if (!rv)
		return false;
	regenerateClipRect();
	
	// set the editor default values
	team=0;
	terrainSize=1; // terrain size 1
	level=0;
	type=0; // water
	editMode=EM_TERRAIN; // terrain

	if (rv);
		draw();
	return rv;
}

bool MapEdit::save(const char *filename, const char *name)
{
	assert(filename);
	assert(name);

	SDL_RWops *stream=globalContainer->fileManager->open(filename,"wb");
	if (stream)
	{
		game.save(stream, true, name);
		SDL_RWclose(stream);
		return true;
	}
	else
		return false;
}


void MapEdit::updateUnits(int x, int y, int w, int h)
{
	for (int dy=y; dy<y+h; dy++)
		for (int dx=x; dx<x+w; dx++)
		{
			Uint16 gid=game.map.getGroundUnit(dx, dy);
			if (gid!=NOGUID)
			{
				int team=Unit::GIDtoTeam(gid);
				int id=Unit::GIDtoID(gid);
				assert(game.teams[team]);
				assert(game.teams[team]->myUnits[id]);
				game.teams[team]->myUnits[id]->selectPreferedMovement();
			}
		}
	for (int dy=y; dy<y+h; dy++)
		for (int dx=x; dx<x+w; dx++)
		{
			Uint16 gid=game.map.getAirUnit(dx, dy);
			if (gid!=NOGUID)
			{
				int team=Unit::GIDtoTeam(gid);
				int id=Unit::GIDtoID(gid);
				assert(game.teams[team]);
				assert(game.teams[team]->myUnits[id]);
				game.teams[team]->myUnits[id]->selectPreferedMovement();
			}
		}
}

void MapEdit::handleKeyPressed(SDLKey key, bool pressed)
{
	//printf("key %x is %d\n", key, pressed);
	switch (key)
	{
		case SDLK_ESCAPE:
			askConfirmationToQuit();
			break;
		case SDLK_UP:
			if (pressed)
		    	viewportSpeedY[1]=-1;
		    else
		    	viewportSpeedY[1]=0;
			break;
		case SDLK_KP8:
			if (pressed)
		    	viewportSpeedY[2]=-1;
		    else
		    	viewportSpeedY[2]=0;
			break;
		case SDLK_DOWN:
			if (pressed)
		    	viewportSpeedY[3]=1;
		    else
		    	viewportSpeedY[3]=0;
			break;
		case SDLK_KP2:
			if (pressed)
		    	viewportSpeedY[4]=1;
		    else
		    	viewportSpeedY[4]=0;
			break;
		case SDLK_LEFT:
			if (pressed)
		    	viewportSpeedX[1]=-1;
		    else
		    	viewportSpeedX[1]=0;
			break;
		case SDLK_KP4:
			if (pressed)
		    	viewportSpeedX[2]=-1;
		    else
		    	viewportSpeedX[2]=0;
			break;
		case SDLK_RIGHT:
			if (pressed)
		    	viewportSpeedX[3]=1;
		    else
		    	viewportSpeedX[3]=0;
			break;
		case SDLK_KP6:
			if (pressed)
		    	viewportSpeedX[4]=1;
		    else
		    	viewportSpeedX[4]=0;
			break;
		case SDLK_KP7:
			if (pressed)
			{
		    	viewportSpeedX[5]=-1;
		    	viewportSpeedY[5]=-1;
		    }
		    else
		    {
		    	viewportSpeedX[5]=0;
		    	viewportSpeedY[5]=0;
		    }
			break;
		case SDLK_KP9:
			if (pressed)
			{
		    	viewportSpeedX[6]=1;
		    	viewportSpeedY[6]=-1;
		    }
		    else
		    {
		    	viewportSpeedX[6]=0;
		    	viewportSpeedY[6]=0;
		    }
			break;
		case SDLK_KP1:
			if (pressed)
			{
		    	viewportSpeedX[7]=-1;
		    	viewportSpeedY[7]=1;
		    }
		    else
		    {
		    	viewportSpeedX[7]=0;
		    	viewportSpeedY[7]=0;
		    }
			break;
		case SDLK_KP3:
			if (pressed)
			{
		    	viewportSpeedX[8]=1;
		    	viewportSpeedY[8]=1;
		    }
		    else
		    {
		    	viewportSpeedX[8]=0;
		    	viewportSpeedY[8]=0;
		    }
			break;
		case SDLK_s:
		{
			if (pressed)
				loadSave(false);
		}
		break;
		case SDLK_l:
		{
			if (pressed)
				loadSave(true);
		}
		break;
		case SDLK_TAB:
		{
			if (pressed)
			{
				int numberOfTeam=game.session.numberOfTeam;
				if (numberOfTeam>0)
				{
					centeredTeam=centeredTeam%numberOfTeam;
					if (SDL_GetModState()&KMOD_SHIFT)
						centeredTeam=team;
					else
						team=centeredTeam;
						
					Team *t=game.teams[centeredTeam];
					if (t)
					{
						Building *b=t->myBuildings[0];
						if (b)
						{
							viewportX=b->getMidX()-(globalContainer->gfx->getW()>>6);
							viewportY=b->getMidY()-(globalContainer->gfx->getH()>>6);

							viewportX&=game.map.getMaskW();
							viewportY&=game.map.getMaskH();

							draw();
						}
					}
					centeredTeam++;
				}
			}
		}
		break;
		default:
		// unhandeld key
		break;
	}
}

void MapEdit::viewportFromMxMY(int mx, int my)
{
	// get data for minimap
	int mMax;
	int szX, szY;
	int decX, decY;
	Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);

	mx-=14+decX;
	my-=14+decY;
	viewportX=((mx*game.map.getW())/szX)-((globalContainer->gfx->getW()-128)>>6);
	viewportY=((my*game.map.getH())/szY)-((globalContainer->gfx->getH())>>6);
	
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();
}

int MapEdit::processEvent(const SDL_Event *event)
{
	int returnCode=0;
	if (event->type==SDL_MOUSEBUTTONUP)
	{
		int mx=event->button.x;
		int my=event->button.y;
		if (event->button.button==SDL_BUTTON_LEFT)
		{
			minimapPushed=false;
			pushedBrush=EM_NONE;
		}
		if (mx>=globalContainer->gfx->getW()-128)
		{
			if (my<128)
			{
				//printf("so what?.\n");
			}
			else
			{
				handleMenuClick(mx, my, event->button.button);
			}
		}
		if (wasClickInMap)
		{
			wasClickInMap=false;
			renderMiniMap();
		}
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		int mx=event->button.x;
		int my=event->button.y;
		if (event->button.button==SDL_BUTTON_LEFT)
		{
			if (mx>globalContainer->gfx->getW()-128)
			{
				if (my<128)
				{
					minimapPushed=true;
					viewportFromMxMY(mx-globalContainer->gfx->getW()+128, my);
					drawMap(screenClip.x, screenClip.y, screenClip.w-128, screenClip.h, true, false);
					drawMiniMap();
				}
			}
			else if (Utilities::ptInRect(mx, my, &mapClip))
			{
				handleMapClick(mx, my);
				pushedBrush=editMode;
				paintEditMode(mx, my, true, true);
				wasClickInMap=true;
			}
		}
		else if (event->button.button==SDL_BUTTON_RIGHT)
		{
			// We relase tools, like in game:
			editMode=EM_NONE;
			paintEditMode(mx, my, true, true);
			minimapPushed=false;
			drawMenu();
		}
	}
	else if ((event->type==SDL_ACTIVEEVENT) && (event->active.gain==0))
	{
		viewportSpeedX[0]=viewportSpeedY[0]=0;
	}
	else if (event->type==SDL_MOUSEMOTION)
	{
		int mx=event->motion.x;
		int my=event->motion.y;
		savedMx=mx;
		savedMy=my;
		
		if (minimapPushed)
		{
			// handle viewport reset
			viewportFromMxMY(mx-globalContainer->gfx->getW()+128, my);
			drawMap(screenClip.x, screenClip.y, screenClip.w-128, screenClip.h, true, false);
			drawMiniMap();
		}
		else
		{
			// handle nice scroll
			const int scrollZoneWidth=5;
			if (mx<scrollZoneWidth)
				viewportSpeedX[0]=-1;
			else if ((mx>globalContainer->gfx->getW()-scrollZoneWidth) )
				viewportSpeedX[0]=1;
			else
				viewportSpeedX[0]=0;

			if (my<scrollZoneWidth)
				viewportSpeedY[0]=-1;
			else if (my>globalContainer->gfx->getH()-scrollZoneWidth)
				viewportSpeedY[0]=1;
			else
				viewportSpeedY[0]=0;

			if (pushedBrush!=EM_NONE)
				handleMapClick(mx, my);

			paintEditMode(mx, my, true, true);
		}

		paintCoordinates(mx, my);

	}
	else if (event->type==SDL_KEYDOWN)
	{
		handleKeyPressed(event->key.keysym.sym, true);
	}
	else if (event->type==SDL_KEYUP)
	{
		handleKeyPressed(event->key.keysym.sym, false);
	}
	else if (event->type==SDL_VIDEORESIZE)
	{
		int newW=event->resize.w&0xFFFFFFE0;
		int newH=event->resize.h&0xFFFFFFE0;
		if (newW<256)
			newW=256;
		if (newH<288)
			newH=288;
		globalContainer->gfx->setRes(newW, newH, 32, globalContainer->settings.screenFlags);
		regenerateClipRect();
		draw();
	}
	else if (event->type==SDL_QUIT)
	{
		returnCode=-1;
		isRunning=false;
	}
	return returnCode;
}
/*
int MapEdit::run(int sizeX, int sizeY, TerrainType terrainType)
{
	game.map.setSize(sizeX, sizeY, terrainType);
	game.map.setGame(&game);
	globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags);

	regenerateClipRect();
	globalContainer->gfx->setClipRect();
	draw();

	isRunning=true;
	int returnCode=0;
	Uint32 startTick, endTick, deltaTick;
	while (isRunning)
	{
		//SDL_Event event;
		startTick=SDL_GetTicks();

		SDL_Event event, mouseMotionEvent, windowEvent;
		bool wasMouseMotion=false;
		bool wasWindowEvent=false;
	
		// we get all pending events but for mousemotion we only keep the last one
		while (SDL_PollEvent(&event))
		{
			//if (event.type==SDL_VIDEORESIZE)
			//{
			//	we don't want video resize
			//}
			if (event.type==SDL_MOUSEMOTION)
			{
				mouseMotionEvent=event;
				wasMouseMotion=true;
			}
			else if (event.type==SDL_ACTIVEEVENT)
			{
				windowEvent=event;
				wasWindowEvent=true;
			}
			else
			{
				returnCode=(processEvent(&event) == -1) ? -1 : returnCode;
			}
		}
		if (wasMouseMotion)
			returnCode=(processEvent(&event) == -1) ? -1 : returnCode;
		if (wasWindowEvent)
			returnCode=(processEvent(&event) == -1) ? -1 : returnCode;

		// redraw on scroll
		bool doRedraw=false;
		int i;
		viewportX+=game.map.getW();
		viewportY+=game.map.getH();
		{
			for (i=0; i<9; i++)
			{
				viewportX+=viewportSpeedX[i];
				viewportY+=viewportSpeedY[i];
				if ((viewportSpeedX[i]!=0) || (viewportSpeedY[i]!=0))
					doRedraw=true;
			}
		}
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();

		if (doRedraw)
		{
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
		}

		endTick=SDL_GetTicks();
		deltaTick=endTick-startTick;
		if (deltaTick<33)
			SDL_Delay(33-deltaTick);
	}

	globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags);
	return returnCode;
}
*/

/*void MapEdit::resize(int sizeX, int sizeY)
{
	game.map.setSize(sizeX, sizeY);
}*/

int MapEdit::run(int sizeX, int sizeY, TerrainType terrainType)
{
	game.map.setSize(sizeX, sizeY, terrainType);
	game.map.setGame(&game);
	return run();
}


int MapEdit::run(void)
{
	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags);

	regenerateClipRect();
	globalContainer->gfx->setClipRect();
	draw();

	isRunning=true;
	int returnCode=0;
	Uint32 startTick, endTick, deltaTick;
	while (isRunning)
	{
		//SDL_Event event;
		startTick=SDL_GetTicks();

		SDL_Event event, mouseMotionEvent, windowEvent;
		bool wasMouseMotion=false;
		bool wasWindowEvent=false;
	
		// we get all pending events but for mousemotion we only keep the last one
		while (SDL_PollEvent(&event))
		{
			//if (event.type==SDL_VIDEORESIZE)
			//{
			//	we don't want video resize
			//}
			if (event.type==SDL_MOUSEMOTION)
			{
				mouseMotionEvent=event;
				wasMouseMotion=true;
			}
			else if (event.type==SDL_ACTIVEEVENT)
			{
				windowEvent=event;
				wasWindowEvent=true;
			}
			else
			{
				returnCode=(processEvent(&event) == -1) ? -1 : returnCode;
			}
		}
			
		if (wasMouseMotion)
			returnCode=(processEvent(&mouseMotionEvent) == -1) ? -1 : returnCode;
		if (wasWindowEvent)
			returnCode=(processEvent(&windowEvent) == -1) ? -1 : returnCode;

		// redraw on scroll
		bool doRedraw=false;
		viewportX+=game.map.getW();
		viewportY+=game.map.getH();
		for (int i=0; i<9; i++)
		{
			viewportX+=viewportSpeedX[i];
			viewportY+=viewportSpeedY[i];
			if ((viewportSpeedX[i]!=0) || (viewportSpeedY[i]!=0))
			{
				doRedraw=true;
				if (pushedBrush!=EM_NONE)
					handleMapClick();
			}
		}
		viewportX&=game.map.getMaskW();
		viewportY&=game.map.getMaskH();

		if (doRedraw)
		{
			drawMap(screenClip.x, screenClip.y, screenClip.w-128, screenClip.h, true, true);
			drawMiniMap();
		}

		endTick=SDL_GetTicks();
		deltaTick=endTick-startTick;
		if (deltaTick<33)
			SDL_Delay(33-deltaTick);
	}

	//globalContainer->gfx->setRes(globalContainer->graphicWidth, globalContainer->graphicHeight , 32, globalContainer->graphicFlags);
	return returnCode;
}
