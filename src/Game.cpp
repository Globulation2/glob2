/*
 * Globulation 2 game support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "Game.h"
#include "BuildingType.h"
#include <assert.h>
#include <string.h>
#include "GlobalContainer.h"
#include "Utilities.h"
#include <set>



BuildingsTypes Game::buildingsTypes("data/buildings.txt");

Game::Game()
{
	init();
}

Game::Game(const SessionInfo *initial)
{
	loadBase(initial);
}

void Game::loadBase(const SessionInfo *initial)
{
	init();
	SDL_RWops *stream=globalContainer->fileManager.open(initial->map.mapName,"rb");
	load(stream);
	SDL_RWclose(stream);
	setBase(initial);
}

Game::~Game()
{
	// delete existing teams and players
	for (int i=0; i<session.numberOfTeam; i++)
	{
		delete teams[i];
	}
	for (int i2=0; i2<session.numberOfPlayer; i2++)
	{
		delete players[i2];
	}
	
	SDL_FreeSurface(minimap);
}

void Game::init()
{
	// init minimap
	minimap=SDL_CreateRGBSurface(SDL_SWSURFACE,128,128,globalContainer->gfx.screen->format->BitsPerPixel,
									globalContainer->gfx.screen->format->Rmask,
									globalContainer->gfx.screen->format->Gmask,
									globalContainer->gfx.screen->format->Bmask,
									globalContainer->gfx.screen->format->Amask);
	session.numberOfTeam=0;
	session.numberOfPlayer=0;
	for (int i=0; i<32; i++)
	{
		teams[i]=NULL;
		players[i]=NULL;
	}
	addTeam();
	setSyncRandSeed();
	
	mouseX=0;
	mouseY=0;
	mouseUnit=NULL;
	selectedUnit=NULL;
	selectedBuilding=NULL;
	
	stepCounter=0;
}

void Game::setBase(const SessionInfo *initial)
{
	assert (initial->numberOfTeam==session.numberOfTeam);
	// TODO, we should be able to play with less team than planed on the map
	// for instance, play at 2 on a 4 player map, and we will have to check the following code !!!

	// the GUI asserts that we have not more team that planed on the map

	// set the base team, for now the number is corect but we should check that further
	for (int i=0; i<session.numberOfTeam; i++)
	{
		teams[i]->setBaseTeam(&(initial->team[i]));
	}

	// set the base players
	for (int i2=0; i2<session.numberOfPlayer; i2++)
	{
		delete players[i2];
	}
	session.numberOfPlayer=initial->numberOfPlayer;
	for (int i3=0; i3<initial->numberOfPlayer; i3++)
	{
		players[i3]=new Player();
		players[i3]->setBasePlayer(&(initial->players[i3]), teams);
	}

	// set the base map
	map.setBaseMap(&(initial->map));

	session.gameTPF=initial->gameTPF;
	session.gameLatency=initial->gameLatency;
	
	anyPlayerWaited=false;
}

void Game::executeOrder(Order *order, int localPlayer)
{
	anyPlayerWaited=false;
	switch (order->getOrderType())
	{
		case ORDER_CREATE:
		{
			// TODO : is it really safe to check fog of war localy to know if we can execute this order ?
			// if not really safe, we have to put -1 instead of team.
			if (checkRoomForBuilding( ((OrderCreate *)order)->posX, ((OrderCreate *)order)->posY, ((OrderCreate *)order)->typeNumber, ((OrderCreate *)order)->team))
			{
				Building *b=addBuilding( ((OrderCreate *)order)->posX, ((OrderCreate *)order)->posY, ((OrderCreate *)order)->team, ((OrderCreate *)order)->typeNumber );
				if (b)
				{
					if (b->type->unitProductionTime)
						b->owner->swarms.push_back(b);
					if (b->type->shootingRange)
						b->owner->turrets.push_back(b);
					b->update();
				}
			}
		}
		break;
		case ORDER_MODIFY_BUILDING:
		{
			for (int i=0; i<((OrderModifyBuildings *)order)->getNumberOfBuilding(); i++)
			{
				Sint32 UID=((OrderModifyBuildings *)order)->UID[i];
				int team=Building::UIDtoTeam(UID);
				int id=Building::UIDtoID(UID);
				Building *b=teams[team]->myBuildings[id];
				if ((b) && (b->buildingState==Building::ALIVE))
				{
					b->maxUnitWorking=((OrderModifyBuildings *)order)->numberRequested[i];
					b->maxUnitWorkingPreferred=b->maxUnitWorking;
					if (order->sender!=localPlayer)
						b->maxUnitWorkingLocal=b->maxUnitWorking;
					b->update();
				}
			}
		}
		break;
		case ORDER_MODIFY_FLAG:
		{
			for (int i=0; i<((OrderModifyFlags *)order)->getNumberOfBuilding(); i++)
			{
				Sint32 UID=((OrderModifyFlags *)order)->UID[i];
				int team=Building::UIDtoTeam(UID);
				int id=Building::UIDtoID(UID);
				Building *b=teams[team]->myBuildings[id];
				if ((b) && (b->buildingState==Building::ALIVE) && (b->type->defaultUnitStayRange))
				{
					b->unitStayRange=((OrderModifyFlags *)order)->range[i];
					if (order->sender!=localPlayer)
						b->unitStayRangeLocal=b->unitStayRangeLocal;
					b->update();
				}
			}
		}
		break;
		case ORDER_MODIFY_SWARM:
		{
			for (int i=0; i<((OrderModifySwarms *)order)->getNumberOfSwarm(); i++)
			{
				Sint32 UID=((OrderModifySwarms *)order)->UID[i];
				int team=Building::UIDtoTeam(UID);
				int id=Building::UIDtoID(UID);
				Building *b=teams[team]->myBuildings[id];
				if ((b) && (b->buildingState==Building::ALIVE) && (b->type->unitProductionTime))
				{
					for (int j=0; j<UnitType::NB_UNIT_TYPE; j++)
					{
						b->ratio[j]=((OrderModifySwarms *)order)->ratio[i*(UnitType::NB_UNIT_TYPE)+j];
						/* commented out by angel
						if (order->sender!=localPlayer)
							b->ratioLocal=b->ratioLocal; // ??
						*/
					}
					b->update();
				}
			}
		}
		break;
		case ORDER_DELETE:
		{
			Sint32 UID=((OrderDelete *)order)->UID;
			int team=Building::UIDtoTeam(UID);
			int id=Building::UIDtoID(UID);
			Building *b=teams[team]->myBuildings[id];
			if ((b) && (b->buildingState==Building::ALIVE))
			{
				b->removeSubscribers();
				b->buildingState=Building::WAITING_FOR_DESTRUCTION;
				b->maxUnitWorking=0;
				b->maxUnitInside=0;
				b->update();
			}
		}
		break;
		case ORDER_UPGRADE:
		{
			Sint32 UID=((OrderDelete *)order)->UID;
			int team=Building::UIDtoTeam(UID);
			int id=Building::UIDtoID(UID);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			
			if ((b) && (b->buildingState==Building::ALIVE) && (!b->type->isBuildingSite) && (b->type->nextLevelTypeNum!=-1) && (b->isHardSpace()))
			{
				if (b->type->unitProductionTime)
					t->swarms.remove(b);
				if (b->type->shootingRange)
					t->turrets.remove(b);
				
				b->removeSubscribers();
				b->buildingState=Building::WAITING_FOR_UPGRADE;
				b->maxUnitWorkingLocal=0;
				b->maxUnitWorking=0;
				b->maxUnitInside=0;
				b->update();
				//printf("order upgrade %d, w=%d\n", (int)b, b->type->width);
			}
		}
		break;
		case ORDER_CANCEL_UPGRADE :
		{
			Sint32 UID=((OrderCancelUpgrade *)order)->UID;
			int team=Building::UIDtoTeam(UID);
			int id=Building::UIDtoID(UID);
			Team *t=teams[team];
			Building *b=t->myBuildings[id];
			if (b)
				b->cancelUpgrade();
		}
		break;
		case ORDER_WAITING_FOR_PLAYER:
		{
			anyPlayerWaited=true;
			maskAwayPlayer=((WaitingForPlayerOrder *)order)->maskAwayPlayer;
		}
		break;
		case ORDER_PLAYER_QUIT_GAME:
		{
			//PlayerQuitsGameOrder *pqgo=(PlayerQuitsGameOrder *)order;
			//netGame have to handle this
			// players[pqgo->player]->type=Player::P_LOST_B;
		}
		break;
	}
}

bool Game::load(SDL_RWops *stream)
{
	// delete existing teams
	for (int i=0; i<session.numberOfTeam; i++)
	{
		delete teams[i];
	}
	for (int i2=0; i2<session.numberOfPlayer; i2++)
	{
		delete players[i2];
	}

	SessionInfo tempSessionInfo;
	tempSessionInfo.load(stream);
	
	session.numberOfPlayer=tempSessionInfo.numberOfPlayer;
	session.numberOfTeam=tempSessionInfo.numberOfTeam;
	
	session.gameTPF=tempSessionInfo.gameTPF;
	session.gameLatency=tempSessionInfo.gameLatency;

	memcpy(map.mapName, tempSessionInfo.map.mapName, 32);
	// other informations are dropped
	
	//session=*((SessionGame *)(&tempSessionInfo));
	
	char signature[4];

	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	
	setSyncRandSeedA(SDL_ReadBE32(stream));
	setSyncRandSeedB(SDL_ReadBE32(stream));
	setSyncRandSeedC(SDL_ReadBE32(stream));

	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;
	
	// recreate new teams and players
	for (int i3=0; i3<session.numberOfTeam; i3++)
	{
		teams[i3]=new Team(stream, this);
	}
	for (int i4=0; i4<session.numberOfPlayer; i4++)
	{
		players[i4]=new Player(stream, teams);
	}
	stepCounter=SDL_ReadBE32(stream);
	
	// we have to load team before map
	map.load(stream, this);

	SDL_RWread(stream, signature, 4, 1);
	if (memcmp(signature,"GLO2",4)!=0)
		return false;

	return true;
}

void Game::save(SDL_RWops *stream)
{
	if (stream==NULL)
	{
		printf("stream is NULL!\n");
		return;
	}
	// first we save a session info
	SessionInfo tempSessionInfo;
	/*
	*((SessionGame *)(&tempSessionInfo))=session;
	tempSessionInfo.map= *((BaseMap *)(&map));
	for (int i=0; i<session.numberOfTeam; i++)
		tempSessionInfo.team[i]= *((BaseTeam *)teams[i]);
	for (int i=0; i<session.numberOfPlayer; i++)
		tempSessionInfo.players[i]= *((BasePlayer *)players[i]);
	*/
	tempSessionInfo.numberOfPlayer=session.numberOfPlayer;
	tempSessionInfo.numberOfTeam=session.numberOfTeam;
	tempSessionInfo.gameTPF=session.gameTPF;
	tempSessionInfo.gameLatency=session.gameLatency;
	
	tempSessionInfo.map=map;
	
	for (int i=0; i<session.numberOfTeam; i++)
	{
		tempSessionInfo.team[i]=*teams[i];
	}
	for (int i2=0; i2<session.numberOfPlayer; i2++)
	{
		tempSessionInfo.players[i2]=*players[i2];
	}
	
	tempSessionInfo.save(stream);
	
	SDL_RWwrite(stream, "GLO2", 4, 1);

	SDL_WriteBE32(stream, getSyncRandSeedA());
	SDL_WriteBE32(stream, getSyncRandSeedB());
	SDL_WriteBE32(stream, getSyncRandSeedC());
	
	SDL_RWwrite(stream, "GLO2", 4, 1);

	for (int i3=0; i3<session.numberOfTeam; i3++)
	{
		teams[i3]->save(stream);
	}
	for (int i4=0; i4<session.numberOfPlayer; i4++)
	{
		players[i4]->save(stream);
	}
	
	SDL_WriteBE32(stream, stepCounter);
	
	map.save(stream);

	SDL_RWwrite(stream, "GLO2", 4, 1);
}

void Game::step(Sint32 localTeam)
{
	if (anyPlayerWaited)
	{
		//printf("waiting for player (%x,%d)\n", maskAwayPlayer, maskAwayPlayer);
	}
	else
	{
		for (int i=0; i<session.numberOfTeam; i++)
		{
			teams[i]->step();
		}
		map.step();
		// NOTE : checkWinCondition();
		syncRand();

		if ((stepCounter&31)==0)
		{
			map.switchFogOfWar();
			for (int t=0; t<session.numberOfTeam; t++)
				for (int i=0; i<512; i++)
				{
					Building *b=teams[t]->myBuildings[i];
					if ((b)&&(!b->type->isBuildingSite || (b->type->level>0))&&(!b->type->isVirtual))
					{
						int sr=b->type->shootingRange;
						if(sr)
							map.setMapDiscovered(b->posX-sr, b->posY-sr, b->type->width+sr*2, b->type->height+sr*2, t);
						else
							map.setMapDiscovered(b->posX-1, b->posY-1, b->type->width+2, b->type->height+2, t);
					}
				}
		}
		if ((stepCounter&31)==0)
		{
			// TODO : allow visual alliances.
			renderMiniMap(localTeam, true);
		}
		stepCounter++;
	}
}

void Game::removeTeam(void)
{
	if (session.numberOfTeam>0)
	{
		// TODO : remove stuff left on the map in a cleany way
		Team *team=teams[--session.numberOfTeam];

		for (int i=0; i<1024; i++)
		{
			if (team->myUnits[i])
				map.setUnit(team->myUnits[i]->posX, team->myUnits[i]->posY, NOUID);
		}

		for (int i2=0; i2<512; i2++)
		{
			if (team->myBuildings[i2])
				map.setBuilding(team->myBuildings[i2]->posX, team->myBuildings[i2]->posY, team->myBuildings[i2]->type->width, team->myBuildings[i2]->type->height, NOUID);
		}
		
		for (int i3=0; i3<256; i3++)
		{
			//if (team->myBullets[i])
			// TODO : handle bullets destruction
		}

		delete team;

		assert (session.numberOfTeam!=0);
		int color=0;
		int colorInc=360/session.numberOfTeam;
		for (int i4=0; i4<session.numberOfTeam; i4++)
		{
			teams[i4]->setCorrectColor(color);
			color+=colorInc;
		}
	}
}

void Game::regenerateDiscoveryMap(void)
{
	memset(map.mapDiscovered, 0, map.w*map.h*sizeof(Uint32));
	for (int t=0; t<session.numberOfTeam; t++)
	{
		int i;
		for (i=0; i<1024; i++)
		{
			Unit *u=teams[t]->myUnits[i];
			if (u)
			{
				map.setMapDiscovered(u->posX-1, u->posY-1, 3, 3, t);
			}
		}
		for (i=0; i<512; i++)
		{
			Building *b=teams[t]->myBuildings[i];
			if (b)
			{
				map.setMapDiscovered(b->posX-1, b->posY-1, b->type->width+2, b->type->height+2, t);
			}
		}
	}
}

Unit *Game::addUnit(int x, int y, int team, int type, int level, int delta, int dx, int dy)
{
	assert(team<session.numberOfTeam);
	
	UnitType *ut=teams[team]->race.getUnitType((UnitType::TypeNum)type, level);
	
#	ifdef WIN32
#		pragma warning (disable : 4800)
#	endif
	if (!map.isFreeForUnit(x, y, ut->performance[FLY]))
#	ifdef WIN32
#		pragma warning (default : 4800)
#	endif
		return NULL;

	int id=-1;
	for (int i=0; i<1024; i++)//we search for a free place for a unit.
		if (teams[team]->myUnits[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==-1)
		return NULL;

	//ok, now we can safely deposite an unit.
	int uid=Unit::UIDfrom(id,team);

	map.setUnit(x, y, uid);

	teams[team]->myUnits[id]= new Unit(x, y, uid, (UnitType::TypeNum)type, teams[team], level);
	teams[team]->myUnits[id]->dx=dx;
	teams[team]->myUnits[id]->dy=dy;
	teams[team]->myUnits[id]->directionFromDxDy();
	teams[team]->myUnits[id]->delta=delta;
	teams[team]->myUnits[id]->selectPreferedMovement();
	return teams[team]->myUnits[id];
}

void Game::addTeam(void)
{
	if (session.numberOfTeam<32)
	{
		teams[session.numberOfTeam]=new Team(this);
		teams[session.numberOfTeam]->teamNumber=session.numberOfTeam;
		teams[session.numberOfTeam]->race.create(Race::USE_DEFAULT);
		teams[session.numberOfTeam]->setCorrectMasks();

		session.numberOfTeam++;
		int color=0;
		int colorInc=360/session.numberOfTeam;
		for (int i=0; i<session.numberOfTeam; i++)
		{
			teams[i]->setCorrectColor(color);
			color+=colorInc;
		}
	}
}

Building *Game::addBuilding(int x, int y, int team, int typeNum)
{
	assert(team<session.numberOfTeam);
	
	int id=-1;
	for (int i=0; i<512; i++)//we search for a free place for a building.
		if (teams[team]->myBuildings[i]==NULL)
		{
			id=i;
			break;
		}
	if (id==-1)
		return NULL;

	//ok, now we can safely deposite an building.
	int uid = Building::UIDfrom(id, team);

	int w=buildingsTypes.buildingsTypes[typeNum]->width;
	int h=buildingsTypes.buildingsTypes[typeNum]->height;

	map.setBuilding(x, y, w, h, uid);

	Building *b=new Building(x, y, uid, typeNum, teams[team], &buildingsTypes);
	teams[team]->myBuildings[id]=b;
	return b;
}

bool Game::removeUnitAndBuilding(int x, int y, SDL_Rect* r, int flags)
{
	Sint16 UID=map.getUnit(x, y);
	if ((UID>=0) && (flags&DEL_UNIT))
	{
		int id=Unit::UIDtoID(UID);
		int team=Unit::UIDtoTeam(UID);
		map.setUnit(x, y, NOUID);
		r->x=x;
		r->y=y;
		r->w=1;
		r->h=1;
		delete (teams[team]->myUnits[id]);
		(teams[team]->myUnits[id])=NULL;
	}
	else if ((UID>-16385) && (UID<0) && (flags&DEL_BUILDING))
	{
		int id=Building::UIDtoID(UID);
		int team=Building::UIDtoTeam(UID);
		Building *b=teams[team]->myBuildings[id];
		r->x=b->posX;
		r->y=b->posY;
		r->w=b->type->width;
		r->h=b->type->height;
		map.setBuilding(r->x, r->y, r->w, r->h, NOUID);
		delete b;
		teams[team]->myBuildings[id]=NULL;
	}
	else
	{
		return false;
	}
	return true;
}

bool Game::removeUnitAndBuilding(int x, int y, int size, SDL_Rect* r, int flags)
{
	int sts=size>>1;
	int stp=(~size)&1;
	SDL_Rect rl;
	bool rv=false;
	bool ri=false;

	for (int scx=(x-sts); scx<=(x+sts-stp); scx++)
		for (int scy=(y-sts); scy<=(y+sts-stp); scy++)
		{
			rv=removeUnitAndBuilding((scx&(map.wMask)), (scy&(map.hMask)), &rl, flags) || rv;
			if (ri)
			{
				Utilities::rectExtendRect(&rl, r);
			}
			else
			{
				*r=rl;
				ri=true;
			}

		}
	return rv;
}

bool Game::checkRoomForBuilding(int coordX, int coordY, int typeNum, int *mapX, int *mapY, Sint32 team)
{
	BuildingType *bt=buildingsTypes.buildingsTypes[typeNum];
	int x=coordX+bt->decLeft;
	int y=coordY+bt->decTop;

	*mapX=x;
	*mapY=y;

	return checkRoomForBuilding(x, y, typeNum, team);
}

bool Game::checkRoomForBuilding(int x, int y, int typeNum, Sint32 team)
{
	BuildingType *bt=buildingsTypes.buildingsTypes[typeNum];
	int w=bt->width;
	int h=bt->height;

	bool isRoom=true;
	if (bt->isVirtual)
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if (map.getUnit(dx, dy)!=NOUID)
					isRoom=false;
		return isRoom;
	}
	else
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if ((!map.isGrass(dx, dy)) || (map.getUnit(dx, dy)!=NOUID))
					isRoom=false;
	}
	
	if (team<0)
		return isRoom;
	
	if (isRoom)
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if (map.isMapDiscovered(dx, dy, team))
					return true;
		return false;
	}
	else
		return false;
	
}

bool Game::checkHardRoomForBuilding(int coordX, int coordY, int typeNum, int *mapX, int *mapY, Sint32 team)
{
	BuildingType *bt=buildingsTypes.buildingsTypes[typeNum];
	int x=coordX+bt->decLeft;
	int y=coordY+bt->decTop;

	*mapX=x;
	*mapY=y;

	return checkHardRoomForBuilding(x, y, typeNum, team);
}

bool Game::checkHardRoomForBuilding(int x, int y, int typeNum, Sint32 team)
{
	BuildingType *bt=buildingsTypes.buildingsTypes[typeNum];
	int w=bt->width;
	int h=bt->height;

	bool isRoom=true;
	if (bt->isVirtual)
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if ((map.getUnit(dx, dy)!=NOUID) && (map.getUnit(dx, dy)<0))
					isRoom=false;
		return isRoom;
	}
	else
	{
		for (int dy=y; dy<y+h; dy++)
			for (int dx=x; dx<x+w; dx++)
				if ((!map.isGrass(dx, dy)) || ((map.getUnit(dx, dy)!=NOUID) && (map.getUnit(dx, dy)<0)))
					isRoom=false;
	}
	
	return isRoom;
}

/*bool Game::checkRoomForUnit(int x, int y)
{
	bool isRoom=true;
	//Check if there is neither ressouce nor other unit, nor building.
	if (map.isRessource(x, y) || (map.getUnit(x, y)!=NOUID))
		isRoom=false;
	return isRoom;
}*/

void Game::drawPointBar(int x, int y, BarOrientation orientation, int maxLength, int actLength, Uint8 r, Uint8 g, Uint8 b, int barWidth)
{
	if ((orientation==LEFT_TO_RIGHT) || (orientation==RIGHT_TO_LEFT))
	{
		/*globalContainer->gfx.drawHorzLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx.drawHorzLine(x, y+barWidth+1, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx.drawVertLine(x+i*3, y+1, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx.drawFilledRect(x, y, maxLength*3+1, barWidth+2, 0, 0, 0);
			
		if (orientation==LEFT_TO_RIGHT)
		{
			for (int i=0; i<actLength; i++)
				globalContainer->gfx.drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
		}
		else
		{
			for (int i=maxLength-actLength; i<maxLength; i++)
				globalContainer->gfx.drawFilledRect(x+i*3+1, y+1, 2, barWidth, r, g, b);
		}
	}
	else if ((orientation==BOTTOM_TO_TOP) || (orientation==TOP_TO_BOTTOM))
	{
		/*globalContainer->gfx.drawVertLine(x, y, maxLength*3+1, 32, 32, 32);
		globalContainer->gfx.drawVertLine(x+barWidth+1, y, maxLength*3+1, 32, 32, 32);
		for (int i=0; i<maxLength+1; i++)
			globalContainer->gfx.drawHorzLine(x+1, y+i*3, barWidth, 32, 32, 32);
		*/
		globalContainer->gfx.drawFilledRect(x, y, barWidth+2, maxLength*3+1, 0, 0, 0);
	
		if (orientation==TOP_TO_BOTTOM)
		{
			for (int i=0; i<actLength; i++)
				globalContainer->gfx.drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
		}
		else
		{
			for (int i=maxLength-actLength; i<maxLength; i++)
				globalContainer->gfx.drawFilledRect(x+1, y+i*3+1, barWidth, 2, r, g, b);
		}
	}
	else
		assert(false);
}

void Game::drawMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected, bool drawHealthFoodBar, bool drawPathLines, bool drawBuildingRects, const bool useMapDiscovered)
{
	int x, y, id;
	int left=(sx>>5);
	int top=(sy>>5);
	int right=((sx+sw+31)>>5);
	int bot=((sy+sh+31)>>5);
	std::set<int> buildingList;

	// we draw the terrains, eventually with debug rects:
	for (y=top; y<=bot; y++)
	{
		for (x=left; x<=right; x++)
			if ((map.isMapDiscovered(x+viewportX, y+viewportY, teamSelected)||(!useMapDiscovered)))
			{
				// draw terrain
				id=map.getTerrain(x+viewportX, y+viewportY);
				PalSprite *sprite;
				if (id<272)
				{
					sprite=(PalSprite *)globalContainer->terrain.getSprite(id);
				}
				else
				{
					sprite=(PalSprite *)globalContainer->ressources.getSprite(id-272);
				}

				if (map.isFOW(x+viewportX, y+viewportY, teamSelected)||(!useMapDiscovered))
					sprite->setPal(&globalContainer->macPal);
				else
					sprite->setPal(&globalContainer->ShadedPal);

				globalContainer->gfx.drawSprite(sprite , x<<5, y<<5);
				// draw Unit or Building
				#ifdef DBG_UID
				if ((!useMapDiscovered) || map.isMapDiscovered(x+viewportX, y+viewportY, teamSelected))
				{
					int UID=map.getUnit(x+viewportX,y+viewportY);
					if (UID!=NOUID)
					{
						if (UID>=0)
							globalContainer->gfx.drawRect(x<<5, y<<5, 32, 32, 0, 0, 255);
						else
							globalContainer->gfx.drawRect(x<<5, y<<5, 32, 32, 255, 0, 0);
					}
					if (map.isRessource(x+viewportX, y+viewportY, (RessourceType)0/*ALGA*/))
						globalContainer->gfx.drawRect(x<<5, y<<5, 32, 32, 255, 0, 255);
				}
				#endif
			}
	}

	// we draw the units and put the buildings in a list:
	mouseUnit=NULL;

	for (y=top-1; y<=bot; y++)
	{
		for (x=left-1; x<=right; x++)
		{
			//if ((!useMapDiscovered) || map.isMapDiscovered(x+viewportX, y+viewportY, teamSelected))
			{
				Sint16 uid=map.getUnit(x+viewportX, y+viewportY);
				if (uid>=0) // Then this is an unit.
				{
					id=Unit::UIDtoID(uid);
					int team=Unit::UIDtoTeam(uid);

					Unit *unit=teams[team]->myUnits[id];

					assert(unit);
					if (unit==NULL)
					{
						printf("warninig, inexistant unit on the map!\n");
						continue;
					}
					int dx=unit->dx;
					int dy=unit->dy;
					if (useMapDiscovered)
					{
						if ((!map.isFOW(x+viewportX, y+viewportY, teamSelected))&&(!map.isFOW(x+viewportX-dx, y+viewportY-dy, teamSelected)))
							continue;
					}

					int imgid;

					UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);

					imgid=ut->startImage[unit->action];

					int px, py;
					map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, viewportX, viewportY);
					int deltaLeft=255-unit->delta;
					if (unit->action<BUILD)
					{
						px-=(unit->dx*deltaLeft)>>3;
						py-=(unit->dy*deltaLeft)>>3;
					}
					else
					{
						// TODO : if looks ugly, do something intelligent here
					}

					int dir=unit->direction;
					if (dir==8)
					{
						imgid+=8*((unit->delta)>>5);
					}
					else
					{
						imgid+=(unit->delta)>>5;
						imgid+=8*dir;
					}

					PalSprite *unitSprite=(PalSprite *)globalContainer->units.getSprite(imgid);
					unitSprite->setPal(&(teams[team]->palette));

					globalContainer->gfx.drawSprite(unitSprite, px, py);
					if (unit==selectedUnit)
						globalContainer->gfx.drawCircle(px+16, py+16, 16, 0, 0, 255);

					if ((px<mouseX)&&((px+32)>mouseX)&&(py<mouseY)&&((py+32)>mouseY)&&((!useMapDiscovered)||(map.isFOW(x+viewportX, y+viewportY, teamSelected))||(Unit::UIDtoTeam(uid)==teamSelected)))
						mouseUnit=unit;

					if (drawHealthFoodBar)
					{
						drawPointBar(px+1, py+25, LEFT_TO_RIGHT, 10, unit->hungry/10000, 80, 179, 223);
						float hpRatio=(float)unit->hp/(float)unit->performance[HP];
						if (hpRatio>0.6)
							drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+9*hpRatio, 78, 187, 78);
						else if (hpRatio>0.3)
							drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+9*hpRatio, 255, 255, 0);
						else
							drawPointBar(px+1, py+25+3, LEFT_TO_RIGHT, 10, 1+9*hpRatio, 255, 0, 0);
					}
					if ((drawPathLines) && (unit->movement==Unit::MOV_GOING_TARGET))
					{
						int lsx, lsy, ldx, ldy;
						lsx=px+16;
						lsy=py+16;
						//#ifdef DBG_PATHFINDING
						if (unit->bypassDirection && unit->verbose)
						{
							unit->owner->game->map.mapCaseToDisplayable(unit->tempTargetX, unit->tempTargetY, &ldx, &ldy, viewportX, viewportY);
							globalContainer->gfx.drawLine(lsx, lsy, ldx+16, ldy+16, 100, 100, 250);
							unit->owner->game->map.mapCaseToDisplayable(unit->borderX, unit->borderY, &ldx, &ldy, viewportX, viewportY);
							globalContainer->gfx.drawLine(lsx, lsy, ldx+16, ldy+16, 250, 100, 100);
							unit->owner->game->map.mapCaseToDisplayable(unit->obstacleX, unit->obstacleY, &ldx, &ldy, viewportX, viewportY);
							globalContainer->gfx.drawLine(lsx, lsy, ldx+16, ldy+16, 0, 50, 50);
						}
						//#endif
						unit->owner->game->map.mapCaseToDisplayable(unit->targetX, unit->targetY, &ldx, &ldy, viewportX, viewportY);
						globalContainer->gfx.drawLine(lsx, lsy, ldx+16, ldy+16, 250, 250, 250);
					}
				}
				else if (uid >= -16385)  // Then this is a building or a flag.
				{
					if ((!useMapDiscovered) || map.isFOW(x+viewportX, y+viewportY, teamSelected) || (Building::UIDtoTeam(uid)==teamSelected))
						buildingList.insert(uid);
				}
			}
		}
	}

	std::set<Building *> flagList;

	for (std::set <int>::iterator it=buildingList.begin(); it!=buildingList.end(); ++it)
	{
		int uid = *it;

		int id = Building::UIDtoID(uid);
		int team = Building::UIDtoTeam(uid);

		Building *building=teams[team]->myBuildings[id];
		BuildingType *type=building->type;

		if (type->isVirtual)
		{
			flagList.insert(building);
			//continue; TODO : optimise and have a big copy-past to show "drawHealthFoodBar" information in a flag more specifically.
		}

		if ((type->isCloacked) && (!(teams[teamSelected]->me & building->owner->allies)))
			continue;

		int imgid=type->startImage;

		map.mapCaseToDisplayable(building->posX, building->posY, &x, &y, viewportX, viewportY);

		if (type->hueImage)
		{
			// Here he hue all the sprite:

			PalSprite *buildingSprite=(PalSprite *)globalContainer->buildings.getSprite(imgid);
			buildingSprite->setPal(&(teams[team]->palette));
			globalContainer->gfx.drawSprite(buildingSprite, x, y);
		}
		else
		{
			// Here we draw the sprite with a flag:

			// First the sprite
			PalSprite *buildingSprite=(PalSprite *)globalContainer->buildings.getSprite(imgid);
			buildingSprite->setPal(&(globalContainer->macPal));
			globalContainer->gfx.drawSprite(buildingSprite, x, y);

			// Then we draw a hued flag of the team.
			int flagImgid=type->flagImage;
			//int w=building->type->width;
			int h=type->height;

			//We draw the flag at left bottom corner on the building
			PalSprite *flagSprite=(PalSprite *)globalContainer->buildings.getSprite(flagImgid);
			int fw=flagSprite->getW();
			//int fh=flagSprite->getH();
			flagSprite->setPal(&(globalContainer->macPal));


			globalContainer->gfx.drawSprite(flagSprite, x/*+(w<<5)-fh*/, y+(h<<5)-fw);

			//We add a hued color over the flag
			PalSprite *flagHue=(PalSprite *)globalContainer->buildings.getSprite(flagImgid+1);
			flagHue->setPal(&(teams[team]->palette));

			globalContainer->gfx.drawSprite(flagHue, x/*+(w<<5)-fh*/, y+(h<<5)-fw);
		}
		
		if (drawBuildingRects)
		{
			int batW=(type->width )<<5;
			int batH=(type->height)<<5;
			int typeNum=building->typeNum;
			globalContainer->gfx.drawRect(x, y, batW, batH, 255, 255, 255, 127);
			
			BuildingType *lastbt=buildingsTypes.getBuildingType(typeNum);
			int lastTypeNum=typeNum;
			int max=0;
			while(lastbt->nextLevelTypeNum>=0)
			{
				lastTypeNum=lastbt->nextLevelTypeNum;
				lastbt=buildingsTypes.getBuildingType(lastTypeNum);
				if (max++>200)
				{
					printf("GameGUI: Error: nextLevelTypeNum architecture is broken.\n");
					assert(false);
					break;
				}
			}
			int exBatX=x+((lastbt->decLeft-type->decLeft)<<5);
			int exBatY=y+((lastbt->decTop-type->decTop)<<5);
			int exBatW=(lastbt->width)<<5;
			int exBatH=(lastbt->height)<<5;
			
			globalContainer->gfx.drawRect(exBatX, exBatY, exBatW, exBatH, 255, 255, 255, 127);
		}
		
		if (drawHealthFoodBar)
		{
			int decy=(type->height*32);
			int healDecx=(type->width-2)*16+1;
			//int unitDecx=(building->type->width*16)-((3*building->maxUnitInside)>>1);

			// TODO : find better color for this
			// health
			if (type->hpMax)
			{
				float hpRatio=(float)building->hp/(float)type->hpMax;
				if (hpRatio>0.6)
					drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(15.0f*hpRatio), 78, 187, 78);
				else if (hpRatio>0.3)
					drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(15.0f*hpRatio), 255, 255, 0);
				else
					drawPointBar(x+healDecx+6, y+decy-4, LEFT_TO_RIGHT, 16, 1+(15.0f*hpRatio), 255, 0, 0);
			}

			// units

			if (building->maxUnitInside>0)
				drawPointBar(x+type->width*32-4, y+1, BOTTOM_TO_TOP, building->maxUnitInside, (signed)building->unitsInside.size(), 255, 255, 255);
			if (building->maxUnitWorking>0)
				drawPointBar(x+type->width*16-((3*building->maxUnitWorking)>>1), y+1,LEFT_TO_RIGHT , building->maxUnitWorking, (signed)building->unitsWorking.size(), 255, 255, 255);

			// food
			if ((type->canFeedUnit) || (type->unitProductionTime))
			{
				// compute bar size, prevent oversize
				int bDiv=1;
				assert(type->height!=0);
				while ( ((type->maxRessource[CORN]*3+1)/bDiv)>((type->height*32)-10))
					bDiv++;
				drawPointBar(x+1, y+1, BOTTOM_TO_TOP, type->maxRessource[CORN]/bDiv, building->ressources[CORN]/bDiv, 255, 255, 120, 1+bDiv);
			}

		}

	}

	// Let's paint the bullets:
	// TODO : optimise : test only possible sectors to show bullets.

	PalSprite *bulletSprite=(PalSprite *)globalContainer->buildings.getSprite(BULLET_IMGID);
	bulletSprite->setPal(&(globalContainer->macPal));       				

	int mapPixW=(map.w)<<5;
	int mapPixH=(map.h)<<5;
	
	for (int i=0; i<(map.wSector*map.hSector); i++)
	{
		Sector *s=&(map.sectors[i]);
		for (std::list<Bullet *>::iterator it=s->bullets.begin();it!=s->bullets.end();it++)
		{
			int x=(*it)->px-(viewportX<<5);
			int y=(*it)->py-(viewportY<<5);
			
			if (x<0)
				x+=mapPixW;
			if (y<0)
				y+=mapPixH;

			//printf("px=(%d, %d) vp=(%d, %d)\n", (*it)->px, (*it)->py, viewportX, viewportY);	
			if ( (x<=sw) && (y<=sh) )
				globalContainer->gfx.drawSprite(bulletSprite, x, y);
		}
	}

	// draw shading

	if (useMapDiscovered)
	{
		for (y=top; y<=bot; y++)
			for (x=left; x<=right; x++)
			{
				if ( (!map.isMapDiscovered(x+viewportX, y+viewportY, teamSelected)))
				{
					globalContainer->gfx.drawFilledRect(x<<5, y<<5, 32, 32, 10, 10, 10);
				}
				/*else if ( (!map.isFOW(x+viewportX, y+viewportY, teamSelected)))
				{
					for (int i=0; i<16; i++)
					{
						globalContainer->gfx.drawVertLine((x<<5)+(i<<1), y<<5, 32, 10, 10, 10);
						globalContainer->gfx.drawHorzLine((x<<5), (y<<5)+(i<<1), 32, 10, 10, 10);
					}
				}*/
			}
	}

	for (std::set <Building *>::iterator it2=flagList.begin(); it2!=flagList.end(); ++it2)
	{
		Building *building=*it2;
		BuildingType *type=building->type;
		int team=building->owner->teamNumber;

		if ((type->isCloacked) && (!(teams[teamSelected]->me & building->owner->allies)))
			continue;

		int imgid=type->startImage;

		map.mapCaseToDisplayable(building->posX, building->posY, &x, &y, viewportX, viewportY);

		// all flags are hued:
		PalSprite *buildingSprite=(PalSprite *)globalContainer->buildings.getSprite(imgid);
		buildingSprite->setPal(&(teams[team]->palette));
		globalContainer->gfx.drawSprite(buildingSprite, x, y);

		// flag circle:
		if (drawHealthFoodBar || (building==selectedBuilding))
			globalContainer->gfx.drawCircle(x+16, y+16, 16+(32*building->unitStayRange), 0, 0, 255);

	}

}

void Game::drawMiniMap(int sx, int sy, int sw, int sh, int viewportX, int viewportY, int teamSelected)
{
	SDL_Rect minimapZone;
	int rx, ry, rw, rh;
	minimapZone.x=globalContainer->gfx.getW()-128;
	minimapZone.y=0;
	minimapZone.w=128;
	minimapZone.h=128;
	SDL_BlitSurface(minimap,0,globalContainer->gfx.screen, &minimapZone);
	rx=viewportX;
	ry=viewportY;
	if (teamSelected>=0)
	{
		rx=(rx-teams[teamSelected]->startPosX+map.w-(map.w>>1));
		ry=(ry-teams[teamSelected]->startPosY+map.h-(map.h>>1));
		rx&=map.getMaskW();
		ry&=map.getMaskH();
	}
	rx=(rx*128)/map.getW();
	ry=(ry*128)/map.getH();
	rw=((globalContainer->gfx.getW()-128)*128)/(32*map.getW());
	rh=(globalContainer->gfx.getH()*128)/(32*map.getH());
	// horizontal line
	// WARNING : ugly copy paste, does anyone have any better idea ?
	if (globalContainer->gfx.screen->format->BitsPerPixel==16)
	{
		Uint16 *ptr1, *ptr2;
		int n;
		for (n=0;n<rw+1;n++)
		{
			ptr1=(Uint16 *)globalContainer->gfx.screen->pixels+((ry&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx+n)&0x7F);
			ptr2=(Uint16 *)globalContainer->gfx.screen->pixels+(((ry+rh)&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx+n)&0x7F);
			*ptr1=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
			*ptr2=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
		}
		for (n=0;n<rh+1;n++)
		{
			ptr1=(Uint16 *)globalContainer->gfx.screen->pixels+(((ry+n)&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx)&0x7F);
			ptr2=(Uint16 *)globalContainer->gfx.screen->pixels+(((ry+n)&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx+rw)&0x7F);
			*ptr1=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
			*ptr2=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
		}
	}
	else
	{
		Uint32 *ptr1, *ptr2;
		int n;
		for (n=0;n<rw+1;n++)
		{
			ptr1=(Uint32 *)globalContainer->gfx.screen->pixels+((ry&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx+n)&0x7F);
			ptr2=(Uint32 *)globalContainer->gfx.screen->pixels+(((ry+rh)&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx+n)&0x7F);
			*ptr1=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
			*ptr2=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
		}
		for (n=0;n<rh+1;n++)
		{
			ptr1=(Uint32 *)globalContainer->gfx.screen->pixels+(((ry+n)&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx)&0x7F);
			ptr2=(Uint32 *)globalContainer->gfx.screen->pixels+(((ry+n)&0x7F)*globalContainer->gfx.screen->w)+globalContainer->gfx.screen->w-128+((rx+rw)&0x7F);
			*ptr1=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
			*ptr2=SDL_MapRGB(globalContainer->gfx.screen->format,255,0,0);
		}
	}

}

void Game::renderMiniMap(int teamSelected, bool showUnitsAndBuildings)
{
	float dMx, dMy;
	int dx, dy;
	float minidx, minidy;
	int r, g, b;
	int nCount;
	Sint16 u;
	bool isMeUnitOrBuilding, isEnemyUnitOrBuilding, isAllyUnitOrBuilding;

	int H[3]= { 0, 90, 0 };
	int E[3]= { 0, 40, 120 };
	int S[3]= { 170, 170, 0 };
	int Player[3]= { 10, 240, 20 };
	int Enemy[3]={ 220, 25, 30 };
	int pcol[3];

	int decSPX, decSPY;

	dMx=(float)map.getW()/128.0f;
	dMy=(float)map.getH()/128.0f;

	if (teamSelected>=0)
	{
		decSPX=teams[teamSelected]->startPosX+map.w/2;
		decSPY=teams[teamSelected]->startPosY+map.h/2;
	}
	else
	{
		decSPX=0;
		decSPY=0;
	}

	Uint8 *ptr;
	int bpp=globalContainer->gfx.screen->format->BytesPerPixel;
	ptr=((Uint8 *)minimap->pixels);
	for (dy=0; dy<128; dy++)
	{
		ptr=((Uint8 *)minimap->pixels)+dy*minimap->pitch;
		for (dx=0; dx<128; dx++)
		{
			pcol[0]=0;
			pcol[1]=0;
			pcol[2]=0;
			nCount=0;
			isMeUnitOrBuilding=false;
			isEnemyUnitOrBuilding=false;
			isAllyUnitOrBuilding=false;

			// compute
			for (minidx=(dMx*dx)+decSPX; minidx<=(dMx*(dx+1))+decSPX; minidx++)
			{
				for (minidy=(dMy*dy)+decSPY; minidy<=(dMy*(dy+1))+decSPY; minidy++)
				{
					if (showUnitsAndBuildings)
					{
						u=map.getUnit(minidx, minidy);
						if (u!=NOUID)
						{
							// TODO : use ally mask
							if (u>=0)
							{
								if (Unit::UIDtoTeam(u)==teamSelected)
									isMeUnitOrBuilding=true;
								else if (map.isFOW(minidx, minidy, teamSelected))
									isEnemyUnitOrBuilding=true;
							}
							else
							{
								if (Building::UIDtoTeam(u)==teamSelected)
									isMeUnitOrBuilding=true;
								else if (map.isFOW(minidx, minidy, teamSelected))
									isEnemyUnitOrBuilding=true;
							}
						}
					}
					if (teamSelected<0)
						pcol[map.getUMTerrain((int)minidx,(int)minidy)]+=3;
					else if (map.isMapDiscovered(minidx, minidy, teamSelected))
					{
						if (map.isFOW(minidx, minidy, teamSelected))
							pcol[map.getUMTerrain((int)minidx,(int)minidy)]+=3;
						else
							pcol[map.getUMTerrain((int)minidx,(int)minidy)]+=2;
					}

					nCount++;
				}
			}

			if (isMeUnitOrBuilding)
			{
				r=Player[0];
				g=Player[1];
				b=Player[2];
			}
			else if (isEnemyUnitOrBuilding)
			{
				r=Enemy[0];
				g=Enemy[1];
				b=Enemy[2];
			}
			else
			{
				nCount*=3;
				r=(int)((H[0]*pcol[Map::GRASS]+E[0]*pcol[Map::WATER]+S[0]*pcol[Map::SAND])/(nCount));
				g=(int)((H[1]*pcol[Map::GRASS]+E[1]*pcol[Map::WATER]+S[1]*pcol[Map::SAND])/(nCount));
				b=(int)((H[2]*pcol[Map::GRASS]+E[2]*pcol[Map::WATER]+S[2]*pcol[Map::SAND])/(nCount));
			}
			switch (bpp)
			{
			case 1:
				*ptr=SDL_MapRGB(globalContainer->gfx.screen->format,r,g,b);
			case 2:
				*((Uint16 *)ptr)=SDL_MapRGB(globalContainer->gfx.screen->format,r,g,b);
			case 4:
				*((Uint32 *)ptr)=SDL_MapRGB(globalContainer->gfx.screen->format,r,g,b);
			}
			ptr+=bpp;
		}
	}
}

Sint32 Game::checkSum()
{
	Sint32 cs=0;

	// TODO : add checkSum() in heritated objets too.

	cs^=session.checkSum();
	
	cs=(cs<<31)|(cs>>1);
	for (int i=0; i<session.numberOfTeam; i++)
	{
		cs^=teams[i]->checkSum();
		cs=(cs<<31)|(cs>>1);
	}
	cs=(cs<<31)|(cs>>1);
	for (int i2=0; i2<session.numberOfPlayer; i2++)
	{
		cs^=players[i2]->checkSum();
		cs=(cs<<31)|(cs>>1);
	}
	cs=(cs<<31)|(cs>>1);
	cs^=map.checkSum();
	cs=(cs<<31)|(cs>>1);

	cs^=getSyncRandSeedA();
	cs^=getSyncRandSeedB();
	cs^=getSyncRandSeedC();
	
	return cs;
}
