/*
 * Globulation 2 game support
 * (c) 2001 Stephane Magnenat, Luc-Olivier de Charriere, Ysagoon
 */

#include "GameGUI.h"
#include "Game.h"
#include "GlobalContainer.h"

extern GlobalContainer globalContainer;

GameGUI::GameGUI()
{
	isRunning=true;
	needRedraw=true;
	drawHealthFoodBar=false;
	drawPathLines=false;
	viewportX=0;
	viewportY=0;
	mouseX=0;
	mouseY=0;
	displayMode=BUILDING_AND_FLAG;
	typeToBuild=-1;
	selBuild=NULL;
	selectionUID=0;
	font=new SDLBitmapFont("data/font2.png");
	statsPtr=0;
	memset(stats, 0, 128*sizeof(TeamStat));
	statMode=STAT_TEXT;

	for (int i=0; i<8; i++)
	{
		viewportSpeedX[i]=0;
		viewportSpeedY[i]=0;
	}
	
	typingMessage=false;
}

GameGUI::~GameGUI()
{
	delete font;
}

void GameGUI::step(void)
{
	SDL_Event event, mouseMotionEvent;
	bool wasMouseMotion=false;
	
	// we get all pending events but for mousemotion we only keep the last one
	while (SDL_PollEvent(&event))
	{
		if (event.type==SDL_MOUSEMOTION)
		{
			mouseMotionEvent=event;
			wasMouseMotion=true;
		}
		else
		{
			processEvent(&event);
		}
	}
	if (wasMouseMotion)
		processEvent(&mouseMotionEvent);
	
	viewportX+=game.map.getW();
	viewportY+=game.map.getH();
	for (int i=0; i<8; i++)
	{
		viewportX+=viewportSpeedX[i];
		viewportY+=viewportSpeedY[i];
	}
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();
	
	statStep();
}

void GameGUI::statStep(void)
{
	static int statPos=0;
	
	statPos++;
	if (statPos==20)
	{
		statsPtr++;
		statsPtr&=0x7F;
		statPos=0;
	}
	
	TeamStat newStats;
	game.teams[localTeam]->computeStat(&newStats);
	stats[statsPtr]=newStats;	
}

void GameGUI::processEvent(SDL_Event *event)
{
	if (event->type==SDL_KEYDOWN)
	{
		handleKey(event->key.keysym, true);
	}
	else if (event->type==SDL_KEYUP)
	{
		handleKey(event->key.keysym, false);
	}
	else if (event->type==SDL_MOUSEMOTION)
	{
		handleMouseMotion(event->motion.x, event->motion.y);
	}
	else if (event->type==SDL_MOUSEBUTTONDOWN)
	{
		int button=event->button.button;
		//int state=event->button.state;

		if (button==SDL_BUTTON_RIGHT)
		{
			handleRightClick();
		}
		else if (button==SDL_BUTTON_LEFT)
		{
			if (event->button.x>globalContainer.gfx.getW()-128)
				handleMenuClick(event->button.x-globalContainer.gfx.getW()+128, event->button.y, event->button.button);
			else
				handleMapClick(event->button.x, event->button.y, event->button.button);
		}
	}
	else if (event->type==SDL_QUIT)
	{
		//TODO :orderQueue.push(new PlayerQuitsGameOrder(localPlayer));
		isRunning=false;
	}
}

void GameGUI::handleRightClick(void)
{
	// We deselect all, we want no tools activated:
	displayMode=BUILDING_AND_FLAG;
	selBuild=NULL;
	selUnit=NULL;
	selectionUID=0;
	typeToBuild=-1;
	needRedraw=true;
}

void GameGUI::handleKey(SDL_keysym keySym, bool pressed)
{
	SDLKey key=keySym.sym;
	 
	int modifier;
	
	if (pressed)
		modifier=1;
	else
		modifier=-1;
	
	if (pressed && typingMessage && (font->printable(keySym.unicode)))
	{
		if (typedChar<MAX_MESSAGE_SIZE)
		{
			typedMessage[typedChar++]=keySym.unicode;
			typedMessage[typedChar]=0;
		}
	}
	
	switch (key)
	{
		case SDLK_ESCAPE:
			orderQueue.push(new PlayerQuitsGameOrder(localPlayer));
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
		    	viewportSpeedX[3]=1;
		    	viewportSpeedY[3]=1;
		    }
		    else
		    {
		    	viewportSpeedX[3]=0;
		    	viewportSpeedY[3]=0;
		    }
			break;
		case SDLK_PLUS:
		case SDLK_KP_PLUS:
			{
				if ((pressed) && (selBuild) && (selBuild->owner->teamNumber==localTeam) && (displayMode==BUILDING_SELECTION_VIEW) && (selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
				{
					int nbReq=(selBuild->maxUnitWorkingLocal+=1);
					orderQueue.push(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
			break;
		case SDLK_MINUS:
		case SDLK_KP_MINUS:
			{
				if ((pressed) && (selBuild) && (selBuild->owner->teamNumber==localTeam) && (displayMode==BUILDING_SELECTION_VIEW) && (selBuild->maxUnitWorkingLocal>0))
				{
					int nbReq=(selBuild->maxUnitWorkingLocal-=1);
					orderQueue.push(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
			break;
		case SDLK_d:
			{
				if ((pressed) && selBuild && (selBuild->owner->teamNumber==localTeam))
				{
					orderQueue.push(new OrderDelete(selBuild->UID));
				}
			}
			break;
		case SDLK_u:
		case SDLK_a:
			{
				if ((pressed) && (selBuild) && (selBuild->owner->teamNumber==localTeam) && (selBuild->type->nextLevelTypeNum!=-1) && (!selBuild->type->isBuildingSite))
				{
					orderQueue.push(new OrderUpgrade(selBuild->UID));
				}
			}
			break;
		case SDLK_p :
			if (pressed)
				drawPathLines=!drawPathLines;
			break;
		case SDLK_i :
			if (pressed)
				drawHealthFoodBar=!drawHealthFoodBar;
			break;
			
		case SDLK_RETURN :
			if (pressed)
			{
				if (typingMessage)
				{
					// TODO : send message only to some players
					orderQueue.push(new MessageOrder((Uint32)0xFFFFFFFF, typedMessage));
					
					typedMessage[0]=0;
					typedChar=0;
					typingMessage=false;
				}
				else
				{
					typedMessage[0]=0;
					typedChar=0;
					typingMessage=true;
				}
			}
		break;
		case SDLK_BACKSPACE :
			if (pressed && (typedChar>0))
			{
				typedChar--;
				typedMessage[typedChar]=0;
			}
		break;
		default:
			
		break;
	}
}

void GameGUI::viewportFromMxMY(int mx, int my)
{
	viewportX=((mx*game.map.getW())>>7)-((globalContainer.gfx.getW()-128)>>6);
	viewportY=((my*game.map.getH())>>7)-((globalContainer.gfx.getH())>>6);
	viewportX+=game.teams[localTeam]->startPosX+(game.map.w>>1);
	viewportY+=game.teams[localTeam]->startPosY+(game.map.h>>1);
	if (viewportX<0)
		viewportX+=game.map.getW();
	if (viewportY<0)
		viewportY+=game.map.getH();
}

void GameGUI::handleMouseMotion(int mx, int my)
{
	const int scrollZoneWidth=8;
	game.mouseX=mouseX=mx;
	game.mouseY=mouseY=my;

	if (mx<scrollZoneWidth)
		viewportSpeedX[0]=-1;
	else if ((mx>globalContainer.gfx.getW()-scrollZoneWidth) )
		viewportSpeedX[0]=1;
	else
		viewportSpeedX[0]=0;

	if (my<scrollZoneWidth)
		viewportSpeedY[0]=-1;
	else if (my>globalContainer.gfx.getH()-scrollZoneWidth)
		viewportSpeedY[0]=1;
	else
		viewportSpeedY[0]=0;

	if ((mx>globalContainer.gfx.getW()-128) && (my<128) && (SDL_GetMouseState(NULL, NULL)&SDL_BUTTON(1)))
	{
		viewportFromMxMY(mx, my);
	}
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
	if (typeToBuild>=0)
	{
		// we get the type of building
		int mapX, mapY;

		// TODO : when we eant building site, pass true tu getTypeNum
		int typeNum;
		if (typeToBuild<(int)BuildingType::EXPLORATION_FLAG)
			typeNum=game.buildingsTypes.getTypeNum(typeToBuild, 0, true);
		else
			typeNum=game.buildingsTypes.getTypeNum(typeToBuild, 0, false);
		BuildingType *bt=game.buildingsTypes.buildingsTypes[typeNum];

		int tempX, tempY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		bool isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, localTeam);

		if (isRoom)
		{
			orderQueue.push(new OrderCreate(localTeam, mapX, mapY, (BuildingType::BuildingTypeNumber)typeNum));
		}
	}
	else if (game.mouseUnit)
	{
		selUnit=game.mouseUnit;
		selBuild=NULL;
		// an unit is selected:
		displayMode=UNIT_SELECTION_VIEW;
		selectionUID=selUnit->UID;
		game.selectedUnit=selUnit;
		checkValidSelection();
	}
	else
	{
		int mapX, mapY;
		Sint32 UID;
		game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
		UID=game.map.getUnit(mapX, mapY);
		if (UID!=NOUID)
		{
			if ((UID<0)&&((Building::UIDtoTeam(UID)==localTeam))||(game.map.isFOW(mapX, mapY, localTeam)))
			{
				displayMode=BUILDING_SELECTION_VIEW;
				game.selectedUnit=NULL;
				selectionUID=UID;
				checkValidSelection();
			}
		}
		else
		{
			// don't change anything
			/*displayMode=BUILDING_AND_FLAG;
			game.selectedUnit=NULL;
			game.selectedBuilding=NULL;
			selBuild=NULL;
			selUnit=NULL;
			needRedraw=true;*/
		}
	}
}

void GameGUI::handleMenuClick(int mx, int my, int button)
{
	// handle minimap
	if (my<128)
	{
		viewportFromMxMY(mx, my);
	}
	else if (displayMode==BUILDING_AND_FLAG)
	{
		if (my<480-32)
		{
			int xNum=mx>>6;
			int yNum=(my-128)>>6;
			typeToBuild=yNum*2+xNum;
			needRedraw=true;
		}
		else
		{
			handleRightClick();
			displayMode=STAT_VIEW;
			needRedraw=true;
		}
	}
	else if (displayMode==STAT_VIEW)
	{
		// do nothibg for now, it's stat
		((int)statMode)++;
		if (((int)statMode)==NB_STAT_MODE)
			((int)statMode)=0;
	}
	else if (displayMode==BUILDING_SELECTION_VIEW)
	{
		assert (selBuild);
		// TODO : handle this in a nice way
		if (selBuild->owner->teamNumber!=localTeam)
			return;
		if ((my>256+35) && (my<256+35+16)  && (selBuild->type->maxUnitWorking) && (selBuild->buildingState==Building::ALIVE))
		{
			int nbReq;
			if (mx<16)
			{
				if(selBuild->maxUnitWorkingLocal>0)
				{
					nbReq=(selBuild->maxUnitWorkingLocal-=1);
					orderQueue.push(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
			else if (mx<128-16)
			{
				nbReq=selBuild->maxUnitWorkingLocal=((mx-16)*MAX_UNIT_WORKING)/94;
				orderQueue.push(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
			}
			else
			{
				if(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)
				{
					nbReq=(selBuild->maxUnitWorkingLocal+=1);
					orderQueue.push(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
		}

		if ((selBuild->type->defaultUnitStayRange) && (my>256+144) && (my<256+144+16))
		{
			int nbReq;
			if (mx<16)
			{
				if(selBuild->unitStayRangeLocal>0)
				{
					nbReq=(selBuild->unitStayRangeLocal-=1);
					orderQueue.push(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
				}
			}
			else if (mx<128-16)
			{
				nbReq=selBuild->unitStayRangeLocal=((mx-16)*MAX_FLAG_RANGE)/94;
				orderQueue.push(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
			}
			else
			{
				if(selBuild->unitStayRangeLocal<MAX_FLAG_RANGE)
				{
					nbReq=(selBuild->unitStayRangeLocal+=1);
					orderQueue.push(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
				}
			}
		}

		if (selBuild->type->unitProductionTime)
		{
			for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
			{
				if ((my>256+80+(i*20))&&(my<256+80+(i*20)+16))
				{
					if (mx<16)
					{
						if (selBuild->ratioLocal[i]>0)
						{
							selBuild->ratioLocal[i]--;
							orderQueue.push(new OrderModifySwarms(&(selBuild->UID), (Sint32 [][UnitType::NB_UNIT_TYPE])&(selBuild->ratioLocal), 1));
						}
					}
					else if (mx<128-16)
					{
						selBuild->ratioLocal[i]=((mx-16)*MAX_RATIO_RANGE)/94;
						orderQueue.push(new OrderModifySwarms(&(selBuild->UID), (Sint32 [][UnitType::NB_UNIT_TYPE])&(selBuild->ratioLocal), 1));

					}
					else
					{
						if (selBuild->ratioLocal[i]<MAX_RATIO_RANGE)
						{
							selBuild->ratioLocal[i]++;
							orderQueue.push(new OrderModifySwarms(&(selBuild->UID), (Sint32 [][UnitType::NB_UNIT_TYPE])&(selBuild->ratioLocal), 1));
						}
					}
					//printf("ratioLocal[%d]=%d\n", i, selBuild->ratioLocal[i]);
				}
			}
		}

		if ((my>256+172) && (my<256+172+16))
		{
			orderQueue.push(new OrderDelete(selBuild->UID));
		}

		if ((my>256+172+16+8) && (my<256+172+16+8+16))
		{
			if (selBuild->buildingState==Building::WAITING_FOR_UPGRADE)
			{
				if ((selBuild->type->lastLevelTypeNum!=-1))
					orderQueue.push(new OrderCancelUpgrade(selBuild->UID));
			}
			else if (selBuild->buildingState==Building::WAITING_FOR_UPGRADE_ROOM)
			{
				orderQueue.push(new OrderCancelUpgrade(selBuild->UID));
			}
			else if ((selBuild->type->lastLevelTypeNum!=-1) && (selBuild->type->isBuildingSite))
			{
				orderQueue.push(new OrderCancelUpgrade(selBuild->UID));
			}
			else if ((selBuild->type->nextLevelTypeNum!=-1) && (!selBuild->type->isBuildingSite))
			{
				orderQueue.push(new OrderUpgrade(selBuild->UID));
			}
		}
	}
	else if (displayMode==UNIT_SELECTION_VIEW)
	{
		selUnit->verbose=!selUnit->verbose;
	}
}

Order *GameGUI::getOrder(void)
{
	if (orderQueue.size()==0)
	{
		Order *order=new SubmitCheckSumOrder(game.checkSum());
		return order;
		//return new NullOrder();
	}
	else
	{
		Order *order=orderQueue.front();
		orderQueue.pop();
		
		return order;
	}
}

void GameGUI::draw(void)
{
	// TODO : compare building to see if something has changed
	
	if (game.anyPlayerWaited && game.maskAwayPlayer)
	{
		int nbap=0; // Number of away players
		Uint32 pm=1;
		Uint32 apm=game.maskAwayPlayer;
		for(int pi=0; pi<game.session.numberOfPlayer; pi++)
		{
			if (pm&apm)
				nbap++;
			pm=pm<<1;
		}
		
		globalContainer.gfx.drawFilledRect(32, 32, globalContainer.gfx.getW()-128-64, 22+nbap*20, 0, 127, 255);
		globalContainer.gfx.drawRect(32, 32, globalContainer.gfx.getW()-128-64, 22+nbap*20, 0, 255, 127);
		pm=1;
		int pnb=0;
		for(int pi=0; pi<game.session.numberOfPlayer; pi++)
		{
			if (pm&apm)
			{
				
				globalContainer.gfx.drawString(48, 48+pnb*20, font,"%s%d", globalContainer.texts.getString("[l waiting for player]"), pi, globalContainer.texts.getString("[r waiting for player]"));
				pnb++;
			}
			pm=pm<<1;
		}
		
		
	}
	else
	{
		int ymesg=32;
		for (std::list <Message>::iterator it=messagesList.begin(); it!=messagesList.end(); ++it)
		{
			globalContainer.gfx.drawString(32, ymesg, font, "%s", it->text);
			ymesg+=20;
			it->showTicks--;
		}
		for (std::list <Message>::iterator it=messagesList.begin(); it!=messagesList.end(); ++it)
		{
			if (it->showTicks<0)
			{
				std::list<Message>::iterator ittemp=it;
				it=messagesList.erase(ittemp);
			}
		}
	}
	
	if (typingMessage)
	{
		globalContainer.gfx.drawString(40, 440, font, ": %s", typedMessage);
	}
	
	//if (needRedraw)
	checkValidSelection();
	{
		needRedraw=false;
		globalContainer.gfx.drawFilledRect(globalContainer.gfx.getW()-128, 128, 128, globalContainer.gfx.getH()-128, 0, 0, 0);

		if (displayMode==BUILDING_AND_FLAG)
		{
			for (int i=0; i<10; i++)
			{
				int typeNum=game.buildingsTypes.getTypeNum(i, 0, false);
				BuildingType *bt=game.buildingsTypes.getBuildingType(typeNum);
				int imgid=bt->startImage;
				int x=((i&0x1)<<6)+globalContainer.gfx.getW()-128;
				int y=((i>>1)<<6)+128;
				int decX=0;
				int decY=0;

				globalContainer.gfx.setClipRect(x+6, y+6, 52, 52);
				PalSprite *buildingSprite=(PalSprite *)globalContainer.buildings.getSprite(imgid);
			
				if (buildingSprite->getW()<=32)
					decX=-16;
				else if (buildingSprite->getW()>64)
					decX=20;
				if (buildingSprite->getH()<=32)
					decY=-16;
				else if (buildingSprite->getH()>64)
					decY=20;
			
				if (bt->hueImage)
					buildingSprite->setPal(&(game.teams[localTeam]->palette));
				else
					buildingSprite->setPal(&(globalContainer.macPal));
		
				globalContainer.gfx.drawSprite(buildingSprite, x-decX, y-decY);
			}
			
			if (typeToBuild>=0)
			{
				int x=((typeToBuild&0x1)<<6)+globalContainer.gfx.getW()-128;
				int y=((typeToBuild>>1)<<6)+128;
				globalContainer.gfx.setClipRect(globalContainer.gfx.getW()-128, 128, 128, globalContainer.gfx.getH()-128);
				globalContainer.gfx.drawRect(x+6, y+6, 52, 52, 255, 0, 0);
				globalContainer.gfx.drawRect(x+5, y+5, 54, 54, 255, 0, 0);
			}

			int fu=game.teams[localTeam]->freeUnits;
			globalContainer.gfx.setClipRect(globalContainer.gfx.getW()-128, 460, 128, 20);
			if (fu<=0)
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-120, 460, font,"%s",globalContainer.texts.getString("[no unit free]"));
			else if (fu==1)
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-120, 460, font,"%s",globalContainer.texts.getString("[one unit free]"));
			else
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-120, 460, font,"%s%d%s",globalContainer.texts.getString("[l units free]"), fu, globalContainer.texts.getString("[r units free]"));
		}
		else if (displayMode==BUILDING_SELECTION_VIEW)
		{
			assert(selBuild);

			// building icon
			globalContainer.gfx.setClipRect(globalContainer.gfx.getW()-128, 128, 128, 128);
			PalSprite *buildingSprite=(PalSprite *)globalContainer.buildings.getSprite(selBuild->type->startImage);
			if (selBuild->type->hueImage)
				buildingSprite->setPal(&(game.teams[selBuild->owner->teamNumber]->palette));
			else
				buildingSprite->setPal(&(globalContainer.macPal));
			globalContainer.gfx.drawSprite(buildingSprite,
				globalContainer.gfx.getW()-128+64-selBuild->type->width*16,
				128+64-selBuild->type->height*16);
				
			// building text
			drawTextCenter(globalContainer.gfx.getW()-128, 128+8, "[building name]", selBuild->type->type);
			
			// building Infos
			globalContainer.gfx.setClipRect(globalContainer.gfx.getW()-128, 128, 128, globalContainer.gfx.getH()-128);

			if (selBuild->type->hpMax)
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+2, font, "%s : %d/%d", globalContainer.texts.getString("[hp]"), selBuild->hp, selBuild->type->hpMax);
			if (selBuild->type->armor)
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+12, font, "%s : %d", globalContainer.texts.getString("[armor]"), selBuild->type->armor);
			if (selBuild->type->shootDamage)
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+22, font, "%s : %d", globalContainer.texts.getString("[damage]"), selBuild->type->shootDamage);

			if (selBuild->type->maxUnitWorking)
			{
				if (selBuild->buildingState==Building::ALIVE)
				{
					globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+23, font, "%s : %d/%d", globalContainer.texts.getString("[working]"), selBuild->unitsWorking.size(), selBuild->maxUnitWorking);
					drawScrollBox(globalContainer.gfx.getW()-128, 256+35, selBuild->maxUnitWorking, selBuild->maxUnitWorkingLocal, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
				}
				else
				{
					if (selBuild->unitsWorking.size()>1)
					{
						globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+23, font, "%s%d%s",
							globalContainer.texts.getString("[still (w)]"),
							selBuild->unitsWorking.size(),
							globalContainer.texts.getString("[units working]"));
					}
					else if (selBuild->unitsWorking.size()==1)
					{
						globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+23, font, "%s",
							globalContainer.texts.getString("[still one unit working]") );
					}
				}
			}
			
			if (selBuild->type->unitProductionTime)
			{
				int Left=(selBuild->productionTimeout*128)/selBuild->type->unitProductionTime;
				int Elapsed=128-Left;
				globalContainer.gfx.drawFilledRect(globalContainer.gfx.getW()-128, 256+55, Elapsed, 7, 100, 100, 255);
				globalContainer.gfx.drawFilledRect(globalContainer.gfx.getW()-128+Elapsed, 256+55, Left, 7, 128, 128, 128);

				for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
				{
					drawScrollBox(globalContainer.gfx.getW()-128, 256+80+(i*20), selBuild->ratio[i], selBuild->ratioLocal[i], 0, MAX_RATIO_RANGE);
					globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+24, 256+83+(i*20), font, "%s", globalContainer.texts.getString("[unit type]", i));
				}
			}
			
			if (selBuild->type->maxUnitInside)
			{
				if (selBuild->buildingState==Building::ALIVE)
				{
					globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+52, font, "%s : %d/%d", globalContainer.texts.getString("[inside]"), selBuild->unitsInside.size(), selBuild->maxUnitInside);
				}
				else
				{
					if (selBuild->unitsInside.size()>1)
					{
						globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+52, font, "%s%d%s",
							globalContainer.texts.getString("[still (i)]"),
							selBuild->unitsInside.size(),
							globalContainer.texts.getString("[units inside]"));
					}
					else if (selBuild->unitsInside.size()==1)
					{
						globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+52, font, "%s",
							globalContainer.texts.getString("[still one unit inside]") );
					}
				}
			}
			for (int i=0; i<NB_RESSOURCES; i++)
				if (selBuild->type->maxRessource[i])
					globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+54+(i*10), font, "%s : %d/%d", globalContainer.texts.getString("[ressources]", i), selBuild->ressources[i], selBuild->type->maxRessource[i]);
			
			if (selBuild->type->defaultUnitStayRange)
			{
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-128+4, 256+132, font, "%s : %d", globalContainer.texts.getString("[range]"), selBuild->unitStayRange);
				drawScrollBox(globalContainer.gfx.getW()-128, 256+144, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, MAX_FLAG_RANGE);
			}
			
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				drawTextCenter(globalContainer.gfx.getW()-128, 256+172, "[wait destroy]");
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				drawButton(globalContainer.gfx.getW()-128+16, 256+172, "[destroy]");
			}
			
			if (selBuild->buildingState==Building::WAITING_FOR_UPGRADE)
			{
				if ((selBuild->type->lastLevelTypeNum!=-1))
					drawButton(globalContainer.gfx.getW()-128+16, 256+172+16+8, "[cancel upgrade]");
			}
			else if (selBuild->buildingState==Building::WAITING_FOR_UPGRADE_ROOM)
			{
				drawButton(globalContainer.gfx.getW()-128+16, 256+172+16+8, "[cancel upgrade]");
			}
			else if ((selBuild->type->lastLevelTypeNum!=-1) && (selBuild->type->isBuildingSite))
			{
				drawButton(globalContainer.gfx.getW()-128+16, 256+172+16+8, "[cancel upgrade]");
			}
			else if ((selBuild->type->nextLevelTypeNum!=-1) && (!selBuild->type->isBuildingSite))
			{
				drawButton(globalContainer.gfx.getW()-128+16, 256+172+16+8, "[upgrade]");
			}
		}
		else if (displayMode==UNIT_SELECTION_VIEW)
		{
			Sint32 UID=selUnit->UID;
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+  0, font, "hp=%d", selUnit->hp);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 15, font, "UID=%d", UID);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 30, font, "id=%d", Unit::UIDtoID(UID));
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 45, font, "Team=%d", Unit::UIDtoTeam(UID));
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 60, font, "medical=%d", selUnit->medical);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 75, font, "activity=%d", selUnit->activity);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 90, font, "displacement=%d", selUnit->displacement);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+105, font, "movement=%d", selUnit->movement);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+120, font, "action=%d", selUnit->action);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+135, font, "pox=(%d;%d)", selUnit->posX, selUnit->posY);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+150, font, "d=(%d;%d)", selUnit->dx, selUnit->dy);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+165, font, "direction=%d", selUnit->direction);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+180, font, "target=(%d;%d)", selUnit->targetX, selUnit->targetY);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+195, font, "tempTarget=(%d;%d)", selUnit->tempTargetX, selUnit->tempTargetY);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+210, font, "obstacle=(%d;%d)", selUnit->obstacleX, selUnit->obstacleY);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+225, font, "border=(%d;%d)", selUnit->borderX, selUnit->borderY);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+240, font, "bypassDirection=%d", selUnit->bypassDirection);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+255, font, "ab=%x", selUnit->attachedBuilding);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+270, font, "speed=%d", selUnit->speed);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+285, font, "verbose=%d", selUnit->verbose);
			globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+300, font, "subscribed=%d", selUnit->subscribed);
			
		}
		else if (displayMode==STAT_VIEW)
		{
			TeamStat newStats;
			newStats=stats[statsPtr];
			if (statMode==STAT_TEXT)
			{
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+  0, font, "Units newStats :");
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 15, font, "%d free on %d", newStats.isFree, newStats.totalUnit);
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 30+5, font, "%3.2f %% Worker", ((float)newStats.numberPerType[0])*100.0f/((float)newStats.totalUnit));
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 45+5, font, "%3.2f %% Explorer", ((float)newStats.numberPerType[1])*100.0f/((float)newStats.totalUnit));
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 60+5, font, "%3.2f %% Warrior", ((float)newStats.numberPerType[2])*100.0f/((float)newStats.totalUnit));
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 75+10, font, "%3.2f %% are Ok", ((float)newStats.needNothing)*100.0f/((float)newStats.totalUnit));
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+ 90+10, font, "%3.2f %% needs food", ((float)newStats.needFood)*100.0f/((float)newStats.totalUnit));
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+105+10, font, "%3.2f %% needs heal ", ((float)newStats.needHeal)*100.0f/((float)newStats.totalUnit));
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+120+15, font, "Upgrades levels :");
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+135+15, font, "Walk %d/%d/%d/%d", newStats.upgradeState[WALK][0], newStats.upgradeState[WALK][1], newStats.upgradeState[WALK][2], newStats.upgradeState[WALK][3]);
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+150+15, font, "Swim %d/%d/%d/%d", newStats.upgradeState[SWIM][0], newStats.upgradeState[SWIM][1], newStats.upgradeState[SWIM][2], newStats.upgradeState[SWIM][3]);
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+165+15, font, "Build %d/%d/%d/%d", newStats.upgradeState[BUILD][0], newStats.upgradeState[BUILD][1], newStats.upgradeState[BUILD][2], newStats.upgradeState[BUILD][3]);
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+180+15, font, "Harvest %d/%d/%d/%d", newStats.upgradeState[HARVEST][0], newStats.upgradeState[HARVEST][1], newStats.upgradeState[HARVEST][2], newStats.upgradeState[HARVEST][3]);
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+195+15, font, "At Speed %d/%d/%d/%d", newStats.upgradeState[ATTACK_SPEED][0], newStats.upgradeState[ATTACK_SPEED][1], newStats.upgradeState[ATTACK_SPEED][2], newStats.upgradeState[ATTACK_SPEED][3]);
				globalContainer.gfx.drawString(globalContainer.gfx.getW()-124, 128+210+15, font, "At Strength %d/%d/%d/%d", newStats.upgradeState[ATTACK_STRENGTH][0], newStats.upgradeState[ATTACK_STRENGTH][1], newStats.upgradeState[ATTACK_STRENGTH][2], newStats.upgradeState[ATTACK_STRENGTH][3]);
			}
			else
			{
				int maxUnit=0;
				for (int i=0; i<128; i++)
				{
					if (stats[i].totalUnit>maxUnit)
						maxUnit=stats[i].totalUnit;
				}
				for (int i=0; i<128; i++)
				{
					int index=(statsPtr+i+1)&0x7F;
					int nbFree=(stats[index].isFree*64)/maxUnit;
					int nbTotal=(stats[index].totalUnit*64)/maxUnit;
					globalContainer.gfx.drawVertLine(globalContainer.gfx.getW()-128+i, 128+ 10 +64-nbTotal, nbTotal-nbFree, 0, 0, 255);
					globalContainer.gfx.drawVertLine(globalContainer.gfx.getW()-128+i, 128+ 10 +64-nbFree, nbFree, 0, 255, 0);
					int nbOk, nbNeedFood, nbNeedHeal;
					if (stats[index].totalUnit)
					{  
						nbOk=(stats[index].needNothing*64)/stats[index].totalUnit;
						nbNeedFood=(stats[index].needFood*64)/stats[index].totalUnit;
						nbNeedHeal=(stats[index].needHeal*64)/stats[index].totalUnit;
					}
					else
					{
						nbOk=nbNeedFood=nbNeedHeal=0;
					}
					globalContainer.gfx.drawVertLine(globalContainer.gfx.getW()-128+i, 128+ 80 +64-nbNeedHeal-nbNeedFood-nbOk, nbOk, 0, 220, 0);
					globalContainer.gfx.drawVertLine(globalContainer.gfx.getW()-128+i, 128+ 80 +64-nbNeedHeal-nbNeedFood, nbNeedFood, 224, 210, 17);
					globalContainer.gfx.drawVertLine(globalContainer.gfx.getW()-128+i, 128+ 80 +64-nbNeedHeal, nbNeedHeal, 255, 0, 0);   

				}
			}

		}
	}

	if (typeToBuild>=0)
	{
		// we get the type of building
		int mapX, mapY;
		int batX, batY, batW, batH;
		int exMapX, exMapY; // ex suffix means EXtended building; the last level building type.
		int exBatX, exBatY, exBatW, exBatH;

		int typeNum=game.buildingsTypes.getTypeNum(typeToBuild, 0, false);

		// we check for room
		BuildingType *bt=game.buildingsTypes.buildingsTypes[typeNum];
		
		int tempX, tempY;
		if (bt->width&0x1)
			tempX=((mouseX)>>5)+viewportX;
		else
			tempX=((mouseX+16)>>5)+viewportX;
			
		if (bt->height&0x1)
			tempY=((mouseY)>>5)+viewportY;
		else
			tempY=((mouseY+16)>>5)+viewportY;
		
		
		bool isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, localTeam);
		
		// we find last's leve type num:
		BuildingType *lastbt=game.buildingsTypes.getBuildingType(typeNum);
		int lastTypeNum=typeNum;
		int max=0;
		while(lastbt->nextLevelTypeNum>=0)
		{
			lastTypeNum=lastbt->nextLevelTypeNum;
			lastbt=game.buildingsTypes.getBuildingType(lastTypeNum);
			if (max++>200)
			{
				printf("GameGUI: Error: nextLevelTypeNum architecture is broken.\n");
				assert(false);
				break;
			}
		}
		
		bool isExtendedRoom=game.checkHardRoomForBuilding(tempX, tempY, lastTypeNum, &exMapX, &exMapY, localTeam);

		// we get the datas
		Sprite *sprite=globalContainer.buildings.getSprite(bt->startImage);
		if (bt->hueImage)
			((PalSprite *)sprite)->setPal(&(game.teams[localTeam]->palette));
		
		batX=(mapX-viewportX)<<5;
		batY=(mapY-viewportY)<<5;
		batW=(bt->width)<<5;
		batH=(bt->height)<<5;
		
		// we get extended building sizes:
		
		exBatX=(exMapX-viewportX)<<5;
		exBatY=(exMapY-viewportY)<<5;
		exBatW=(lastbt->width)<<5;
		exBatH=(lastbt->height)<<5;
		
		
		globalContainer.gfx.setClipRect(0, 0, globalContainer.gfx.getW()-128, globalContainer.gfx.getH());
		globalContainer.gfx.drawSprite(sprite, batX, batY);

		if (isRoom)
		{
			if (isExtendedRoom)
				globalContainer.gfx.drawRect(exBatX-1, exBatY-1, exBatW+1, exBatH+1, 255, 255, 255, 127);
			else
				globalContainer.gfx.drawRect(exBatX-1, exBatY-1, exBatW+1, exBatH+1, 127, 0, 0, 127);
			globalContainer.gfx.drawRect(batX, batY, batW, batH, 255, 255, 255, 127);
		}
		else
			globalContainer.gfx.drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
		
	}
	else if (selBuild)
	{
		globalContainer.gfx.setClipRect(0, 0, globalContainer.gfx.getW()-128, globalContainer.gfx.getH());
		int centerX, centerY;
		game.map.buildingPosToCursor(selBuild->posX, selBuild->posY,  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
		if (selBuild->owner->teamNumber==localTeam)
			globalContainer.gfx.drawCircle(centerX, centerY, selBuild->type->width*16, 0, 0, 190);
		else if ((game.teams[localTeam]->allies) & (selBuild->owner->me))
			globalContainer.gfx.drawCircle(centerX, centerY, selBuild->type->width*16, 255, 196, 0);
		else if (!selBuild->type->isVirtual)
			globalContainer.gfx.drawCircle(centerX, centerY, selBuild->type->width*16, 190, 0, 0);
	}
}

void GameGUI::drawAll(int team)
{
	globalContainer.gfx.setClipRect(0, 0, globalContainer.gfx.getW()-128, globalContainer.gfx.getH());
	bool drawBuildingRects=(typeToBuild>=0);
	game.drawMap(0, 0, globalContainer.gfx.getW()-128, globalContainer.gfx.getH(),viewportX, viewportY, localTeam, drawHealthFoodBar, drawPathLines, drawBuildingRects, true);
	
	globalContainer.gfx.setClipRect(globalContainer.gfx.getW()-128, 0, 128, 128);
	game.drawMiniMap(globalContainer.gfx.getW()-128, 0, 128, 128, viewportX, viewportY, team);

	globalContainer.gfx.setClipRect(0, 0, globalContainer.gfx.getW(), globalContainer.gfx.getH());
	draw();
}

void GameGUI::executeOrder(Order *order)
{
	switch (order->getOrderType())
	{
		case ORDER_TEXT_MESSAGE :
		{
			MessageOrder *mo=(MessageOrder *)order;
			int sp=mo->sender;
			
			if (mo->recepientsMask &(1<<localPlayer))
			{
				Message message;
				message.showTicks=DEFAULT_MESSAGE_SHOW_TICKS;
				sprintf(message.text, "%s : %s", game.players[sp]->name, mo->getText());
				messagesList.push_front(message);
			}
			game.executeOrder(order);
		}
		break;
		case ORDER_QUITED :
		{
			if (order->sender==localPlayer)
				isRunning=false;
			
			game.executeOrder(order);
		}
		break;
		case ORDER_PLAYER_QUIT_GAME :
		{
			int qp=order->sender;
			Message message;
			message.showTicks=DEFAULT_MESSAGE_SHOW_TICKS;
			sprintf(message.text, "%s%s%s", globalContainer.texts.getString("[l has left the game]"), game.players[qp]->name, globalContainer.texts.getString("[r has left the game]"));
			message.text[MAX_MESSAGE_SIZE-1]=0;
			messagesList.push_front(message);
			
			game.executeOrder(order);
		}
		break;
		default:
		{
			game.executeOrder(order);
		}
	}
}
void GameGUI::load(SDL_RWops *stream)
{
	bool result=game.load(stream);
	if (result==false)
		printf("ENG : Critical : Wrong map format, signature missmatch\n");
}

void GameGUI::save(SDL_RWops *stream)
{
	game.save(stream);
}

void GameGUI::drawButton(int x, int y, const char *caption)
{
	globalContainer.gfx.drawFilledRect(x, y, 96, 16, 128, 128, 128);
	globalContainer.gfx.drawHorzLine(x, y, 96, 200, 200, 200);
	globalContainer.gfx.drawHorzLine(x, y+15, 96, 28, 28, 28);
	globalContainer.gfx.drawVertLine(x, y, 16, 200, 200, 200);
	globalContainer.gfx.drawVertLine(x+95, y, 16, 200, 200, 200);
	globalContainer.gfx.drawString(x+3, y+3, font, globalContainer.texts.getString(caption));
}

void GameGUI::drawTextCenter(int x, int y, const char *caption, int i)
{
	char *text;

	if (i==-1)
		text=globalContainer.texts.getString(caption);
	else
		text=globalContainer.texts.getString(caption, i);

	int dec=(128-font->getStringWidth(text))>>1;
	globalContainer.gfx.drawString(x+dec, y, font, text);
}

void GameGUI::drawScrollBox(int x, int y, int value, int valueLocal, int act, int max)
{
	globalContainer.gfx.drawFilledRect(x, y, 128, 16, 128, 128, 128);
	globalContainer.gfx.drawHorzLine(x, y, 128, 200, 200, 200);
	globalContainer.gfx.drawHorzLine(x, y+16, 128, 28, 28, 28);
	globalContainer.gfx.drawVertLine(x, y, 16, 200, 200, 200);
	globalContainer.gfx.drawVertLine(x+16, y, 16, 200, 200, 200);
	globalContainer.gfx.drawVertLine(x+16+96, y, 16, 200, 200, 200);
	globalContainer.gfx.drawVertLine(x+15, y, 16, 28, 28, 28);
	globalContainer.gfx.drawVertLine(x+16+95, y, 16, 28, 28, 28);
	globalContainer.gfx.drawVertLine(x+16+96+15, y, 16, 28, 28, 28);
	globalContainer.gfx.drawString(x+6, y+3, font, "-");
	globalContainer.gfx.drawString(x+96+16+6, y+3, font, "+");
	int size=(valueLocal*94)/max;
	globalContainer.gfx.drawFilledRect(x+16+1, y+1, size, 14, 100, 100, 200);
	size=(value*94)/max;
	globalContainer.gfx.drawFilledRect(x+16+1, y+3, size, 10, 28, 28, 200);
	size=(act*94)/max;
	globalContainer.gfx.drawFilledRect(x+16+1, y+5, size, 6, 28, 200, 28);
}

void GameGUI::checkValidSelection(void)
{
	if (displayMode==BUILDING_SELECTION_VIEW)
	{
		if (selectionUID<0)
		{
			int id=Building::UIDtoID(selectionUID);
			int team=Building::UIDtoTeam(selectionUID);
			
			selBuild=game.teams[team]->myBuildings[id];
		}
		else
			selBuild=NULL;
		game.selectedBuilding=selBuild;
		if (selBuild==NULL)
		{
			game.selectedUnit=NULL;
			game.selectedBuilding=NULL;
			displayMode=BUILDING_AND_FLAG;
			needRedraw=true;
		}
	}
	else if (displayMode==UNIT_SELECTION_VIEW)
	{
		if (selectionUID>=0)
		{
			int id=Unit::UIDtoID(selectionUID);
			int team=Unit::UIDtoTeam(selectionUID);
			selUnit=game.teams[team]->myUnits[id];
		}
		else
			selUnit=NULL;
		game.selectedUnit=selUnit;
		if (selUnit==NULL)
		{
			game.selectedUnit=NULL;
			game.selectedBuilding=NULL;
			displayMode=BUILDING_AND_FLAG;
			needRedraw=true;
		}
	}
	else
	{
		selBuild=NULL;
		selUnit=NULL;
		game.selectedUnit=NULL;
		game.selectedBuilding=NULL;
		//displayMode=BUILDING_AND_FLAG;
	}
}
