/*
  Copyright (C) 2001, 2002, 2003 Stephane Magnenat & Luc-Olivier de Charrière
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

#include "GameGUI.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "Utilities.h"
#include "YOG.h"
#include <stdio.h>
#include <stdarg.h>

#define TYPING_INPUT_BASE_INC 7
#define TYPING_INPUT_MAX_POS 46

InGameTextInput::InGameTextInput(GraphicContext *parentCtx)
:OverlayScreen(parentCtx, 492, 34)
{
	textInput=new TextInput(5, 5, 482, 24, globalContainer->standardFont, "", true, 256);
	addWidget(textInput);
}

void InGameTextInput::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==TEXT_VALIDATED)
	{
		endValue=0;
	}
}

GameGUI::GameGUI()
{
	init();
}

GameGUI::~GameGUI()
{

}

void GameGUI::init()
{
	int i;
	paused=false;
	isRunning=true;
	exitGlobCompletely=false;
	toLoadGameFileName[0]=0;
	needRedraw=true;
	drawHealthFoodBar=true;
	drawPathLines=false;
	viewportX=0;
	viewportY=0;
	mouseX=0;
	mouseY=0;
	displayMode=BUILDING_AND_FLAG;
	typeToBuild=-1;
	selBuild=NULL;
	selectionPushed=false;
	miniMapPushed=false;
	putMark=false;
	showUnitWorkingToBuilding=false;
	selectionUID=0;
	chatMask=0xFFFFFFFF;
	statMode=STAT_TEXT;

	for (i=0; i<9; i++)
	{
		viewportSpeedX[i]=0;
		viewportSpeedY[i]=0;
	}

	inGameMenu=IGM_NONE;
	gameMenuScreen=NULL;
	typingInputScreen=NULL;
	typingInputScreenPos=0;
	
	messagesList.clear();
	markList.clear();
	localTeam=NULL;
	teamStats=NULL;
}

void GameGUI::adjustInitialViewport()
{
	assert(localTeamNo>=0);
	assert(localTeamNo<32);
	assert(game.session.numberOfPlayer>0);
	assert(game.session.numberOfPlayer<32);
	assert(localTeamNo<game.session.numberOfPlayer);
	
	localTeam=game.teams[localTeamNo];
	assert(localTeam);
	teamStats=&localTeam->stats;
	
	viewportX=localTeam->startPosX-((globalContainer->gfx->getW()-128)>>6);
	viewportY=localTeam->startPosY-(globalContainer->gfx->getH()>>6);
	viewportX=(viewportX+game.map.getW())%game.map.getW();
	viewportY=(viewportY+game.map.getH())%game.map.getH();
}

void GameGUI::moveFlag(int mx, int my)
{
	int posX, posY;
	game.map.cursorToBuildingPos(mx, my, selBuild->type->width, selBuild->type->height, &posX, &posY, viewportX, viewportY);
	if ((selBuild->posXLocal!=posX)||(selBuild->posYLocal!=posY))
	{
		Sint32 UID=selBuild->UID;
		OrderMoveFlags *oms=new OrderMoveFlags(&UID, &posX, &posY, 1);
		// First, we check if anoter move of the same flag is already in the "orderQueue".
		bool found=false;
		for (std::list<Order *>::iterator it=orderQueue.begin(); it!=orderQueue.end(); ++it)
		{
			if ( ((*it)->getOrderType()==ORDER_MOVE_FLAG) && ( *((OrderMoveFlags *)(*it))->UID==UID) )
			{
				delete (*it);
				(*it)=oms;
				found=true;
				break;
			}
		}
		if (!found)
			orderQueue.push_back(oms);
		selBuild->posXLocal=posX;
		selBuild->posYLocal=posY;
	}
}

void GameGUI::flagSelectedStep(void)
{
	// update flag
	int mx, my;
	Uint8 button=SDL_GetMouseState(&mx, &my);
	if ((button&SDL_BUTTON(1)) && (mx<globalContainer->gfx->getW()-128))
	{
		if (selBuild && selectionPushed && (selBuild->type->isVirtual))
		{
			moveFlag(mx, my);
		}
	}
}

void GameGUI::step(void)
{
	SDL_Event event, mouseMotionEvent, windowEvent;
	bool wasMouseMotion=false;
	bool wasWindowEvent=false;
	// we get all pending events but for mousemotion we only keep the last one
	while (SDL_PollEvent(&event))
	{
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
			processEvent(&event);
		}
	}
	if (wasMouseMotion)
		processEvent(&mouseMotionEvent);
	if (wasWindowEvent)
		processEvent(&windowEvent);
	
	int oldViewportX=viewportX;
	int oldViewportY=viewportY;
	viewportX+=game.map.getW();
	viewportY+=game.map.getH();
	for (int i=0; i<9; i++)
	{
		viewportX+=viewportSpeedX[i];
		viewportY+=viewportSpeedY[i];
	}
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();
	
	if ((viewportX!=oldViewportX) || (viewportY!=oldViewportY))
		flagSelectedStep();
	
	assert(localTeam);
	if (localTeam->wasEvent(Team::UNIT_UNDER_ATTACK_EVENT))
	{
		Sint32 UID=localTeam->getEventId();
		int team=Unit::UIDtoTeam(UID);
		int id=Unit::UIDtoID(UID);
		Unit *u=game.teams[team]->myUnits[id];
		int strDec=(int)(u->typeNum);
		addMessage(200, 30, 30, "%s %s", globalContainer->texts.getString("[unit type]", strDec), globalContainer->texts.getString("[attacked(f)]"));
	}
	if (localTeam->wasEvent(Team::BUILDING_UNDER_ATTACK_EVENT))
	{
		Sint32 UID=localTeam->getEventId();
		int team=Building::UIDtoTeam(UID);
		int id=Building::UIDtoID(UID);
		Building *b=game.teams[team]->myBuildings[id];
		int strDec=b->type->type;
		addMessage(255, 0, 0, "%s %s", globalContainer->texts.getString("[building name]", strDec), globalContainer->texts.getString("[attacked]"));
	}
	if (localTeam->wasEvent(Team::BUILDING_FINISHED_EVENT))
	{
		Sint32 UID=localTeam->getEventId();
		int team=Building::UIDtoTeam(UID);
		int id=Building::UIDtoID(UID);
		Building *b=game.teams[team]->myBuildings[id];
		int strDec=b->type->type;
		addMessage(30, 255, 30, "%s %s",  globalContainer->texts.getString("[building name]", strDec), globalContainer->texts.getString("[finished]"));
	}
		
	// do a yog step
	yog->step();

	// display yog chat messages
	for (std::list<YOG::Message>::iterator m=yog->receivedMessages.begin(); m!=yog->receivedMessages.end(); ++m)
		if (!m->gameGuiPainted)
		{
			switch(m->messageType)//set the text color
			{
				case YCMT_MESSAGE:
					/* We don't want YOG messages to appear while in the game.
					addMessage(99, 143, 255, "<%s> %s", m->userName, m->text);*/
				break;
				case YCMT_PRIVATE_MESSAGE:
					addMessage(99, 255, 242, "<%s%s> %s", globalContainer->texts.getString("[from:]"), m->userName, m->text);
				break;
				case YCMT_ADMIN_MESSAGE:
					addMessage(138, 99, 255, "<%s> %s", m->userName, m->text);
				break;
				case YCMT_PRIVATE_RECEIPT:
					addMessage(99, 255, 242, "<%s%s> %s", globalContainer->texts.getString("[to:]"), m->userName, m->text);
				break;
				case YCMT_PRIVATE_RECEIPT_BUT_AWAY:
					addMessage(99, 255, 242, "<%s%s> %s", globalContainer->texts.getString("[away:]"), m->userName, m->text);
				break;
				case YCMT_EVENT_MESSAGE:
					addMessage(99, 143, 255, "%s", m->text);
				break;
				default:
					printf("m->messageType=%d\n", m->messageType);
					assert(false);
				break;
			}
			m->gameGuiPainted=true;
		}
}

void GameGUI::synchroneStep(void)
{
	assert(localTeam);
	assert(teamStats);
}

bool GameGUI::processGameMenu(SDL_Event *event)
{
	gameMenuScreen->translateAndProcessEvent(event);
	switch (inGameMenu)
	{
		case IGM_MAIN:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameMainScreen::LOAD_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_LOAD;
					gameMenuScreen=new LoadSaveScreen(".", "game");
					gameMenuScreen->dispatchPaint(gameMenuScreen->getSurface());
					return true;
				}
				break;
				case InGameMainScreen::SAVE_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_SAVE;
					gameMenuScreen=new LoadSaveScreen(".", "game", false, game.session.getMapName());
					gameMenuScreen->dispatchPaint(gameMenuScreen->getSurface());
					return true;
				}
				break;
				case InGameMainScreen::ALLIANCES:
				{
					delete gameMenuScreen;
					if (game.session.numberOfPlayer<=8)
					{
						inGameMenu=IGM_ALLIANCE8;
						gameMenuScreen=new InGameAlliance8Screen(this);
						gameMenuScreen->dispatchPaint(gameMenuScreen->getSurface());
					}
					else
					{
						inGameMenu=IGM_NONE;
					}
					return true;
				}
				break;
				case InGameMainScreen::OPTIONS:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_OPTION;
					gameMenuScreen=new InGameOptionScreen(this);
					gameMenuScreen->dispatchPaint(gameMenuScreen->getSurface());
					return true;
				}
				break;
				case InGameMainScreen::RETURN_GAME:
				{
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					return true;
				}
				break;
				case InGameMainScreen::QUIT_GAME:
				{
					orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					return true;
				}
				break;
				default:
				return false;
			}
		}

		case IGM_ALLIANCE8:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameAlliance8Screen::OK :
				{
					// mask are for players, we need to convert them to team.
					Uint32 playerAllianceMask=((InGameAlliance8Screen *)gameMenuScreen)->getAllianceMask();
					Uint32 teamAllianceMask=0;
					Uint32 playerVisionMask=((InGameAlliance8Screen *)gameMenuScreen)->getVisionMask();
					Uint32 teamVisionMask=0;
					int i;

					for (i=0; i<game.session.numberOfPlayer; i++)
					{
						int otherTeam=game.players[i]->teamNumber;
						if (playerAllianceMask&(1<<i))
						{
							// player is allied, ally team
							teamAllianceMask|=(1<<otherTeam);
						}
						if (playerVisionMask&(1<<i))
						{
							// player is shared vision, share vision with team
							teamVisionMask|=(1<<otherTeam);
						}
					}
					orderQueue.push_back(new SetAllianceOrder(localTeamNo, teamAllianceMask, teamVisionMask));
					chatMask=((InGameAlliance8Screen *)gameMenuScreen)->getChatMask();
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
				}
				return true;

				default:
				return false;
			}
		}

		case IGM_OPTION:
		{
			if (gameMenuScreen->endValue == InGameOptionScreen::OK)
			{
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				printf("!! NOT CODED YET !! Use option\n");
				return true;
			}
			else
			{
				return false;
			}
		}

		case IGM_LOAD:
		case IGM_SAVE:
		{
			switch (gameMenuScreen->endValue)
			{
				case LoadSaveScreen::OK:
				{
					const char *name=((LoadSaveScreen *)gameMenuScreen)->fileName;
					if (inGameMenu==IGM_LOAD)
					{
						strncpy(toLoadGameFileName, name, sizeof(toLoadGameFileName));
						toLoadGameFileName[sizeof(toLoadGameFileName)-1]=0;
						orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));
					}
					else
					{
						SDL_RWops *stream=globalContainer->fileManager->open(name,"wb");
						if (stream)
						{
							save(stream, name);
							SDL_RWclose(stream);
						}
						else
							printf("GGU : Can't save map\n");
					}
				}

				case LoadSaveScreen::CANCEL:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				return true;

				default:
				return false;
			}
		}

		default:
		return false;
	}
}

void GameGUI::processEvent(SDL_Event *event)
{
	// handle typing
	if (typingInputScreen)
	{
		//if (event->type==SDL_KEYDOWN)
		typingInputScreen->translateAndProcessEvent(event);

		if (typingInputScreen->endValue==0)
		{
			char message[256];
			strncpy(message, typingInputScreen->getText(), 256);
			if (message[0])
			{
				bool foundLocal=false;
				yog->handleMessageAliasing(message, 256);
				if (strncmp(message, "/m ", 3)==0)
				{
					for (int i=0; i<game.session.numberOfPlayer; i++)
						if (game.players[i])
						{
							char *name=game.players[i]->name;
							int l=Utilities::strnlen(name, BasePlayer::MAX_NAME_LENGTH);
							if ((strncmp(name, message+3, l)==0)&&(message[3+l]==' '))
							{
								orderQueue.push_back(new MessageOrder(game.players[i]->numberMask, MessageOrder::PRIVATE_MESSAGE_TYPE, message+4+l));
								foundLocal=true;
							}
						}
					if (!foundLocal)
						yog->sendMessage(message);
				}
				else if (message[0]=='/')
					yog->sendMessage(message);
				else
					orderQueue.push_back(new MessageOrder(chatMask, MessageOrder::NORMAL_MESSAGE_TYPE, message));
				typingInputScreen->setText("");
			}
			typingInputScreenInc=-TYPING_INPUT_BASE_INC;
			typingInputScreen->endValue=1;
			return;
		}
	}

	// if there is a menu he get events first
	if (inGameMenu)
	{
		processGameMenu(event);
	}
	else
	{
		if (event->type==SDL_KEYDOWN)
		{
			handleKey(event->key.keysym, true);
		}
		else if (event->type==SDL_KEYUP)
		{
			handleKey(event->key.keysym, false);
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
				if (event->button.x>globalContainer->gfx->getW()-128)
					handleMenuClick(event->button.x-globalContainer->gfx->getW()+128, event->button.y, event->button.button);
				else
					handleMapClick(event->button.x, event->button.y, event->button.button);
			}
			else if (button==4)
			{
				if ((selBuild) && (selBuild->owner->teamNumber==localTeamNo) && 
					(selBuild->type->maxUnitWorking) && (selBuild->buildingState==Building::ALIVE) &&
					(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
				{
					int nbReq=(selBuild->maxUnitWorkingLocal+=1);
					orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
			else if (button==5)
			{
				if ((selBuild) && (selBuild->owner->teamNumber==localTeamNo) && 
					(selBuild->type->maxUnitWorking) && (selBuild->buildingState==Building::ALIVE) &&
					(selBuild->maxUnitWorkingLocal>0))
				{
					int nbReq=(selBuild->maxUnitWorkingLocal-=1);
					orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
		}
		else if (event->type==SDL_MOUSEBUTTONUP)
		{
			if (event->button.button==SDL_BUTTON_LEFT)
				miniMapPushed=false,
			selectionPushed=false;
			showUnitWorkingToBuilding=false;
		}
	}
	
	if (event->type==SDL_MOUSEMOTION)
	{
		handleMouseMotion(event->motion.x, event->motion.y, event->motion.state);
	}
	else if (event->type==SDL_ACTIVEEVENT)
	{
		handleActivation(event->active.state, event->active.gain);
	}
	else if (event->type==SDL_QUIT)
	{
		exitGlobCompletely=true;
		orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));
		//isRunning=false;
	}
	else if (event->type==SDL_VIDEORESIZE)
	{
		int newW=event->resize.w;
		int newH=event->resize.h;
		newW&=(~(0x1F));
		newH&=(~(0x1F));
		if (newW<640)
			newW=640;
		if (newH<480)
			newH=480;
		printf("New size : %dx%d\n", newW, newH);
		globalContainer->gfx->setRes(newW, newH, 32, globalContainer->graphicFlags);
	}
}

void GameGUI::handleActivation(Uint8 state, Uint8 gain)
{
	if (gain==0)
	{
		viewportSpeedX[0]=viewportSpeedY[0]=0;
	}
}

void GameGUI::handleRightClick(void)
{
	// We deselect all, we want no tools activated:
	if ((displayMode==BUILDING_AND_FLAG) && (typeToBuild<0))
	{
		displayMode=STAT_VIEW;
	}
	else //(typeToBuild>=0)
	{
		displayMode=BUILDING_AND_FLAG;
		selBuild=NULL;
		selectionPushed=false;
		selUnit=NULL;
		selectionUID=0;
		typeToBuild=-1;
		needRedraw=true;
	}
}

void GameGUI::handleKey(SDL_keysym keySym, bool pressed)
{
	SDLKey key=keySym.sym;

	int modifier;

	if (pressed)
		modifier=1;
	else
		modifier=-1;

	if (!typingInputScreen)
	{
		switch (key)
		{
			case SDLK_ESCAPE:
				{
					if ((inGameMenu==IGM_NONE) && (!pressed))
					{
						gameMenuScreen=new InGameMainScreen();
						gameMenuScreen->dispatchPaint(gameMenuScreen->getSurface());
						inGameMenu=IGM_MAIN;
					}
				}
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
			case SDLK_PLUS:
			case SDLK_KP_PLUS:
			    {
					if ((pressed) && (selBuild) && (selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (displayMode==BUILDING_SELECTION_VIEW) && (selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
					{
						int nbReq=(selBuild->maxUnitWorkingLocal+=1);
						orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
					}
				}
				break;
			case SDLK_MINUS:
			case SDLK_KP_MINUS:
				{
					if ((pressed) && (selBuild) && (selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (displayMode==BUILDING_SELECTION_VIEW) && (selBuild->maxUnitWorkingLocal>0))
					{
						int nbReq=(selBuild->maxUnitWorkingLocal-=1);
						orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
					}
				}
				break;
			case SDLK_d:
				{
					if ((pressed) && selBuild && (selBuild->owner->teamNumber==localTeamNo))
					{
						orderQueue.push_back(new OrderDelete(selBuild->UID));
					}
				}
				break;
			case SDLK_u:
			case SDLK_a:
				{
					if ((pressed) && (selBuild) && (selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->nextLevelTypeNum!=-1) && (!selBuild->type->isBuildingSite))
					{
						orderQueue.push_back(new OrderConstruction(selBuild->UID));
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
			case SDLK_m :
				if (pressed)
					putMark=true;
				break;

			case SDLK_RETURN :
				if (pressed)
				{
					typingInputScreen=new InGameTextInput(globalContainer->gfx);
					typingInputScreen->dispatchPaint(typingInputScreen->getSurface());
					typingInputScreenInc=TYPING_INPUT_BASE_INC;
					typingInputScreenPos=0;
				}
				break;
			case SDLK_TAB:
				if (pressed)
					iterateSelection();
				break;
			case SDLK_SPACE:
				if (pressed)
				{
				    int evX, evY; 
				    int sw, sh;
					localTeam->getEventPos(&evX, &evY);
					sw=globalContainer->gfx->getW();
					sh=globalContainer->gfx->getH();
					viewportX=evX-((sw-128)>>6);
					viewportY=evY-(sh>>6);
				}
				break;
			case SDLK_w:
				if (pressed)
					paused=!paused;
				break;
			default:

			break;
		}
	}
}

void GameGUI::coordinateFromMxMY(int mx, int my, int *cx, int *cy, bool useviewport)
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
	if (useviewport)
	{
		*cx-=((globalContainer->gfx->getW()-128)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}
	*cx+=localTeam->startPosX+(game.map.getW()>>1);
	*cy+=localTeam->startPosY+(game.map.getH()>>1);
	
	*cx&=game.map.getMaskW();
	*cy&=game.map.getMaskH();
}

void GameGUI::handleMouseMotion(int mx, int my, int button)
{
	const int scrollZoneWidth=5;
	game.mouseX=mouseX=mx;
	game.mouseY=mouseY=my;

	if (miniMapPushed)
	{
		coordinateFromMxMY(mx-globalContainer->gfx->getW()+128, my, &viewportX, &viewportY);
	}
	else
	{
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
	}

	if (button&SDL_BUTTON(1))
		if (mx<globalContainer->gfx->getW()-128)
			if (selBuild && selectionPushed && (selBuild->type->isVirtual))
			{
				moveFlag(mx, my);
			}
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
	if (typeToBuild>=0)
	{
		// we get the type of building
		int mapX, mapY;

		int typeNum;

		// try to get the building site, if it doesn't exists, get the finished building (for flags)
		typeNum=globalContainer->buildingsTypes.getTypeNum(typeToBuild, 0, true);
		if (typeNum==-1)
		{
			typeNum=globalContainer->buildingsTypes.getTypeNum(typeToBuild, 0, false);
			assert(globalContainer->buildingsTypes.buildingsTypes[typeNum]->isVirtual);
		}
		assert (typeNum!=-1);

		BuildingType *bt=globalContainer->buildingsTypes.buildingsTypes[typeNum];

		int tempX, tempY;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		bool isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, localTeamNo);

		if (isRoom)
			orderQueue.push_back(new OrderCreate(localTeamNo, mapX, mapY, (BuildingType::BuildingTypeNumber)typeNum));
	}
	else
	{
		int mapX, mapY;
		Sint32 UID;
		game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
		UID=game.map.getUnit(mapX, mapY);
		// check for flag first
		for (std::list<Building *>::iterator virtualIt=localTeam->virtualBuildings.begin();
				virtualIt!=localTeam->virtualBuildings.end(); ++virtualIt)
			{
				Building *b=*virtualIt;
				if ((b->posX==mapX) && (b->posY==mapY))
				{
					displayMode=BUILDING_SELECTION_VIEW;
					game.selectedUnit=NULL;
					selectionPushed=true;
					selectionUID=b->UID;
					checkValidSelection();
					return;
				}
			}
		// then for unit
		if (game.mouseUnit)
		{
			selUnit=game.mouseUnit;
			selBuild=NULL;
			selectionPushed=true;
			// an unit is selected:
			displayMode=UNIT_SELECTION_VIEW;
			selectionUID=selUnit->UID;
			game.selectedUnit=selUnit;
			checkValidSelection();
		}
		else
		{
			// then for building
			if (UID!=NOUID)
			{
				if (UID<0)
				{
					int buildingTeam=Building::UIDtoTeam(UID);
					// we can select for view buildings that are in shared vision
					if ((game.map.isMapDiscovered(mapX, mapY, localTeam->sharedVision))
						&& ( (game.teams[buildingTeam]->allies&(1<<localTeamNo)) || game.map.isFOW(mapX, mapY, localTeam->sharedVision)))
					{
						displayMode=BUILDING_SELECTION_VIEW;
						game.selectedUnit=NULL;
						selectionPushed=true;
						selectionUID=UID;
						checkValidSelection();
						showUnitWorkingToBuilding=true;
					}
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
			//! look if there is a virtual building (flag) selected
		}
	}
}

void GameGUI::handleMenuClick(int mx, int my, int button)
{
	// handle minimap
	if (my<128)
	{
		if (putMark)
		{
			int markx, marky;
			coordinateFromMxMY(mx, my, &markx, &marky, false);
			orderQueue.push_back(new MapMarkOrder(localTeamNo, markx, marky));
			putMark = false;
		}
		else
		{
			miniMapPushed=true;
			coordinateFromMxMY(mx, my, &viewportX, &viewportY);
		}
	}
	else if (displayMode==BUILDING_AND_FLAG)
	{
		if (my<128+20)
		{
			handleRightClick();
			displayMode=STAT_VIEW;
			needRedraw=true;	
		}
		// NOTE : here 6 is 12 /2. 12 is the number of buildings in menu
		else if (my<128+20+6*48)
		{
			
			int xNum=mx>>6;
			int yNum=(my-128-20)/48;
			typeToBuild=yNum*2+xNum;
			needRedraw=true;
		}
	}
	else if (displayMode==STAT_VIEW)
	{
		// do nothing for now, it's stat
#		ifndef WIN32
			((int)statMode)++;
			if (((int)statMode)==NB_STAT_MODE)
				((int)statMode)=0;
#		else
			switch (statMode) {
			case STAT_TEXT :
				statMode = STAT_GRAPH;
				break;
			case STAT_GRAPH :
				statMode = NB_STAT_MODE;
				break;
			case NB_STAT_MODE :
				statMode = STAT_TEXT;
				break;
			}
#		endif
	}
	else if (displayMode==BUILDING_SELECTION_VIEW)
	{
		assert (selBuild);
		// TODO : handle this in a nice way
		if (selBuild->owner->teamNumber!=localTeamNo)
			return;
		if ((my>256+45+12) && (my<256+45+16+12)  && (selBuild->type->maxUnitWorking) && (selBuild->buildingState==Building::ALIVE))
		{
			int nbReq;
			if (mx<16)
			{
				if(selBuild->maxUnitWorkingLocal>0)
				{
					nbReq=(selBuild->maxUnitWorkingLocal-=1);
					orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
				}
			}
			else if (mx<128-16)
			{
				nbReq=selBuild->maxUnitWorkingLocal=((mx-16)*MAX_UNIT_WORKING)/94;
				orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
			}
			else
			{
				if(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)
				{
					nbReq=(selBuild->maxUnitWorkingLocal+=1);
					orderQueue.push_back(new OrderModifyBuildings(&(selBuild->UID), &(nbReq), 1));
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
					orderQueue.push_back(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
				}
			}
			else if (mx<128-16)
			{
				if (selBuild->type->type==BuildingType::EXPLORATION_FLAG)
					nbReq=selBuild->unitStayRangeLocal=((mx-16)*MAX_EXPLO_FLAG_RANGE)/94;
				else if (selBuild->type->type==BuildingType::WAR_FLAG)
					nbReq=selBuild->unitStayRangeLocal=((mx-16)*MAX_WAR_FLAG_RANGE)/94;
				else if (selBuild->type->type==BuildingType::CLEARING_FLAG)
					nbReq=selBuild->unitStayRangeLocal=((mx-16)*MAX_CLEARING_FLAG_RANGE)/94;
				else
					assert(false);
				orderQueue.push_back(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
			}
			else
			{
				// TODO : check in orderQueue to avoid useless orders.
				if (selBuild->type->type==BuildingType::EXPLORATION_FLAG)
				{
					if(selBuild->unitStayRangeLocal<MAX_EXPLO_FLAG_RANGE)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
					}
				}
				else if (selBuild->type->type==BuildingType::WAR_FLAG)
				{
					if(selBuild->unitStayRangeLocal<MAX_WAR_FLAG_RANGE)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
					}
				}
				else if (selBuild->type->type==BuildingType::CLEARING_FLAG)
				{
					if(selBuild->unitStayRangeLocal<MAX_CLEARING_FLAG_RANGE)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(new OrderModifyFlags(&(selBuild->UID), &(nbReq), 1));
					}
				}
				else
					assert(false);

			}
		}

		if (selBuild->type->unitProductionTime)
		{
			for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
			{
				if ((my>256+90+(i*20)+12)&&(my<256+90+(i*20)+16+12))
				{
					if (mx<16)
					{
						if (selBuild->ratioLocal[i]>0)
						{
							selBuild->ratioLocal[i]--;

							Sint32 rdyPtr[1][UnitType::NB_UNIT_TYPE];
							memcpy(rdyPtr, selBuild->ratioLocal, UnitType::NB_UNIT_TYPE*sizeof(Sint32));
							orderQueue.push_back(new OrderModifySwarms(&(selBuild->UID), rdyPtr, 1));
						}
					}
					else if (mx<128-16)
					{
						selBuild->ratioLocal[i]=((mx-16)*MAX_RATIO_RANGE)/94;

						Sint32 rdyPtr[1][UnitType::NB_UNIT_TYPE];
						memcpy(rdyPtr, selBuild->ratioLocal, UnitType::NB_UNIT_TYPE*sizeof(Sint32));
						orderQueue.push_back(new OrderModifySwarms(&(selBuild->UID), rdyPtr, 1));
					}
					else
					{
						if (selBuild->ratioLocal[i]<MAX_RATIO_RANGE)
						{
							selBuild->ratioLocal[i]++;

							Sint32 rdyPtr[1][UnitType::NB_UNIT_TYPE];
							memcpy(rdyPtr, selBuild->ratioLocal, UnitType::NB_UNIT_TYPE*sizeof(Sint32));
							orderQueue.push_back(new OrderModifySwarms(&(selBuild->UID), rdyPtr, 1));
						}
					}
					//printf("ratioLocal[%d]=%d\n", i, selBuild->ratioLocal[i]);
				}
			}
		}

		if ((my>256+172) && (my<256+172+16))
		{
			BuildingType *buildingType=selBuild->type;
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				assert(buildingType->nextLevelTypeNum!=-1);
				orderQueue.push_back(new OrderCancelConstruction(selBuild->UID));
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				assert(buildingType->nextLevelTypeNum!=-1);
				assert(buildingType->lastLevelTypeNum!=-1);
				orderQueue.push_back(new OrderCancelConstruction(selBuild->UID));
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
			{
				if (selBuild->hp<buildingType->hpMax)
				{
					// repair
					if (selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && (localTeam->maxBuildLevel()>=buildingType->level))
						orderQueue.push_back(new OrderConstruction(selBuild->UID));
				}
				else if (buildingType->nextLevelTypeNum!=-1)
				{
					// upgrade
					if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
						orderQueue.push_back(new OrderConstruction(selBuild->UID));
				}
			}
		}

		if ((my>256+172+16+8) && (my<256+172+16+8+16))
		{
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				orderQueue.push_back(new OrderCancelDelete(selBuild->UID));
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				orderQueue.push_back(new OrderDelete(selBuild->UID));
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
		orderQueue.pop_front();
		return order;
	}
}

void GameGUI::draw(void)
{
	checkValidSelection();
	{
		needRedraw=false;
		globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128);
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128, 0, 0, 0);

		if (displayMode==BUILDING_AND_FLAG)
		{
			int i;
			for (i=0; i<12; i++)
			{
				int typeNum=globalContainer->buildingsTypes.getTypeNum(i, 0, false);
				BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);
				int imgid=bt->startImage;
				int x=((i&0x1)*64)+globalContainer->gfx->getW()-128;
				int y=((i>>1)*48)+128+20;
				int decX=0;
				int decY=0;

				globalContainer->gfx->setClipRect(x+6, y+3, 52, 42);
				Sprite *buildingSprite=globalContainer->buildings;

				if (buildingSprite->getW(imgid)<=32)
					decX=-16;
				else if (buildingSprite->getW(imgid)>64)
					decX=20;
				if (buildingSprite->getH(imgid)<=32)
					decY=-8;
				else if (buildingSprite->getH(imgid)>64)
					decY=26;

				buildingSprite->setBaseColor(localTeam->colorR, localTeam->colorG, localTeam->colorB);
				globalContainer->gfx->drawSprite(x-decX, y-decY, buildingSprite, imgid);
			}

			globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128);
			if (typeToBuild>=0)
			{
				int x=((typeToBuild&0x1)*64)+globalContainer->gfx->getW()-128;
				int y=((typeToBuild>>1)*48)+128+20;
				globalContainer->gfx->drawRect(x+6, y+3, 52, 42, 255, 0, 0);
				globalContainer->gfx->drawRect(x+5, y+2, 54, 44, 255, 0, 0);
			}

			char buttonText[64];
			int viewFu=teamStats->getFreeUnits(UnitType::WORKER)-teamStats->getUnitsNeeded();
			if (viewFu<-1)
				snprintf(buttonText, 64, "%s%d%s", globalContainer->texts.getString("[l units needed]"), -viewFu, globalContainer->texts.getString("[r units needed]"));
			else if (viewFu==-1)
				snprintf(buttonText, 64, "%s", globalContainer->texts.getString("[one unit needed]"));
			else if (viewFu==0)
				snprintf(buttonText, 64, "%s", globalContainer->texts.getString("[no unit needed]"));
			//else if (viewFu==0)
			//	snprintf(buttonText, 64, "%s", globalContainer->texts.getString("[no unit free]"));
			else if (viewFu==1)
				snprintf(buttonText, 64, "%s", globalContainer->texts.getString("[one unit free]"));
			else
				snprintf(buttonText, 64, "%s%d%s", globalContainer->texts.getString("[l units free]"), viewFu, globalContainer->texts.getString("[r units free]"));
			
			// draw button, for stat
			drawButton(globalContainer->gfx->getW()-128+12, 128+4, buttonText, false);

			// draw building infos
			if (mouseX>globalContainer->gfx->getW()-128)
			{
				if ((mouseY>128+20) && (mouseY<128+20+6*48))
				{
					int xNum=(mouseX-globalContainer->gfx->getW()+128)>>6;
					int yNum=(mouseY-128-20)/48;
					int typeId=yNum*2+xNum;
					drawTextCenter(globalContainer->gfx->getW()-128, 128+22+6*48, "[building name]", typeId);
					int typeNum=globalContainer->buildingsTypes.getTypeNum(typeId, 0, true);
					if (typeNum!=-1)
					{
						BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 128+36+6*48, globalContainer->littleFont, 
							"%s: %d", globalContainer->texts.getString("[wood]"), bt->maxRessource[0]);								
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, 128+36+6*48, globalContainer->littleFont, 
							"%s: %d", globalContainer->texts.getString("[corn]"), bt->maxRessource[1]);
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 128+48+6*48, globalContainer->littleFont, 
							"%s: %d", globalContainer->texts.getString("[stone]"), bt->maxRessource[2]);
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, 128+48+6*48, globalContainer->littleFont,
							"%s: %d", globalContainer->texts.getString("[Alga]"), bt->maxRessource[3]);
					}
				}
			}
		}
		else if (displayMode==BUILDING_SELECTION_VIEW)
		{
			assert(selBuild);

			// building icon
			globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, 128);
			Sprite *buildingSprite=globalContainer->buildings;
			buildingSprite->setBaseColor(game.teams[selBuild->owner->teamNumber]->colorR, game.teams[selBuild->owner->teamNumber]->colorG, game.teams[selBuild->owner->teamNumber]->colorB);
			BuildingType *buildingType=selBuild->type;
			globalContainer->gfx->drawSprite(
				globalContainer->gfx->getW()-128+64-buildingType->width*16,
				128+64-buildingType->height*16,
				buildingSprite, buildingType->startImage);

			// building text
			drawTextCenter(globalContainer->gfx->getW()-128, 128+8, "[building name]", buildingType->type);
			if (buildingType->isBuildingSite)
				drawTextCenter(globalContainer->gfx->getW()-128, 128+64+24, "[building site]");
			// FIXME : find a clean way here
			char *textT=globalContainer->texts.getString("[level]");
			int decT=(128-globalContainer->littleFont->getStringWidth(textT)-globalContainer->littleFont->getStringWidth(" : ")-globalContainer->littleFont->getStringWidth(buildingType->level+1))>>1;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+decT, 128+96+8, globalContainer->littleFont, "%s : %d", textT, buildingType->level+1);


			// building Infos
			globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128);

			if (buildingType->hpMax)
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+2, globalContainer->littleFont, "%s : %d/%d", globalContainer->texts.getString("[hp]"), selBuild->hp, buildingType->hpMax);
			if (buildingType->armor)
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+12, globalContainer->littleFont, "%s : %d", globalContainer->texts.getString("[armor]"), buildingType->armor);
			if (buildingType->shootDamage)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+22, globalContainer->littleFont, "%s : %d", globalContainer->texts.getString("[damage]"), buildingType->shootDamage);
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+32, globalContainer->littleFont, "%s : %d", globalContainer->texts.getString("[range]"), buildingType->shootingRange);
			}
			if ((selBuild->owner->allies) &(1<<localTeamNo))
			{
				if (buildingType->maxUnitWorking)
				{
					if (selBuild->buildingState==Building::ALIVE)
					{
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+33+10, globalContainer->littleFont, "%s : %d/%d", globalContainer->texts.getString("[working]"), selBuild->unitsWorking.size(), selBuild->maxUnitWorking);
						drawScrollBox(globalContainer->gfx->getW()-128, 256+45+12, selBuild->maxUnitWorking, selBuild->maxUnitWorkingLocal, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
					}
					else
					{
						if (selBuild->unitsWorking.size()>1)
						{
							globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+33+10, globalContainer->littleFont, "%s%d%s",
								globalContainer->texts.getString("[still (w)]"),
								selBuild->unitsWorking.size(),
								globalContainer->texts.getString("[units working]"));
						}
						else if (selBuild->unitsWorking.size()==1)
						{
							globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+33+10, globalContainer->littleFont, 
								"%s", globalContainer->texts.getString("[still one unit working]") );
						}
					}
				}

				if (buildingType->unitProductionTime)
				{
					int Left=(selBuild->productionTimeout*128)/buildingType->unitProductionTime;
					int Elapsed=128-Left;
					globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 256+65+12, Elapsed, 7, 100, 100, 255);
					globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128+Elapsed, 256+65+12, Left, 7, 128, 128, 128);

					for (int i=0; i<UnitType::NB_UNIT_TYPE; i++)
					{
						drawScrollBox(globalContainer->gfx->getW()-128, 256+90+(i*20)+12, selBuild->ratio[i], selBuild->ratioLocal[i], 0, MAX_RATIO_RANGE);
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+24, 256+93+(i*20)+12, globalContainer->littleFont, "%s", globalContainer->texts.getString("[unit type]", i));
					}
				}

				if (buildingType->maxUnitInside)
				{
					if (selBuild->buildingState==Building::ALIVE)
					{
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+62+12, globalContainer->littleFont, "%s : %d/%d", globalContainer->texts.getString("[inside]"), selBuild->unitsInside.size(), selBuild->maxUnitInside);
					}
					else
					{
						if (selBuild->unitsInside.size()>1)
						{
							globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+62+12, globalContainer->littleFont, "%s%d%s",
								globalContainer->texts.getString("[still (i)]"),
								selBuild->unitsInside.size(),
								globalContainer->texts.getString("[units inside]"));
						}
						else if (selBuild->unitsInside.size()==1)
						{
							globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+62+12, globalContainer->littleFont, "%s",
								globalContainer->texts.getString("[still one unit inside]") );
						}
					}
				}
				for (int i=0; i<NB_RESSOURCES; i++)
					if (buildingType->maxRessource[i])
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+64+(i*10)+12, globalContainer->littleFont, "%s : %d/%d", globalContainer->texts.getString("[ressources]", i), selBuild->ressources[i], buildingType->maxRessource[i]);

				// it is a unit ranged attractor (aka flag)
				if (buildingType->defaultUnitStayRange)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+132, globalContainer->littleFont, "%s : %d", globalContainer->texts.getString("[range]"), selBuild->unitStayRange);

					// get flag stat
					int goingTo, onSpot;
					selBuild->computeFlagStat(&goingTo, &onSpot);
					// display flag stat
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 256+80, globalContainer->littleFont, "%d %s", goingTo, globalContainer->texts.getString("[in way]"));
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 256+92, globalContainer->littleFont, "%d %s", onSpot, globalContainer->texts.getString("[on the spot]"));

					// display range box
					if (buildingType->type==BuildingType::EXPLORATION_FLAG)
						drawScrollBox(globalContainer->gfx->getW()-128, 256+144, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, MAX_EXPLO_FLAG_RANGE);
					else if (buildingType->type==BuildingType::WAR_FLAG)
						drawScrollBox(globalContainer->gfx->getW()-128, 256+144, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, MAX_WAR_FLAG_RANGE);
					else if (buildingType->type==BuildingType::CLEARING_FLAG)
						drawScrollBox(globalContainer->gfx->getW()-128, 256+144, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, MAX_CLEARING_FLAG_RANGE);
					else
						assert(false);
				}
				
				// repair and upgrade
				if (selBuild->constructionResultState==Building::REPAIR)
				{
					assert(buildingType->nextLevelTypeNum!=-1);
					drawBlueButton(globalContainer->gfx->getW()-128+12, 256+172, "[cancel repair]");
				}
				else if (selBuild->constructionResultState==Building::UPGRADE)
				{
					assert(buildingType->nextLevelTypeNum!=-1);
					assert(buildingType->lastLevelTypeNum!=-1);
					drawBlueButton(globalContainer->gfx->getW()-128+12, 256+172, "[cancel upgrade]");
				}
				else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
				{
					if (selBuild->hp<buildingType->hpMax)
					{
						// repair
						if (selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && (localTeam->maxBuildLevel()>=buildingType->level))
							drawBlueButton(globalContainer->gfx->getW()-128+12, 256+172, "[repair]");
					}
					else if (buildingType->nextLevelTypeNum!=-1)
					{
						// upgrade
						if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
						{
							drawBlueButton(globalContainer->gfx->getW()-128+12, 256+172, "[upgrade]");
							if ( mouseX>globalContainer->gfx->getW()-128+12 && mouseX<globalContainer->gfx->getW()-12
								&& mouseY>256+172 && mouseY<256+172+16 )
								{
									globalContainer->littleFont->pushColor(200, 200, 255);

									// We draw the ressources cost.
									int typeNum=buildingType->nextLevelTypeNum;
									BuildingType *bt=globalContainer->buildingsTypes.getBuildingType(typeNum);
									globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+172-30, globalContainer->littleFont, 
										"%s: %d", globalContainer->texts.getString("[wood]"), bt->maxRessource[0]);
									globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, 256+172-30, globalContainer->littleFont, 
										"%s: %d", globalContainer->texts.getString("[corn]"), bt->maxRessource[1]);
									globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+172-18, globalContainer->littleFont, 
										"%s: %d", globalContainer->texts.getString("[stone]"), bt->maxRessource[2]);
									globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, 256+172-18, globalContainer->littleFont,
										"%s: %d", globalContainer->texts.getString("[Alga]"), bt->maxRessource[3]);

									// We draw the new abilities:
									bt=globalContainer->buildingsTypes.getBuildingType(bt->nextLevelTypeNum);
									if (bt->hpMax)
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+96, 256+2, globalContainer->littleFont, "%d", bt->hpMax);

									if (bt->armor)
									{
										if (!buildingType->armor)
											globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 256+12, globalContainer->littleFont, "%s", globalContainer->texts.getString("[armor]"));
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+96, 256+12, globalContainer->littleFont, "%d", bt->armor);
									}
									if (bt->shootDamage)
									{
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+96, 256+22, globalContainer->littleFont, "%d", bt->shootDamage);
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+96, 256+32, globalContainer->littleFont, "%d", bt->shootingRange);
									}
									if (bt->maxUnitInside)
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+96, 256+62+12, globalContainer->littleFont, "%d", bt->maxUnitInside);

									if (buildingType->maxRessource[CORN])
										globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+96, 256+64+(CORN*10)+12, globalContainer->littleFont, "%d", bt->maxRessource[CORN]);

									globalContainer->littleFont->popColor();
								}
						}
					}
				}
				
				// building destruction
				if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
				{
					drawRedButton(globalContainer->gfx->getW()-128+12, 256+172+16+8, "[cancel destroy]");
				}
				else if (selBuild->buildingState==Building::ALIVE)
				{
					drawRedButton(globalContainer->gfx->getW()-128+12, 256+172+16+8, "[destroy]");
				}
				//globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, 470, globalContainer->littleFont, "UID%d;bs%d;ws%d;is%d", selBuild->UID, selBuild->buildingState, selBuild->unitsWorkingSubscribe.size(), selBuild->unitsInsideSubscribe.size());
			}
		}
		else if (displayMode==UNIT_SELECTION_VIEW)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+4, globalContainer->littleFont, "%s", globalContainer->texts.getString("[unit type]", selUnit->typeNum));
			
			/*
			const char *teamOfUnitName = 
			me = 0, 0, 190
			allied = 255, 196, 0
			enemy = 190, 0, 0
			*/
			
			Uint8 r, g, b;

			if (selUnit->hp<=selUnit->trigHP)
				{ r=255; g=0; b=0; }
			else
				{ r=0; g=255; b=0; }

			globalContainer->littleFont->pushColor(r, g, b);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+20, globalContainer->littleFont, "%s : %d", globalContainer->texts.getString("[hp]"), selUnit->hp);
			globalContainer->littleFont->popColor();

			if (selUnit->isUnitHungry())
				{ r=255; g=0; b=0; }
			else
				{ r=0; g=255; b=0; }

			globalContainer->littleFont->pushColor(r, g, b);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+36, globalContainer->littleFont, "%s : %2.0f %%", globalContainer->texts.getString("[food left]"), ((float)selUnit->hungry*100.0f)/(float)Unit::HUNGRY_MAX);
			globalContainer->littleFont->popColor();

			if (selUnit->performance[HARVEST])
			{
				if (selUnit->caryedRessource>=0)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+64, globalContainer->littleFont, "%s", globalContainer->texts.getString("[carry]"));
					globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32-8, 128+56, globalContainer->ressources, (selUnit->caryedRessource*10)+9);
				}
				else
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+64, globalContainer->littleFont, "%s", globalContainer->texts.getString("[don't carry anything]"));
				}
			}
			
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+96, globalContainer->littleFont, "%s : %d", globalContainer->texts.getString("[current speed]"), selUnit->speed);
			
			if (selUnit->typeNum!=UnitType::EXPLORER)
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+168, globalContainer->littleFont, "%s:", globalContainer->texts.getString("[levels]"), selUnit->speed);
			
			if (selUnit->performance[WALK])
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 132+184, globalContainer->littleFont, "%s (%d) : %d", globalContainer->texts.getString("[Walk]"), 1+selUnit->level[WALK], selUnit->performance[WALK]);
			if (selUnit->performance[SWIM])
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 132+196, globalContainer->littleFont, "%s (%d) : %d", globalContainer->texts.getString("[Swim]"), 1+selUnit->level[SWIM], selUnit->performance[SWIM]);
			if (selUnit->performance[BUILD])
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 132+208, globalContainer->littleFont, "%s (%d) : %d", globalContainer->texts.getString("[Build]"), 1+selUnit->level[BUILD], selUnit->performance[BUILD]);
			if (selUnit->performance[HARVEST])
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 132+220, globalContainer->littleFont, "%s (%d) : %d", globalContainer->texts.getString("[Harvest]"), 1+selUnit->level[HARVEST], selUnit->performance[HARVEST]);
			if (selUnit->performance[ATTACK_SPEED])
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 132+232, globalContainer->littleFont, "%s (%d) : %d", globalContainer->texts.getString("[At. speed]"), 1+selUnit->level[ATTACK_SPEED], selUnit->performance[ATTACK_SPEED]);
			if (selUnit->performance[ATTACK_STRENGTH])
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 132+244, globalContainer->littleFont, "%s (%d) : %d", globalContainer->texts.getString("[At. strength]"), 1+selUnit->level[ATTACK_STRENGTH], selUnit->performance[ATTACK_STRENGTH]);

			/* debug code:
			Sint32 UID=selUnit->UID;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+  0, globalContainer->littleFont, "hp=%d", selUnit->hp);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+ 15, globalContainer->littleFont, "UID=%d", UID);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+ 30, globalContainer->littleFont, "id=%d", Unit::UIDtoID(UID));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+ 45, globalContainer->littleFont, "Team=%d", Unit::UIDtoTeam(UID));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+ 60, globalContainer->littleFont, "medical=%d", selUnit->medical);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+ 75, globalContainer->littleFont, "activity=%d", selUnit->activity);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+ 90, globalContainer->littleFont, "displacement=%d", selUnit->displacement);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+105, globalContainer->littleFont, "movement=%d", selUnit->movement);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+120, globalContainer->littleFont, "action=%d", selUnit->action);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+135, globalContainer->littleFont, "pox=(%d;%d)", selUnit->posX, selUnit->posY);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+150, globalContainer->littleFont, "d=(%d;%d)", selUnit->dx, selUnit->dy);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+165, globalContainer->littleFont, "direction=%d", selUnit->direction);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+180, globalContainer->littleFont, "target=(%d;%d)", selUnit->targetX, selUnit->targetY);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+195, globalContainer->littleFont, "tempTarget=(%d;%d)", selUnit->tempTargetX, selUnit->tempTargetY);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+210, globalContainer->littleFont, "obstacle=(%d;%d)", selUnit->obstacleX, selUnit->obstacleY);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+225, globalContainer->littleFont, "border=(%d;%d)", selUnit->borderX, selUnit->borderY);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+240, globalContainer->littleFont, "bypassDirection=%d", selUnit->bypassDirection);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+255, globalContainer->littleFont, "ab=%x", selUnit->attachedBuilding);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+270, globalContainer->littleFont, "speed=%d", selUnit->speed);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+285, globalContainer->littleFont, "verbose=%d", selUnit->verbose);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+300, globalContainer->littleFont, "subscribed=%d", selUnit->subscribed);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, 128+315, globalContainer->littleFont, "ndToRckMed=%d", selUnit->needToRecheckMedical);
			*/
		}
		else if (displayMode==STAT_VIEW)
		{
			if (statMode==STAT_TEXT)
				teamStats->drawText();
			else
				teamStats->drawStat();
		}
	}
}

void GameGUI::drawOverlayInfos(void)
{
	if (typeToBuild>=0)
	{
		// we get the type of building
		int mapX, mapY;
		int batX, batY, batW, batH;
		int exMapX, exMapY; // ex suffix means EXtended building; the last level building type.
		int exBatX, exBatY, exBatW, exBatH;
		int tempX, tempY;
		bool isRoom, isExtendedRoom;

		int typeNum=globalContainer->buildingsTypes.getTypeNum(typeToBuild, 0, false);

		// we check for room
		BuildingType *bt=globalContainer->buildingsTypes.buildingsTypes[typeNum];


		if (bt->width&0x1)
			tempX=((mouseX)>>5)+viewportX;
		else
			tempX=((mouseX+16)>>5)+viewportX;

		if (bt->height&0x1)
			tempY=((mouseY)>>5)+viewportY;
		else
			tempY=((mouseY+16)>>5)+viewportY;

		isRoom=game.checkRoomForBuilding(tempX, tempY, typeNum, &mapX, &mapY, localTeamNo);

		// we find last's leve type num:
		BuildingType *lastbt=globalContainer->buildingsTypes.getBuildingType(typeNum);
		int lastTypeNum=typeNum;
		int max=0;
		while(lastbt->nextLevelTypeNum>=0)
		{
			lastTypeNum=lastbt->nextLevelTypeNum;
			lastbt=globalContainer->buildingsTypes.getBuildingType(lastTypeNum);
			if (max++>200)
			{
				printf("GameGUI: Error: nextLevelTypeNum architecture is broken.\n");
				assert(false);
				break;
			}
		}

		// we check room for extension
		if (bt->isVirtual)
			isExtendedRoom=true;
		else
			isExtendedRoom=game.checkHardRoomForBuilding(tempX, tempY, lastTypeNum, &exMapX, &exMapY, localTeamNo);

		// we get the datas
		Sprite *sprite=globalContainer->buildings;
		sprite->setBaseColor(localTeam->colorR, localTeam->colorG, localTeam->colorB);

		batX=(mapX-viewportX)<<5;
		batY=(mapY-viewportY)<<5;
		batW=(bt->width)<<5;
		batH=(bt->height)<<5;

		// we get extended building sizes:

		exBatX=(exMapX-viewportX)<<5;
		exBatY=(exMapY-viewportY)<<5;
		exBatW=(lastbt->width)<<5;
		exBatH=(lastbt->height)<<5;

		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->startImage);

		if (isRoom)
		{
			globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 127);
		}
		else
			globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
		if (isRoom&&isExtendedRoom)
			globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+1, exBatH+1, 255, 255, 255, 127);
		else
			globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+1, exBatH+1, 127, 0, 0, 127);

	}
	else if (selBuild)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		int centerX, centerY;
		game.map.buildingPosToCursor(selBuild->posXLocal, selBuild->posYLocal,  selBuild->type->width, selBuild->type->height, &centerX, &centerY, viewportX, viewportY);
		if (selBuild->owner->teamNumber==localTeamNo)
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 0, 0, 190);
		else if ((localTeam->allies) & (selBuild->owner->me))
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 255, 196, 0);
		else if (!selBuild->type->isVirtual)
			globalContainer->gfx->drawCircle(centerX, centerY, selBuild->type->width*16, 190, 0, 0);

		// draw a white circle around units that are working at building
		if ((showUnitWorkingToBuilding)
			&& ((selBuild->owner->allies) &(1<<localTeamNo)))
		{
			for (std::list<Unit *>::iterator unitsWorkingIt=selBuild->unitsWorking.begin(); unitsWorkingIt!=selBuild->unitsWorking.end(); ++unitsWorkingIt)
			{
				Unit *unit=*unitsWorkingIt;
				int px, py;
				game.map.mapCaseToDisplayable(unit->posX, unit->posY, &px, &py, viewportX, viewportY);
				int deltaLeft=255-unit->delta;
				if (unit->action<BUILD)
				{
					px-=(unit->dx*deltaLeft)>>3;
					py-=(unit->dy*deltaLeft)>>3;
				}
				globalContainer->gfx->drawCircle(px+16, py+16, 16, 255, 255, 255, 180);
			}
		}
	}
	if (localTeam->isAlive==false)
	{
		// TODO : draw won screen
		globalContainer->gfx->drawString(20, globalContainer->gfx->getH()>>1, globalContainer->littleFont, "%s", globalContainer->texts.getString("[you have lost]"));
	}
	else if (localTeam->hasWon==true)
	{
		// TODO : draw lost screen
		globalContainer->gfx->drawString(20, globalContainer->gfx->getH()>>1, globalContainer->littleFont, "%s", globalContainer->texts.getString("[you have won]"));
	}

	// draw message List
	// FIXME : shift this into a menu
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

		globalContainer->gfx->drawFilledRect(32, 32, globalContainer->gfx->getW()-128-64, 22+nbap*20, 0, 0, 140, 127);
		globalContainer->gfx->drawRect(32, 32, globalContainer->gfx->getW()-128-64, 22+nbap*20, 255, 255, 255);
		pm=1;
		int pnb=0;
		for(int pi2=0; pi2<game.session.numberOfPlayer; pi2++)
		{
			if (pm&apm)
			{

				globalContainer->gfx->drawString(48, 48+pnb*20, globalContainer->standardFont,"%s%d%s", globalContainer->texts.getString("[l waiting for player]"), pi2, globalContainer->texts.getString("[r waiting for player]"));
				pnb++;
			}
			pm=pm<<1;
		}
	}
	else
	{
		int ymesg=32;
		
		// show script text
		if (game.script.isTextShown)
			globalContainer->gfx->drawString(32, ymesg, globalContainer->standardFont, "%s", game.script.textShown.c_str());
		
		// show script counter
		if (game.script.getMainTimer())
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-165, ymesg, globalContainer->standardFont, "%d", game.script.getMainTimer());
		
		// if either script text or script timer has been shown, increment line count
		if (game.script.isTextShown || game.script.getMainTimer())
			ymesg+=32;
		
		// display messages
		for (std::list <Message>::iterator it=messagesList.begin(); it!=messagesList.end();)
		{
			globalContainer->standardFont->pushColor(it->r, it->g, it->b, it->a);
			globalContainer->standardFont->pushStyle(Font::STYLE_BOLD);
			globalContainer->gfx->drawString(32, ymesg, globalContainer->standardFont, "%s", it->text);
			globalContainer->standardFont->popStyle();
			globalContainer->standardFont->popColor();
			ymesg+=20;
			
			// delete old messages
			if (!(--(it->showTicks)))
			{
				it=messagesList.erase(it);
			}
			else
			{
				++it;
			}
			
		}
		
		// display map mark
		globalContainer->gfx->setClipRect();
		for (std::list <Mark>::iterator it=markList.begin(); it!=markList.end();)
		{
			
			int ray = Mark::DEFAULT_MARK_SHOW_TICKS-it->showTicks;
			Uint8 a;
			
			/*if (ray < (Mark::DEFAULT_MARK_SHOW_TICKS>>1))
				a = DrawableSurface::ALPHA_OPAQUE;
			else
			{*/
				//float coef = (float)(it->showTicks)/(float)(Mark::DEFAULT_MARK_SHOW_TICKS);//>>1);
				a = (it->showTicks*DrawableSurface::ALPHA_OPAQUE)/(Mark::DEFAULT_MARK_SHOW_TICKS);
			//}
			
			int mMax;
			int szX, szY;
			int decX, decY;
			int x, y;
			
			// FIXME : if needed, move this into a function like coordinateFromMxMY,
			// copy - pasted from Game.drawMiniMap
			Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);
			
			x = it->x;
			y = it->y;
			x = x - localTeam->startPosX + (game.map.getW()>>1);
			y = y - localTeam->startPosY + (game.map.getH()>>1);
			x &= game.map.getMaskW();
			y &= game.map.getMaskH();
			x = (x*100)/mMax;
			y = (y*100)/mMax;
			x += globalContainer->gfx->getW()-128+14+decX;
			y += 14+decY;
			
			a = (it->showTicks*DrawableSurface::ALPHA_OPAQUE)/(Mark::DEFAULT_MARK_SHOW_TICKS);
			globalContainer->gfx->drawCircle(x, y, ray, it->r, it->g, it->b, a);
			globalContainer->gfx->drawCircle(x, y, (ray*11)/8, it->r, it->g, it->b, a);
			
			// delete old marks
			if (!(--(it->showTicks)))
			{
				it=markList.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
	
	// info bar
	globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 20, 0, 0, 0, 127);
	
	Uint8 redC[]={200, 0, 0};
	Uint8 greenC[]={0, 200, 0};
	Uint8 whiteC[]={200, 200, 200};
	Uint8 actC[3];
	int free, tot, i;
	
	int dec=(globalContainer->gfx->getW()-640)>>2;
	dec += 32;
	
	for (i=0; i<3; i++)
	{
		free = teamStats->getFreeUnits(UnitType::TypeNum(i));
		// worker is a special case
		if (i==0)
			free -= teamStats->getUnitsNeeded();
		tot = teamStats->getTotalUnits(UnitType::TypeNum(i));
		if (free<0)
			memcpy(actC, redC, sizeof(redC));
		else if (free>0)
			memcpy(actC, greenC, sizeof(greenC));
		else
			memcpy(actC, whiteC, sizeof(whiteC));
		
		globalContainer->gfx->drawSprite(dec+2, 2, globalContainer->unitmini, i);
		globalContainer->littleFont->pushColor(actC[0], actC[1], actC[2]);
		globalContainer->gfx->drawString(dec+22, 3, globalContainer->littleFont, "%d / %d", free, tot);
		globalContainer->littleFont->popColor();
		
		dec += 148;
	}
}

void GameGUI::drawInGameMenu(void)
{
	globalContainer->gfx->drawSurface(gameMenuScreen->decX, gameMenuScreen->decY, gameMenuScreen->getSurface());
}

void GameGUI::drawInGameTextInput(void)
{
	typingInputScreen->decX=(globalContainer->gfx->getW()-128-492)/2;
	typingInputScreen->decY=globalContainer->gfx->getH()-typingInputScreenPos;
	globalContainer->gfx->drawSurface(typingInputScreen->decX, typingInputScreen->decY, typingInputScreen->getSurface());
	if (typingInputScreenInc>0)
		if (typingInputScreenPos<TYPING_INPUT_MAX_POS-TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			typingInputScreenPos=TYPING_INPUT_MAX_POS;
		}
	else if (typingInputScreenInc<0)
		if (typingInputScreenPos>TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			delete typingInputScreen;
			typingInputScreen=NULL;
		}
}


void GameGUI::drawAll(int team)
{
	globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
	bool drawBuildingRects=(typeToBuild>=0);
	game.drawMap(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH(),viewportX, viewportY, localTeamNo, drawHealthFoodBar, drawPathLines, drawBuildingRects, false);

	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 0, 128, 128);
	game.drawMiniMap(globalContainer->gfx->getW()-128, 0, 128, 128, viewportX, viewportY, team);

	globalContainer->gfx->setClipRect();
	draw();

	globalContainer->gfx->setClipRect();
	drawOverlayInfos();

	if (inGameMenu)
	{
		globalContainer->gfx->setClipRect();
		drawInGameMenu();
	}

	if (typingInputScreen)
	{
		globalContainer->gfx->setClipRect();
		drawInGameTextInput();
	}
}

void GameGUI::executeOrder(Order *order)
{
	switch (order->getOrderType())
	{
		case ORDER_TEXT_MESSAGE :
		{
			MessageOrder *mo=(MessageOrder *)order;
			int sp=mo->sender;
			Uint32 messageOrderType=mo->messageOrderType;
			
			if (messageOrderType==MessageOrder::NORMAL_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(230, 230, 230, "%s : %s", game.players[sp]->name, mo->getText());
			}
			else if (messageOrderType==MessageOrder::PRIVATE_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(99, 255, 242, "<%s%s> %s", globalContainer->texts.getString("[from:]"), game.players[sp]->name, mo->getText());
				else if (sp==localPlayer)
				{
					Uint32 rm=mo->recepientsMask;
					int k;
					for (k=0; k<32; k++)
						if (rm==1)
						{
							addMessage(99, 255, 242, "<%s%s> %s", globalContainer->texts.getString("[to:]"), game.players[k]->name, mo->getText());
							break;
						}
						else
							rm=rm>>1;
					assert(k<32);
				}
			}
			else
				assert(false);
			
			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_QUITED :
		{
			if (order->sender==localPlayer)
				isRunning=false;
			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_PLAYER_QUIT_GAME :
		{
			int qp=order->sender;
			addMessage(110, 0, 255,  "%s%s%s", globalContainer->texts.getString("[l has left the game]"), game.players[qp]->name, globalContainer->texts.getString("[r has left the game]"));
			
			game.executeOrder(order, localPlayer);
		}
		break;
		case  ORDER_MAP_MARK:
		{
			MapMarkOrder *mmo=(MapMarkOrder *)order;

			assert(game.teams[mmo->teamNumber]->teamNumber<game.session.numberOfTeam);
			if (game.teams[mmo->teamNumber]->allies & (game.teams[localTeamNo]->me))
			{
				addMark(mmo);
			}
		}
		default:
		{
			game.executeOrder(order, localPlayer);
		}
	}
}

bool GameGUI::loadBase(const SessionInfo *initial)
{
	if (initial->mapGenerationDescriptor)
	{
		assert(initial->fileIsAMap);
		initial->mapGenerationDescriptor->synchronizeNow();
		if (!game.generateMap(*initial->mapGenerationDescriptor))
			return false;
		game.setBase(initial);
	}
	else
	{
		const char *s=initial->getFileName();
		assert(s);
		assert(s[0]);
		printf("GameGUI::loadBase::s=%s.\n", s);
		SDL_RWops *stream=globalContainer->fileManager->open(s,"rb");
		if (!load(stream))
			return false;
		SDL_RWclose(stream);
		game.setBase(initial);
	}

	return true;
}

bool GameGUI::load(SDL_RWops *stream)
{
	init();

	bool result=game.load(stream);

	if (result==false)
	{
		printf("GameGUI : Critical : Wrong map format, signature missmatch\n");
		return false;
	}
	
	if (!game.session.fileIsAMap)
	{
		// load gui's specific infos
		chatMask=SDL_ReadBE32(stream);

		if (game.session.versionMinor>3)
		{
			localPlayer=SDL_ReadBE32(stream);
			localTeamNo=SDL_ReadBE32(stream);
		}
		if (game.session.versionMinor>4)
			assert(!game.session.fileIsAMap);
		
	}

	return true;
}

void GameGUI::save(SDL_RWops *stream, const char *name)
{
	// Game is can't be no more automatically generated
	delete game.session.mapGenerationDescriptor;
	game.session.mapGenerationDescriptor=NULL;
	
	game.save(stream, false, name);
	SDL_WriteBE32(stream, chatMask);
	SDL_WriteBE32(stream, localPlayer);
	SDL_WriteBE32(stream, localTeamNo);
}

// TODO : merge thoses 3 functions into one

void GameGUI::drawButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	globalContainer->gfx->drawFilledRect(x, y, 104, 16, 128, 128, 128);
	globalContainer->gfx->drawHorzLine(x, y, 104, 200, 200, 200);
	globalContainer->gfx->drawHorzLine(x, y+15, 104, 28, 28, 28);
	globalContainer->gfx->drawVertLine(x, y, 16, 200, 200, 200);
	globalContainer->gfx->drawVertLine(x+103, y, 16, 200, 200, 200);
	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=globalContainer->texts.getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+((104-len)>>1), y+((16-h)>>1), globalContainer->littleFont, "%s", textToDraw);
}

void GameGUI::drawBlueButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	globalContainer->gfx->drawFilledRect(x, y, 104, 16, 128, 128, 192);
	globalContainer->gfx->drawHorzLine(x, y, 104, 200, 200, 255);
	globalContainer->gfx->drawHorzLine(x, y+15, 104, 28, 28, 92);
	globalContainer->gfx->drawVertLine(x, y, 16, 200, 200, 255);
	globalContainer->gfx->drawVertLine(x+103, y, 16, 200, 200, 255);
	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=globalContainer->texts.getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+((104-len)>>1), y+((16-h)>>1), globalContainer->littleFont, "%s", textToDraw);
}

void GameGUI::drawRedButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	globalContainer->gfx->drawFilledRect(x, y, 104, 16, 192, 128, 128);
	globalContainer->gfx->drawHorzLine(x, y, 104, 255, 200, 200);
	globalContainer->gfx->drawHorzLine(x, y+15, 104, 92, 28, 28);
	globalContainer->gfx->drawVertLine(x, y, 16, 255, 200, 200);
	globalContainer->gfx->drawVertLine(x+103, y, 16, 255, 200, 200);
	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=globalContainer->texts.getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+((104-len)>>1), y+((16-h)>>1), globalContainer->littleFont, "%s", textToDraw);
}

void GameGUI::drawTextCenter(int x, int y, const char *caption, int i)
{
	char *text;

	if (i==-1)
		text=globalContainer->texts.getString(caption);
	else
		text=globalContainer->texts.getString(caption, i);

	int dec=(128-globalContainer->littleFont->getStringWidth(text))>>1;
	globalContainer->gfx->drawString(x+dec, y, globalContainer->littleFont, "%s", text);
}

void GameGUI::drawScrollBox(int x, int y, int value, int valueLocal, int act, int max)
{
	globalContainer->gfx->drawFilledRect(x, y, 128, 16, 128, 128, 128);
	globalContainer->gfx->drawHorzLine(x, y, 128, 200, 200, 200);
	globalContainer->gfx->drawHorzLine(x, y+16, 128, 28, 28, 28);
	globalContainer->gfx->drawVertLine(x, y, 16, 200, 200, 200);
	globalContainer->gfx->drawVertLine(x+16, y, 16, 200, 200, 200);
	globalContainer->gfx->drawVertLine(x+16+96, y, 16, 200, 200, 200);
	globalContainer->gfx->drawVertLine(x+15, y, 16, 28, 28, 28);
	globalContainer->gfx->drawVertLine(x+16+95, y, 16, 28, 28, 28);
	globalContainer->gfx->drawVertLine(x+16+96+15, y, 16, 28, 28, 28);
	globalContainer->gfx->drawString(x+6, y+1, globalContainer->littleFont, "-");
	globalContainer->gfx->drawString(x+96+16+6, y+1, globalContainer->littleFont, "+");
	int size=(valueLocal*94)/max;
	globalContainer->gfx->drawFilledRect(x+16+1, y+1, size, 14, 100, 100, 200);
	size=(value*94)/max;
	globalContainer->gfx->drawFilledRect(x+16+1, y+3, size, 10, 28, 28, 200);
	size=(act*94)/max;
	globalContainer->gfx->drawFilledRect(x+16+1, y+5, size, 6, 28, 200, 28);
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

void GameGUI::iterateSelection(void)
{
	if (displayMode==BUILDING_SELECTION_VIEW)
	{
		assert (selBuild);
		int pos=Building::UIDtoID(selectionUID);
		int team=Building::UIDtoTeam(selectionUID);
		int i=pos;
		if (team==localTeamNo)
		{
			while (i<pos+512)
			{
				i++;
				Building *b=game.teams[team]->myBuildings[i&0x1FF];
				if (b && b->typeNum==selBuild->typeNum)
				{
					selBuild=b;
					selectionUID=b->UID;
					centerViewportOnSelection();
					break;
				}
			}
		}
	}
}

void GameGUI::centerViewportOnSelection(void)
{
	if (selectionUID<0)
	{
		assert (selBuild);
		Building *b=game.teams[Building::UIDtoTeam(selectionUID)]->myBuildings[Building::UIDtoID(selectionUID)];
		viewportX=b->getMidX()-((globalContainer->gfx->getW()-128)>>6);
		viewportY=b->getMidY()-((globalContainer->gfx->getH())>>6);
		viewportX=(viewportX+game.map.getW())&game.map.getMaskW();
		viewportY=(viewportY+game.map.getH())&game.map.getMaskH();
	}
}

void GameGUI::addMessage(Uint8 r, Uint8 g, Uint8 b, const char *msgText, ...)
{
	Message message;
	message.showTicks=Message::DEFAULT_MESSAGE_SHOW_TICKS;
	message.r = r;
	message.g = g;
	message.b = b;
	char fullText[1024];
	
	va_list ap;
	va_start(ap, msgText);
	vsnprintf (fullText, 1024, msgText, ap);
	va_end(ap);
	fullText[1023]=0;
	
	char *sLine=fullText;
	char *ptr=fullText;
	char *lastSpace=fullText;
	
	globalContainer->standardFont->pushStyle(Font::STYLE_BOLD);
	while (*ptr)
	{
		if (strchr(" \t\n\r", *ptr))
			lastSpace=ptr;
			
		char c=*(ptr+1);
		*(ptr+1)=0;
		
		if ((globalContainer->standardFont->getStringWidth(sLine)>globalContainer->gfx->getW()-128-64) || (ptr-sLine>=Message::MAX_DISPLAYED_MESSAGE_SIZE))
		{
			int len=lastSpace-sLine;
			// prevent crash if line doesn't have any space
			if (len)
			{
				memcpy(message.text, sLine, len);
				message.text[len]=0;
				messagesList.push_back(message);

				sLine=lastSpace+1;
			}
			else
			{
				len=ptr-sLine;
				memcpy(message.text, sLine, len);
				message.text[len]=0;
				messagesList.push_back(message);
				
				sLine=ptr;
			}
			lastSpace=sLine;
		}
		*(ptr+1)=c;
		ptr++;
	}
	globalContainer->standardFont->popStyle();
	
	memcpy(message.text, sLine, ptr-sLine);
	message.text[ptr-sLine]=0;
	messagesList.push_back(message);
}

void GameGUI::addMark(MapMarkOrder *mmo)
{
	Mark mark;
	mark.showTicks=Mark::DEFAULT_MARK_SHOW_TICKS;
	mark.x=mmo->x;
	mark.y=mmo->y;
	mark.r=game.teams[mmo->teamNumber]->colorR;
	mark.g=game.teams[mmo->teamNumber]->colorG;
	mark.b=game.teams[mmo->teamNumber]->colorB;
	
	markList.push_front(mark);
}

