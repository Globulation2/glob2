/*
 * Globulation 2 Editor main
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GAG.h"
#include "Game.h"
#include "MapEdit.h"
#include "GlobalContainer.h"
#include "UnitType.h"
#include "Utilities.h"



MapEdit::MapEdit()
{
	// default value;
	viewportX=0;
	viewportY=0;

	// set the editor default values
	team=0;
	terrainSize=1; // terrain size 1
	level=0;
	type=Map::WATER; // water
	editMode=TERRAIN; // terrain

	// load menu
	menu=new IMGGraphicArchive("data/guiedit.data");
	font=new SDLBitmapFont("data/font.png");
}

MapEdit::~MapEdit()
{
	delete menu;
	delete font;
}

void MapEdit::drawMap(int sx, int sy, int sw, int sh, bool needUpdate)
{
	Utilities::rectClipRect(sx, sy, sw, sh, mapClip);
	
	globalContainer->gfx.setClipRect(sx, sy, sw, sh);

	game.drawMap(sx, sy, sw, sh, viewportX, viewportY, team);

	globalContainer->gfx.setClipRect(&screenClip);
	
	if (needUpdate)
		SDL_UpdateRect(globalContainer->gfx.screen, sx, sy, sw, sh);
}

void MapEdit::drawMiniMap(void)
{
	game.drawMiniMap(globalContainer->gfx.getW()-128, 0, 128, 128, viewportX, viewportY);
	SDL_UpdateRect(globalContainer->gfx.screen, globalContainer->gfx.getW()-128, 0, 128, 128);
}

void MapEdit::renderMiniMap(void)
{
	game.renderMiniMap(-1);
	drawMiniMap();
}

void MapEdit::drawMenu(void)
{
	int menuStartW=globalContainer->gfx.getW()-128;

	// draw buttons
	globalContainer->gfx.drawSprite(menu->getSprite(0), menuStartW+0, 129);
	globalContainer->gfx.drawSprite(menu->getSprite(0), menuStartW+0, 167);
	globalContainer->gfx.drawSprite(menu->getSprite(0), menuStartW+0, 269);
	globalContainer->gfx.drawSprite(menu->getSprite(0), menuStartW+0, 455);
	globalContainer->gfx.drawSprite(menu->getSprite(1), menuStartW+0, 135);
	globalContainer->gfx.drawSprite(menu->getSprite(2), menuStartW+32, 135);
	globalContainer->gfx.drawSprite(menu->getSprite(3), menuStartW+64, 135);
	globalContainer->gfx.drawSprite(menu->getSprite(4), menuStartW+96, 135);
	globalContainer->gfx.drawSprite(menu->getSprite(5), menuStartW+0, 173);
	globalContainer->gfx.drawSprite(menu->getSprite(6), menuStartW+32, 173);
	globalContainer->gfx.drawSprite(menu->getSprite(7), menuStartW+64, 173);
	globalContainer->gfx.drawSprite(menu->getSprite(8), menuStartW+96, 173);
	globalContainer->gfx.drawSprite(menu->getSprite(9), menuStartW+0, 205);
	globalContainer->gfx.drawSprite(menu->getSprite(10), menuStartW+32, 205);
	globalContainer->gfx.drawSprite(menu->getSprite(11), menuStartW+64, 205);
	globalContainer->gfx.drawSprite(menu->getSprite(12), menuStartW+96, 205);
	globalContainer->gfx.drawSprite(menu->getSprite(13), menuStartW+0, 237);
	globalContainer->gfx.drawSprite(menu->getSprite(14), menuStartW+32, 237);
	globalContainer->gfx.drawSprite(menu->getSprite(15), menuStartW+64, 237);
	globalContainer->gfx.drawSprite(menu->getSprite(16), menuStartW+96, 237);
	//globalContainer->gfx.drawSprite(menu->getSprite(17), menuStartW+0, 275);
	//globalContainer->gfx.drawSprite(menu->getSprite(18), menuStartW+32, 275);
	//globalContainer->gfx.drawSprite(menu->getSprite(19), menuStartW+64, 275);
	//globalContainer->gfx.drawSprite(menu->getSprite(20), menuStartW+96, 275);
	/*globalContainer->gfx.drawSprite(menu->getSprite(21), menuStartW+0, 307);
	globalContainer->gfx.drawSprite(menu->getSprite(22), menuStartW+32, 307);
	globalContainer->gfx.drawSprite(menu->getSprite(23), menuStartW+64, 307);
	globalContainer->gfx.drawSprite(menu->getSprite(24), menuStartW+96, 307);
	globalContainer->gfx.drawSprite(menu->getSprite(25), menuStartW+0, 339);
	globalContainer->gfx.drawSprite(menu->getSprite(26), menuStartW+32, 339);
	globalContainer->gfx.drawSprite(menu->getSprite(27), menuStartW+64, 339);
	globalContainer->gfx.drawSprite(menu->getSprite(28), menuStartW+96, 339);*/
	globalContainer->gfx.drawSprite(menu->getSprite(29), menuStartW+0, 371);
	globalContainer->gfx.drawSprite(menu->getSprite(30), menuStartW+0, 423);
	globalContainer->gfx.drawSprite(menu->getSprite(31), menuStartW+32, 423);
	globalContainer->gfx.drawSprite(menu->getSprite(32), menuStartW+64, 423);
	globalContainer->gfx.drawSprite(menu->getSprite(33), menuStartW+96, 423);
	
	// draw units
	globalContainer->gfx.drawFilledRect(menuStartW, 275, 128, 96, 0, 0, 0);

	PalSprite *unitSprite=(PalSprite *)globalContainer->units.getSprite(64);
	unitSprite->setPal(&(game.teams[team]->palette));
	globalContainer->gfx.drawSprite(unitSprite,menuStartW+0, 275);

	unitSprite=(PalSprite *)globalContainer->units.getSprite(0);
	unitSprite->setPal(&(game.teams[team]->palette));
	globalContainer->gfx.drawSprite(unitSprite,menuStartW+32, 275);

	unitSprite=(PalSprite *)globalContainer->units.getSprite(256);
	unitSprite->setPal(&(game.teams[team]->palette));
	globalContainer->gfx.drawSprite(unitSprite,menuStartW+64, 275);
	
	// draw buildings
	{
		for (int i=0; i<8; i++)
		{
			int typeNum;
			if (i!=0)
				typeNum=globalContainer->buildingsTypes.getTypeNum(i, ((level>2) ? 2 : level) , false);
			else
				typeNum=globalContainer->buildingsTypes.getTypeNum(i, 0, false);
			BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);
			int imgid=bt->startImage;
			int x=((i&0x3)<<5)+menuStartW;
			int y=((i>>2)<<5)+307;
			
			globalContainer->gfx.setClipRect( x+1, y+1, 30, 30);
			PalSprite *buildingSprite=(PalSprite *)globalContainer->buildings.getSprite(imgid);
			//int w=buildingSprite->getW();
			//int h=buildingSprite->getH();
			if (bt->hueImage)
				buildingSprite->setPal(&(game.teams[team]->palette));
			else
				buildingSprite->setPal(&(globalContainer->macPal));
			
			globalContainer->gfx.drawSprite(buildingSprite, x-20, y-20);
		}
	}
	globalContainer->gfx.setClipRect(&screenClip);	
	
	// draw selections
	drawSelRect(menuStartW+((terrainSize-1)*16), 237, 32, 32);
	drawSelRect(menuStartW+(level*32), 423, 32, 32);
	if (editMode==TERRAIN)
		drawSelRect(menuStartW+(type*32), 173, 32, 32);
	else if (editMode==RESSOURCE)
		drawSelRect(menuStartW+(type*32), 205, 32, 32);
	else if (editMode==UNIT)
		drawSelRect(menuStartW+(type*32), 275, 32, 32);
	else if (editMode==BUILDING)
		if (type<4)
			drawSelRect(menuStartW+(type*32), 307, 32, 32);
		else
			drawSelRect(menuStartW+((type-4)*32), 339, 32, 32);
	else if (editMode==DELETE)
		drawSelRect(menuStartW+64, 135, 32, 32);

	// draw teams
	if (game.session.numberOfTeam<=8)
	{
		{
			for (int i=0; i<game.session.numberOfTeam; i++)
			{
				int line=i/4;
				int dec=i%4;
				float r, g, b;
				float h=(float)(game.teams[i]->color);
				float s=1.0f;
				float v=1.0f;
				Palette::HSVtoRGB(&r, &g, &b, h, s, v);
				globalContainer->gfx.drawFilledRect(menuStartW+12+1+dec*26, 371+1+line*26, 24, 24, (Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255));
			}
		}
	}
	else
	{
		{
			for (int i=0; i<game.session.numberOfTeam; i++)
			{
				int line=i/8;
				int dec=i%8;
				float r, g, b;
				float h=(float)(game.teams[i]->color);
				float s=1.0f;
				float v=1.0f;
				Palette::HSVtoRGB(&r, &g, &b, h, s, v);
				globalContainer->gfx.drawFilledRect(menuStartW+12+1+dec*13, 371+1+line*13, 11, 11, (Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255));
			}
		}
	}

	float r, g, b;
	float h=(float)(game.teams[team]->color);
	float s=1.0f;
	float v=1.0f;
	Palette::HSVtoRGB(&r, &g, &b, h, s, v);
	
	// draw team selection
	if (game.session.numberOfTeam<=8)
	{
		int line, dec;
		{
			for (int i=0; i<game.session.numberOfTeam; i++)
			{
				line=i/4;
				dec=i%4;
				if (game.teams[team]->allies & game.teams[i]->me)
					globalContainer->gfx.drawFilledRect(menuStartW+20+dec*26, 379+line*26, 10, 10, (Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255));
			}
		}
		
		line=team/4;
		dec=team%4;
		globalContainer->gfx.drawRect(menuStartW+12+dec*26, 371+line*26, 26, 26, 255, 0, 0);
		globalContainer->gfx.drawRect(menuStartW+13+dec*26, 372+line*26, 24, 24, 0, 0, 0);

	}
	else
	{
		int line, dec;
		{
			for (int i=0; i<game.session.numberOfTeam; i++)
			{
				line=i/8;
				dec=i%8;
				if (game.teams[team]->allies & game.teams[i]->me)
					globalContainer->gfx.drawFilledRect(menuStartW+16+dec*13, 375+line*13, 5, 5, (Uint8)(r*255), (Uint8)(g*255), (Uint8)(b*255));
			}
		}
		
		line=team/8;
		dec=team%8;
		globalContainer->gfx.drawRect(menuStartW+12+dec*13, 371+line*13, 13, 13, 255, 0, 0);
		globalContainer->gfx.drawRect(menuStartW+13+dec*13, 372+line*13, 11, 11, 0, 0, 0);

	}

	SDL_UpdateRect(globalContainer->gfx.screen,menuStartW, 128, 128, globalContainer->gfx.getH()-128);
}

void MapEdit::draw(void)
{
	drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
	renderMiniMap();
	drawMenu();
}

void MapEdit::handleMapClick(int mx, int my)
{
	int x, y;
	int winX, winW, winY, winH;
	static int ax, ay, atype;
	bool needRedraw=false;

	if (editMode==TERRAIN)
	{
		game.map.displayToMapCaseUnaligned(mx, my, &x, &y, viewportX, viewportY);
		if ((ax!=x)||(ay!=y)||(atype!=type))
		{
			game.map.setUMatPos(x,y,(Map::TerrainType)type, terrainSize);
			needRedraw=true;

			winX=((mx+16)&0xFFFFFFE0)-((terrainSize>>1)<<5)-64;
			winW=(terrainSize+3)<<5;
			winY=((my+16)&0xFFFFFFE0)-((terrainSize>>1)<<5)-64;
			winH=(terrainSize+3)<<5;

			int dec;
			if ((type==Map::WATER) || (type==Map::SAND))
			{
				if (type==Map::WATER)
					dec=3;
				else
					dec=1;

				int delX, delY, delW, delH;

				SDL_Rect r;
				if (game.removeUnitAndBuilding(x, y, terrainSize+dec, &r, Game::DEL_BUILDING))
				{
					game.map.mapCaseToDisplayable(r.x, r.y, &delX, &delY, viewportX, viewportY);
					delW=r.w<<5;
					delH=r.h<<5;
				}
				Utilities::rectExtendRect(delX, delY, delW, delH,  &winX, &winY, &winW, &winH);
			}
			updateUnits(x-(terrainSize>>1)-2, y-(terrainSize>>1)-2, terrainSize+3, terrainSize+3);
		}
	}
	else if (editMode==RESSOURCE)
	{
		game.map.displayToMapCaseAligned(mx, my, &x, &y, viewportX, viewportY);
 		if ((ax!=x)||(ay!=y)||(atype!=type))
		{
			game.map.setResAtPos(x,y,type,terrainSize);
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
			}

			Utilities::rectExtendRect(delX, delY, delW, delH,  &winX, &winY, &winW, &winH);
		}
	}
	else if (editMode==UNIT)
	{
		game.map.displayToMapCaseAligned(mx, my, &x, &y, viewportX, viewportY);

		game.addUnit(x, y, team, type, level, rand()%256, 0, 0);
		game.regenerateDiscoveryMap();

		winX=mx&0xFFFFFFE0;
		winY=my&0xFFFFFFE0;
		winW=32;
		winH=32;
		needRedraw=true;
	}
	else if (editMode==BUILDING)
	{
		//game.map.displayToMapCaseUnaligned(mx, my, &x, &y, viewportX, viewportY);
		int typeNum=globalContainer->buildingsTypes.getTypeNum(type, level, false);
		BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);

		int tempX, tempY;
		game.map.cursorToBuildingPos(mx, my, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);

		if (game.checkRoomForBuilding(tempX, tempY, typeNum, &x, &y, -1))
		{
			game.addBuilding(x, y, team, typeNum );
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
	else if (editMode==DELETE)
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
		drawMap(winX, winY, winW, winH);
		renderMiniMap();
	}

	atype=type;
	ax=x;
	ay=y;
}

void MapEdit::regenerateClipRect(void)
{
	screenClip.x=0;
	screenClip.y=0;
	screenClip.w=globalContainer->gfx.getW();
	screenClip.h=globalContainer->gfx.getH();
	mapClip.x=0;
	mapClip.y=0;
	mapClip.w=screenClip.w-128;
	mapClip.h=screenClip.h;
	viewportW=mapClip.w>>5;
	viewportH=mapClip.h>>5;
}

void MapEdit::drawSelRect(int x, int y, int w, int h)
{
	globalContainer->gfx.drawRect(x, y, w, h, 255, 0, 0);
	globalContainer->gfx.drawRect(x+1, y+1, w-2, h-2, 255, 0, 0);
}

void MapEdit::handleMenuClick(int mx, int my, int button)
{
	mx-=globalContainer->gfx.getW()-128;
	if ((my>135) && (my<167))
	{
		if (mx<32)
			load();
		else if (mx<64)
			save();
		else if (mx<96)
			editMode=DELETE;
		else
			isRunning=false;
	}
	else if ((my>173) && (my<205))
	{
		if (mx>=96)
			return;
		editMode=TERRAIN;
		type=mx/32;
	}
	else if ((my>205) && (my<237))
	{
		editMode=RESSOURCE;
		type=mx/32;
	}
	else if ((my>237) && (my<269))
	{
		int table[4]={1,3,5,7};
		terrainSize=table[mx/32];
	}
	else if ((my>275) && (my<307))
	{
		editMode=UNIT;
		type=mx/32;
		if (type>2)// we have only 3 units.
			type=2;
	}
	else if ((my>307) && (my<339))
	{
		editMode=BUILDING;
		type=mx/32;
		if (type==0)
			level=0;
		if (level>2)
			level=2;
	}
	else if ((my>339) && (my<371))
	{
		editMode=BUILDING;
		type=4+(mx/32);
		if (level>2)
			level=2;
	}
	else if ((my>371) && (my<423))
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
			game.addTeam();
			draw();
		}
		else
		{
			// select a team
			if (game.session.numberOfTeam<=8)
			{
				int px=(mx-12)/26;
				int py=(my-371)/26;
				int newteam=(py*4)+px;
				if (newteam<game.session.numberOfTeam)
				{
					if (button==SDL_BUTTON_LEFT)
					{
						team=newteam;
						drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
					}
					else if ((button==SDL_BUTTON_RIGHT) && (team!=newteam))
					{
						game.teams[team]->allies^=(1<<newteam);
						game.teams[team]->enemies^=(1<<newteam);
						game.teams[team]->sharedVision^=(1<<newteam);
						
					}
				}
			}
			else
			{
				int px=(mx-12)/13;
				int py=(my-371)/13;
				int newteam=(py*8)+px;
				if (newteam<game.session.numberOfTeam)
				{
					if (button==SDL_BUTTON_LEFT)
					{
						team=newteam;
						drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
					}
					else if ((button==SDL_BUTTON_RIGHT) && (team!=newteam))
					{
						game.teams[team]->allies^=(1<<newteam);
						game.teams[team]->enemies^=(1<<newteam);
						game.teams[team]->sharedVision^=(1<<newteam);
					}
				}
			}
			
			// set Team alliance
		}
	}
	else if ((my>423) && (my<455))
	{
		level=mx/32;
		if (editMode==BUILDING)
		{
			if (type==0)
				level=0;
			if (level>2)
				level=2;
		}
	}

	drawMenu();
}

void MapEdit::load(void)
{
	SDL_RWops *stream=globalContainer->fileManager.open("default.map","rb");
	if (game.load(stream)==false)
		fprintf(stderr, "MED : Warning, Error during map load\n");
	SDL_RWclose(stream);

	// set the editor default values
	team=0;
	terrainSize=1; // terrain size 1
	level=0;
	type=0; // water
	editMode=TERRAIN; // terrain

	draw();
}

void MapEdit::save(void)
{
	SDL_RWops *stream=globalContainer->fileManager.open("default.map","wb");
	game.save(stream);
	SDL_RWclose(stream);
}


void MapEdit::updateUnits(int x, int y, int w, int h)
{
	int uid;
	
	for (int dy=y; dy<y+h; dy++)
		for (int dx=x; dx<x+w; dx++)
			if ((uid=game.map.getUnit(dx, dy))>=0)
			{
				int team=uid/1024;
				int id=uid%1024;
				game.teams[team]->myUnits[id]->selectPreferedMovement();
			}
}

void MapEdit::handleKeyPressed(SDLKey key)
{
	switch (key)
	{
		case SDLK_ESCAPE:
			isRunning=false;
			break;
		case SDLK_UP:
		case SDLK_KP8:
			viewportY=(viewportY+game.map.getH()-1)&game.map.getMaskH();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_DOWN:
		case SDLK_KP2:
			viewportY=(viewportY+1)&game.map.getMaskH();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_LEFT:
		case SDLK_KP4:
			viewportX=(viewportX+game.map.getW()-1)&game.map.getMaskW();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_RIGHT:
		case SDLK_KP6:
			viewportX=(viewportX+1)&game.map.getMaskW();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_KP7:
			viewportY=(viewportY+game.map.getH()-1)&game.map.getMaskH();
			viewportX=(viewportX+game.map.getW()-1)&game.map.getMaskW();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_KP9:
			viewportY=(viewportY+game.map.getH()-1)&game.map.getMaskH();
			viewportX=(viewportX+1)&game.map.getMaskW();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);

			drawMiniMap();
			break;
		case SDLK_KP1:
			viewportY=(viewportY+1)&game.map.getMaskH();
			viewportX=(viewportX+game.map.getW()-1)&game.map.getMaskW();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_KP3:
			viewportY=(viewportY+1)&game.map.getMaskH();
			viewportX=(viewportX+1)&game.map.getMaskW();
			drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
			drawMiniMap();
			break;
		case SDLK_s:
		{
			save();
		}
		break;
		case SDLK_l:
		{
			load();
		}
		break;
		default:
		// unhandeld key
		break;
	}
}

int MapEdit::run(void)
{
	game.map.setSize(7,7, &game);
	globalContainer->gfx.setRes(640, 480, 32, SDL_ANYFORMAT|SDL_SWSURFACE|SDL_RESIZABLE);
	regenerateClipRect();
	globalContainer->gfx.setClipRect(NULL);
	draw();

	isRunning=true;
	int returnCode=0;
	while (isRunning)
	{
		SDL_Event event;

		SDL_WaitEvent(&event);

		if (event.type!=SDL_VIDEORESIZE)
			while (SDL_PollEvent(&event))
			{
				if (event.type==SDL_VIDEORESIZE)
					break;
			}

		if (event.type==SDL_MOUSEBUTTONDOWN)
		{
			int mx=event.button.x;
			int my=event.button.y;
			if (mx>=globalContainer->gfx.screen->w-128)
			{
				if (my<128)
				{
					mx=mx-globalContainer->gfx.screen->w+128;
					viewportX=((mx*game.map.getW())>>7)-((globalContainer->gfx.screen->w-128)>>6);
					viewportY=((my*game.map.getH())>>7)-((globalContainer->gfx.screen->h)>>6);
					if (viewportX<0)
						viewportX+=game.map.getW();
					if (viewportY<0)
						viewportY+=game.map.getH();
					drawMap(screenClip.x,screenClip.y,screenClip.w-128,screenClip.h);
					drawMiniMap();
				}
				else
				{
					handleMenuClick(mx, my, event.button.button);
				}
			}
			else
			{
				handleMapClick(mx, my);
			}
		}
		else if (event.type==SDL_MOUSEMOTION)
		{
			int mx=event.motion.x;
			int my=event.motion.y;
			
			int x=globalContainer->gfx.getW()-128;
			int y=460;
			
			// NOTE : this is just to test fonts
			globalContainer->gfx.drawFilledRect(x, y, 128, 20, 0, 255, 0);
			globalContainer->gfx.drawString(x, y, font, "(%d, %d)", mx, my);
			SDL_UpdateRect(globalContainer->gfx.screen, x, y, 128, 20);
			
			static int oldBrush=NONE;
			static int orX=0, orY=0, orW=0, orH=0;
			const int maxNbRefreshZones=2;
			SDL_Rect refreshZones[maxNbRefreshZones];
			int nbRefreshZones=0;

			if ( (oldBrush==BUILDING) || (oldBrush==UNIT) || (oldBrush==TERRAIN) || (oldBrush==RESSOURCE) || (editMode==DELETE) )
			{
				drawMap(orX, orY, orW, orH, false);

				refreshZones[nbRefreshZones].x=orX;
				refreshZones[nbRefreshZones].y=orY;
				refreshZones[nbRefreshZones].w=orW;
				refreshZones[nbRefreshZones].h=orH;
				nbRefreshZones++;

				oldBrush=NONE;
			}

			if (Utilities::ptInRect(mx, my, &mapClip))
			{
				if (event.motion.state&SDL_BUTTON(1))
				{
					handleMapClick(mx, my);
				}
				if ( (editMode==TERRAIN) || (editMode==RESSOURCE) || (editMode==DELETE) )
				{
					//terrainSize
					int x, y, w, h;
					if (editMode==TERRAIN)
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

					globalContainer->gfx.drawRect(x, y, w, h, 255, 255, 255, 128);

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
				else if (editMode==UNIT)
				{

					int cx=(mx>>5)+viewportX;
					int cy=(my>>5)+viewportY;

					int px=mx&0xFFFFFFE0;
					int py=my&0xFFFFFFE0;
					int pw=32;
					int ph=32;

					bool isRoom=game.map.isFreeForUnit(cx, cy, type==UnitType::EXPLORER);

					int imgid;
					if (type==UnitType::WORKER)
						imgid=64;
					else if (type==UnitType::EXPLORER)
						imgid=0;
					else if (type==UnitType::WARRIOR)
						imgid=256;

					PalSprite *unitSprite=(PalSprite *)globalContainer->units.getSprite(imgid);
					unitSprite->setPal(&(game.teams[team]->palette));

					globalContainer->gfx.setClipRect(&mapClip);

					globalContainer->gfx.drawSprite(unitSprite, px, py);

					Utilities::rectClipRect(px, py, pw, ph, mapClip);
					if (isRoom)
						globalContainer->gfx.drawRect(px, py, pw, ph, 255, 255, 255, 128);
					else
						globalContainer->gfx.drawRect(px, py, pw, ph, 255, 0, 0, 128);

					globalContainer->gfx.setClipRect(&screenClip);

					refreshZones[nbRefreshZones].x=px;
					refreshZones[nbRefreshZones].y=py;
					refreshZones[nbRefreshZones].w=pw;
					refreshZones[nbRefreshZones].h=ph;
					nbRefreshZones++;

					orX=px;
					orY=py;
					orW=pw;
					orH=ph;
					oldBrush=UNIT;
				}
				else if (editMode==BUILDING)
				{
					int mapX, mapY;
					int batX, batY, batW, batH;

					// we get the type of building
					int typeNum=globalContainer->buildingsTypes.getTypeNum(type, level, false);
					BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);
					
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
					Sprite *sprite=globalContainer->buildings.getSprite(bt->startImage);
					if (bt->hueImage)
						((PalSprite *)sprite)->setPal(&(game.teams[team]->palette));
					
					batX=(mapX-viewportX)<<5;
					batY=(mapY-viewportY)<<5;
					batW=(bt->width)<<5;
					batH=(bt->height)<<5;

					globalContainer->gfx.setClipRect(&mapClip);
					globalContainer->gfx.drawSprite(sprite, batX, batY);

					Utilities::rectClipRect(batX, batY, batW, batH, mapClip);

					if (isRoom)
						globalContainer->gfx.drawRect(batX, batY, batW, batH, 255, 255, 255, 128);
					else
						globalContainer->gfx.drawRect(batX, batY, batW, batH, 255, 0, 0, 128);
					
					
					if (isRoom)
					{
						BuildingType *nnbt=bt;
						int max=0;
						while(nnbt->nextLevelTypeNum!=-1)
						{
							nnbt=globalContainer->buildingsTypes.getBuildingType(nnbt->nextLevelTypeNum);
							if (max++>200)
							{
								printf("MapEdit: Error: nextLevelTypeNum architecture is broken.\n");
								assert(false);
								break;
							}
						}
						int typeNum=nnbt->typeNum;
						
						tempX+=((-bt->decLeft+nnbt->decLeft)<<5);
						tempY+=((-bt->decTop +nnbt->decTop )<<5);
						
						isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, -1);
						
						batX=(mapX-viewportX)<<5;
						batY=(mapY-viewportY)<<5;
						batW=(nnbt->width)<<5;
						batH=(nnbt->height)<<5;
					
						if (isRoom)
							globalContainer->gfx.drawRect(batX, batY, batW, batH, 255, 255, 255, 128);
						else
							globalContainer->gfx.drawRect(batX, batY, batW, batH, 255, 0, 0, 128);

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
					
					globalContainer->gfx.setClipRect(&screenClip);
					oldBrush=BUILDING;
				}
			}

			assert(nbRefreshZones<=maxNbRefreshZones);
			if (nbRefreshZones>0)
			{
				SDL_UpdateRects(globalContainer->gfx.screen, nbRefreshZones, refreshZones);
			}


		}
		else if (event.type==SDL_KEYDOWN)
		{
			handleKeyPressed(event.key.keysym.sym);
		}
		else if (event.type==SDL_VIDEORESIZE)
		{
			int newW=event.resize.w&0xFFFFFFE0;
			int newH=event.resize.h&0xFFFFFFE0;
			if (newW<256)
				newW=256;
			if (newH<288)
				newH=288;
			globalContainer->gfx.setRes(newW, newH, 32, SDL_ANYFORMAT|SDL_SWSURFACE|SDL_RESIZABLE);
			regenerateClipRect();
			draw();
		}
		else if (event.type==SDL_QUIT)
		{
			returnCode=-1;
			isRunning=false;
		}
	}
	
	globalContainer->gfx.setRes(640, 480, 32, SDL_ANYFORMAT|SDL_SWSURFACE);
	return returnCode;
}
