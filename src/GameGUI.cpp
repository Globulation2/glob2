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

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <algorithm>

#include <FileManager.h>
#include <GUITextInput.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "YOG.h"
#include "IRC.h"
#include "SoundMixer.h"

#ifndef DX9_BACKEND	// TODO:Die!
#include <SDL_keysym.h>
#else
#include <Types.h>
#endif

#define TYPING_INPUT_BASE_INC 7
#define TYPING_INPUT_MAX_POS 46

#define YPOS_BASE_DEFAULT 160
#define YPOS_BASE_BUILDING YPOS_BASE_DEFAULT
#define YPOS_BASE_FLAG YPOS_BASE_DEFAULT
#define YPOS_BASE_STAT YPOS_BASE_DEFAULT
#define YPOS_BASE_UNIT YPOS_BASE_DEFAULT
#define YPOS_BASE_RESSOURCE YPOS_BASE_DEFAULT

#define YOFFSET_NAME 28
#define YOFFSET_ICON 52
#define YOFFSET_CARYING 34
#define YOFFSET_BAR 30
#define YOFFSET_INFOS 12
#define YOFFSET_TOWER 22

#define YOFFSET_B_SEP 3

#define YOFFSET_TEXT_BAR 16
#define YOFFSET_TEXT_PARA 14
#define YOFFSET_TEXT_LINE 12

#define YOFFSET_PROGRESS_BAR 10

#define YOFFSET_BRUSH 56

//! Pointer to IRC client in YOGScreen, NULL if no IRC client is available
extern IRC *ircPtr;

enum GameGUIGfxId
{
	EXCHANGE_BUILDING_ICONS = 21
};

//! The screen that contains the text input while typing message in game
class InGameTextInput:public OverlayScreen
{
protected:
	//! the text input widget
	TextInput *textInput;

public:
	//! InGameTextInput constructor
	InGameTextInput(GraphicContext *parentCtx);
	//! InGameTextInput destructor
	virtual ~InGameTextInput() { }
	//! React on action from any widget (but there is only one anyway)
	virtual void onAction(Widget *source, Action action, int par1, int par2);
	//! Return the text typed
	const char *getText(void) const { return textInput->getText(); }
	//! Set the text
	void setText(const char *text) const { textInput->setText(text); }
};

InGameTextInput::InGameTextInput(GraphicContext *parentCtx)
:OverlayScreen(parentCtx, 492, 34)
{
	textInput=new TextInput(5, 5, 482, 24, ALIGN_LEFT, ALIGN_LEFT, "standard", "", true, 256);
	addWidget(textInput);
	dispatchInit();
}

void InGameTextInput::onAction(Widget *source, Action action, int par1, int par2)
{
	if (action==TEXT_VALIDATED)
	{
		endValue=0;
	}
}

GameGUI::GameGUI()
:game(this)
{
}

GameGUI::~GameGUI()
{

}

void GameGUI::init()
{
	isRunning=true;
	gamePaused=false;
	hardPause=false;
	exitGlobCompletely=false;
	toLoadGameFileName[0]=0;
	drawHealthFoodBar=true;
	drawPathLines=false;
	viewportX=0;
	viewportY=0;
	mouseX=0;
	mouseY=0;
	displayMode=BUILDING_VIEW;
	selectionMode=NO_SELECTION;
	selectionPushed=false;
	selection.build = 0;
	selection.building = NULL;
	selection.unit = NULL;
	miniMapPushed=false;
	putMark=false;
	showUnitWorkingToBuilding=false;
	chatMask=0xFFFFFFFF;

	viewportSpeedX=0;
	viewportSpeedY=0;

	inGameMenu=IGM_NONE;
	gameMenuScreen=NULL;
	typingInputScreen=NULL;
	typingInputScreenPos=0;

	messagesList.clear();
	markList.clear();
	localTeam=NULL;
	teamStats=NULL;

	hasEndOfGameDialogBeenShown=false;
	panPushed=false;
	brushType = FORBIDDEN_BRUSH;

	buildingsChoiceName.clear();
	buildingsChoiceName.push_back("swarm");
	buildingsChoiceName.push_back("inn");
	buildingsChoiceName.push_back("hospital");
	buildingsChoiceName.push_back("racetrack");
	buildingsChoiceName.push_back("swimmingpool");
	buildingsChoiceName.push_back("barracks");
	buildingsChoiceName.push_back("school");
	buildingsChoiceName.push_back("defencetower");
	buildingsChoiceName.push_back("stonewall");
	buildingsChoiceName.push_back("market");
	buildingsChoiceState.resize(buildingsChoiceName.size(), true);

	flagsChoiceName.clear();
	flagsChoiceName.push_back("explorationflag");
	flagsChoiceName.push_back("warflag");
	flagsChoiceName.push_back("clearingflag");
	flagsChoiceState.resize(flagsChoiceName.size(), true);

	hiddenGUIElements=0;
	
 	for (size_t i=0; i<SMOOTH_CPU_LOAD_WINDOW_LENGTH; i++)
		smoothedCpuLoad[i]=0;
	smoothedCpuLoadPos=0;
}

void GameGUI::adjustLocalTeam()
{
	assert(localTeamNo>=0);
	assert(localTeamNo<32);
	assert(game.session.numberOfPlayer>0);
	assert(game.session.numberOfPlayer<32);
	assert(localTeamNo<game.session.numberOfTeam);
	
	localTeam=game.teams[localTeamNo];
	assert(localTeam);
	teamStats=&localTeam->stats;
	
	// recompute local forbidden and guard areas
	game.map.computeLocalForbidden(localTeamNo);
	game.map.computeLocalGuardArea(localTeamNo);
}

void GameGUI::adjustInitialViewport()
{
	assert(localTeam);
	viewportX=localTeam->startPosX-((globalContainer->gfx->getW()-128)>>6);
	viewportY=localTeam->startPosY-(globalContainer->gfx->getH()>>6);
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();
}

void GameGUI::moveFlag(int mx, int my, bool drop)
{
	int posX, posY;
	Building* selBuild=selection.building;
	game.map.cursorToBuildingPos(mx, my, selBuild->type->width, selBuild->type->height, &posX, &posY, viewportX, viewportY);
	if ((selBuild->posXLocal!=posX)
		||(selBuild->posYLocal!=posY)
		||(drop && (selectionPushedPosX!=posX || selectionPushedPosY!=posY)))
	{
		Uint16 gid=selBuild->gid;
		OrderMoveFlag *oms=new OrderMoveFlag(gid, posX, posY, drop);
		// First, we check if anoter move of the same flag is already in the "orderQueue".
		bool found=false;
		for (std::list<Order *>::iterator it=orderQueue.begin(); it!=orderQueue.end(); ++it)
		{
			if ( ((*it)->getOrderType()==ORDER_MOVE_FLAG) && ( ((OrderMoveFlag *)(*it))->gid==gid) )
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

void GameGUI::brushStep(int mx, int my)
{
	// if we have an area over 32x32, which mean over 128 bytes, send it
	if (brushAccumulator.getAreaSurface() > 32*32)
	{
		sendBrushOrders();
	}
	// we add brush to accumulator
	int mapX, mapY;
	game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY,  viewportX, viewportY);
	int fig = brush.getFigure();
	brushAccumulator.applyBrush(BrushApplication(mapX, mapY, fig));
	// we get coordinates
	int startX = mapX-BrushTool::getBrushDimX(fig);
	int startY = mapY-BrushTool::getBrushDimY(fig);
	int width  = BrushTool::getBrushWidth(fig);
	int height = BrushTool::getBrushHeight(fig);
	// we update local values
	if (brush.getType() == BrushTool::MODE_ADD)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (brushType == FORBIDDEN_BRUSH)
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					else if (brushType == GUARD_AREA_BRUSH)
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
					else
						assert(false);
				}
	}
	else if (brush.getType() == BrushTool::MODE_DEL)
	{
		for (int y=startY; y<startY+height; y++)
			for (int x=startX; x<startX+width; x++)
				if (BrushTool::getBrushValue(fig, x-startX, y-startY))
				{
					if (brushType == FORBIDDEN_BRUSH)
						game.map.localForbiddenMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					else if (brushType == GUARD_AREA_BRUSH)
						game.map.localGuardAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
					else
						assert(false);
				}
	}
	else
		assert(false);
}

void GameGUI::sendBrushOrders(void)
{
	if (brushAccumulator.getApplicationCount() > 0)
	{
		//std::cout << "GameGUI::sendBrushOrders : sending application of size " << brushAccumulator.getAreaSurface()/8 << std::endl;
		if (brushType == FORBIDDEN_BRUSH)
			orderQueue.push_back(new OrderAlterateForbidden(localTeamNo, brush.getType(), &brushAccumulator));
		else if (brushType == GUARD_AREA_BRUSH)
			orderQueue.push_back(new OrderAlterateGuardArea(localTeamNo, brush.getType(), &brushAccumulator));
		else
			assert(false);
		brushAccumulator.clear();
	}
}

void GameGUI::dragStep(void)
{
	int mx, my;
	Uint8 button = SDL_GetMouseState(&mx, &my);
	if ((button&SDL_BUTTON(1)) && (mx<globalContainer->gfx->getW()-128))
	{
		// Update flag
		if (selectionMode == BUILDING_SELECTION)
		{
			Building* selBuild=selection.building;
			if (selBuild && selectionPushed && (selBuild->type->isVirtual))
				moveFlag(mx, my, false);
		}
		// Update brush
		else if (selectionMode==BRUSH_SELECTION)
		{
			brushStep(mx, my);
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
	handleKeyAlways();
	viewportX+=viewportSpeedX;
	viewportY+=viewportSpeedY;
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();

	if ((viewportX!=oldViewportX) || (viewportY!=oldViewportY))
		dragStep();

	assert(localTeam);
	if (localTeam->wasEvent(Team::UNIT_UNDER_ATTACK_EVENT))
	{
		Uint16 gid=localTeam->getEventId();
		int team=Unit::GIDtoTeam(gid);
		int id=Unit::GIDtoID(gid);
		Unit *u=game.teams[team]->myUnits[id];
		if (u)
		{
			int strDec=(int)(u->typeNum);
			addMessage(200, 30, 30, Toolkit::getStringTable()->getString("[Your %s are under attack]"), Toolkit::getStringTable()->getString("[units type]", strDec));
		}
	}
	if (localTeam->wasEvent(Team::UNIT_CONVERTED_LOST))
	{
		addMessage(140, 0, 0, Toolkit::getStringTable()->getString("[Your unit got converted to another team]"));
	}
	if (localTeam->wasEvent(Team::UNIT_CONVERTED_ACQUIERED))
	{
		addMessage(100, 255, 100, Toolkit::getStringTable()->getString("[Another team's unit got converted to your team]"));
	}
	if (localTeam->wasEvent(Team::BUILDING_UNDER_ATTACK_EVENT))
	{
		Uint16 gid=localTeam->getEventId();
		int team=Building::GIDtoTeam(gid);
		int id=Building::GIDtoID(gid);
		Building *b=game.teams[team]->myBuildings[id];
		if (b)
		{
			int strDec=b->type->shortTypeNum;
			addMessage(255, 0, 0, "%s", Toolkit::getStringTable()->getString("[the building is under attack]", strDec));
		}
	}
	if (localTeam->wasEvent(Team::BUILDING_FINISHED_EVENT))
	{
		Uint16 gid=localTeam->getEventId();
		int team=Building::GIDtoTeam(gid);
		int id=Building::GIDtoID(gid);
		Building *b=game.teams[team]->myBuildings[id];
		if (b)
		{
			int strDec=b->type->shortTypeNum;
			addMessage(30, 255, 30, "%s",  Toolkit::getStringTable()->getString("[the building is finished]", strDec));
		}
	}
	
	// music step
	musicStep();
	
	// do a yog step
	yog->step();
	
	// do a irc step if we have to
	if (ircPtr)
		ircPtr->step();
	// TODO : should we read the datas now or let them for return to YOGScreen ?

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
					addMessage(99, 255, 242, "<%s%s> %s", Toolkit::getStringTable()->getString("[from:]"), m->userName, m->text);
				break;
				case YCMT_ADMIN_MESSAGE:
					addMessage(138, 99, 255, "<%s> %s", m->userName, m->text);
				break;
				case YCMT_PRIVATE_RECEIPT:
					addMessage(99, 255, 242, "<%s%s> %s", Toolkit::getStringTable()->getString("[to:]"), m->userName, m->text);
				break;
				case YCMT_PRIVATE_RECEIPT_BUT_AWAY:
					addMessage(99, 255, 242, "<%s%s> %s", Toolkit::getStringTable()->getString("[away:]"), m->userName, m->text);
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

	// do we have won or lost conditions
	checkWonConditions();
	
	if (game.anyPlayerWaited) // TODO: warning valgrind
		game.anyPlayerWaitedTimeFor++;
}

void GameGUI::musicStep(void)
{
	static unsigned warTimeout = 0;
	static unsigned buildingTimeout = 0;
	
	// something bad happened
	if (localTeam->wasEvent(Team::UNIT_UNDER_ATTACK_EVENT) ||
		localTeam->wasEvent(Team::BUILDING_UNDER_ATTACK_EVENT) ||
		localTeam->wasEvent(Team::UNIT_CONVERTED_LOST))
	{
	   warTimeout = 220;
	   globalContainer->mix->setNextTrack(4, true);
	}
	
	// something good happened
	if (localTeam->wasEvent(Team::BUILDING_FINISHED_EVENT) ||
		localTeam->wasEvent(Team::UNIT_CONVERTED_ACQUIERED))
	{
		buildingTimeout = 220;
		globalContainer->mix->setNextTrack(3, true);
	}
	
	// if end of special thing
	if ((buildingTimeout == 1) || (warTimeout == 1))
		globalContainer->mix->setNextTrack(2, true);
	
	// decay variables
	if (warTimeout > 0)
		warTimeout--;
	if (buildingTimeout > 0)
		buildingTimeout--;
}

void GameGUI::syncStep(void)
{
	assert(localTeam);
	assert(teamStats);

	if ((game.stepCounter&255) == 79)
	{
		const char *name = Toolkit::getStringTable()->getString("[auto save]");
		std::string fileName = glob2NameToFilename("games", name, "game");
		OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(fileName));
		if (stream->isEndOfStream())
		{
			std::cerr << "GameGUI::syncStep : can't open autosave file " << name << " for writing" << std::endl;
		}
		else
		{
			save(stream, name);
		}
		delete stream;
	}
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
					gameMenuScreen = new LoadSaveScreen("games", "game", true, game.session.getMapNameC(), glob2FilenameToName, glob2NameToFilename);
					return true;
				}
				break;
				case InGameMainScreen::SAVE_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_SAVE;
					gameMenuScreen = new LoadSaveScreen("games", "game", false, game.session.getMapNameC(), glob2FilenameToName, glob2NameToFilename);
					return true;
				}
				break;
				case InGameMainScreen::ALLIANCES:
				{
					delete gameMenuScreen;
					gameMenuScreen=NULL;
					inGameMenu=IGM_ALLIANCE;
					gameMenuScreen = new InGameAllianceScreen(this);
					return true;
				}
				break;
				case InGameMainScreen::OPTIONS:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_OPTION;
					gameMenuScreen = new InGameOptionScreen(this);
					return true;
				}
				break;
				case InGameMainScreen::RETURN_GAME:
				{
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					gameMenuScreen=NULL;
					return true;
				}
				break;
				case InGameMainScreen::QUIT_GAME:
				{
					orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					gameMenuScreen=NULL;
					return true;
				}
				break;
				default:
				return false;
			}
		}

		case IGM_ALLIANCE:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameAllianceScreen::OK :
				{
					Uint32 playerMask[5];
					Uint32 teamMask[5];
					playerMask[0]=((InGameAllianceScreen *)gameMenuScreen)->getAlliedMask();
					playerMask[1]=((InGameAllianceScreen *)gameMenuScreen)->getEnemyMask();
					playerMask[2]=((InGameAllianceScreen *)gameMenuScreen)->getExchangeVisionMask();
					playerMask[3]=((InGameAllianceScreen *)gameMenuScreen)->getFoodVisionMask();
					playerMask[4]=((InGameAllianceScreen *)gameMenuScreen)->getOtherVisionMask();
					teamMask[0]=teamMask[1]=teamMask[2]=teamMask[3]=teamMask[4]=0;

					// mask are for players, we need to convert them to team.
					for (int pi=0; pi<game.session.numberOfPlayer; pi++)
					{
						int otherTeam=game.players[pi]->teamNumber;
						for (int mi=0; mi<5; mi++)
						{
							if (playerMask[mi]&(1<<pi))
							{
								// player is set, set team
								teamMask[mi]|=(1<<otherTeam);
							}
						}
					}
					
					// we have a special cases for uncontroled Teams:
					// FIXME : remove this
					for (int ti=0; ti<game.session.numberOfTeam; ti++)
						if (game.teams[ti]->playersMask==0)
							teamMask[1]|=(1<<ti); // we want to hit them.
					
					orderQueue.push_back(new SetAllianceOrder(localTeamNo,
						teamMask[0], teamMask[1], teamMask[2], teamMask[3], teamMask[4]));
					chatMask=((InGameAllianceScreen *)gameMenuScreen)->getChatMask();
					inGameMenu=IGM_NONE;
					delete gameMenuScreen;
					gameMenuScreen=NULL;
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
				gameMenuScreen=NULL;
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
					const char *locationName=((LoadSaveScreen *)gameMenuScreen)->getFileName();
					if (inGameMenu==IGM_LOAD)
					{
						strncpy(toLoadGameFileName, locationName, sizeof(toLoadGameFileName));
						toLoadGameFileName[sizeof(toLoadGameFileName)-1]=0;
						orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));
					}
					else
					{
						OutputStream *stream = new BinaryOutputStream(Toolkit::getFileManager()->openOutputStreamBackend(locationName));
						if (stream->isEndOfStream())
						{
							std::cerr << "GGU : Can't save map " << locationName << std::endl;
						}
						else
						{
							const char *name = ((LoadSaveScreen *)gameMenuScreen)->getName();
							assert(name);
							save(stream, name);
						}
						delete stream;
					}
				}

				case LoadSaveScreen::CANCEL:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
				return true;

				default:
				return false;
			}
		}

		case IGM_END_OF_GAME:
		{
			switch (gameMenuScreen->endValue)
			{
				case InGameEndOfGameScreen::QUIT:
				orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));

				case InGameEndOfGameScreen::CONTINUE:
				inGameMenu=IGM_NONE;
				delete gameMenuScreen;
				gameMenuScreen=NULL;
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
						if (game.players[i] &&
							(game.players[i]->type>=Player::P_AI||game.players[i]->type==Player::P_IP||game.players[i]->type==Player::P_LOCAL))
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

	// the dump (debug) keys are always handled
	if (event->type == SDL_KEYDOWN)
		handleKeyDump(event->key);
	
	// if there is a menu he get events first
	if (inGameMenu)
	{
		processGameMenu(event);
	}
	else
	{
		if (event->type==SDL_KEYDOWN)
		{
			handleKey(event->key.keysym.sym, true);
		}
		else if (event->type==SDL_KEYUP)
		{
			handleKey(event->key.keysym.sym, false);
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

				// NOTE : if there is more than this, move to a func
				if ((event->button.y<34) && (event->button.x<globalContainer->gfx->getW()-126) && (event->button.x>globalContainer->gfx->getW()-160))
				{
					if (inGameMenu==IGM_NONE)
					{
						gameMenuScreen=new InGameMainScreen(!(hiddenGUIElements & HIDABLE_ALLIANCE));
						inGameMenu=IGM_MAIN;
					}
					// the following is commented becase we don't get click event while in menu.
					// if this change uncomment the following code :
					/*else
					{
						delete gameMenuScreen;
						gameMenuScreen=NULL;
						inGameMenu=IGM_NONE;
					}*/
				}
			}
			else if (button==SDL_BUTTON_MIDDLE)
			{
				if ((selectionMode==BUILDING_SELECTION) && (globalContainer->gfx->getW()-event->button.x<128))
				{
					Building* selBuild=selection.building;
					assert (selBuild);
					selBuild->verbose=(selBuild->verbose+1)%5;
					printf("building gid=(%d)\n", selBuild->gid);
					if (selBuild->verbose==0)
						printf(" verbose off\n");
					else if (selBuild->verbose==1 || selBuild->verbose==2)
						printf(" verbose global [%d]\n", selBuild->verbose&1);
					else if (selBuild->verbose==3 || selBuild->verbose==4)
						printf(" verbose local [%d]\n", selBuild->verbose&1);
					else
						assert(false);
					printf(" pos=(%d, %d)\n", selBuild->posX, selBuild->posY);
					printf(" dirtyLocalGradient=[%d, %d]\n", selBuild->dirtyLocalGradient[0], selBuild->dirtyLocalGradient[1]);
					printf(" globalGradient=[%p, %p]\n", selBuild->globalGradient[0], selBuild->globalGradient[1]);
					printf(" locked=[%d, %d]\n", selBuild->locked[0], selBuild->locked[1]);
					printf(" unitsInsideSubscribe=%d,\n", static_cast<unsigned>(selBuild->unitsInsideSubscribe.size()));
					
				}
				else
				{
					// Enable paning
					panPushed=true;
					panMouseX=event->button.x;
					panMouseY=event->button.y;
					panViewX=viewportX;
					panViewY=viewportY;
				}
			}
			else if (button==4)
			{
				if (selectionMode==BUILDING_SELECTION)
				{
					Building* selBuild=selection.building;
					if ((selBuild->owner->teamNumber==localTeamNo) &&
						(selBuild->buildingState==Building::ALIVE))
					{
						if ((selBuild->type->maxUnitWorking) &&
							(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)&&
							!(SDL_GetModState()&KMOD_SHIFT))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal+=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
						}
						else if ((selBuild->type->defaultUnitStayRange) &&
							(selBuild->unitStayRangeLocal<(unsigned)selBuild->type->maxUnitStayRange) &&
							(SDL_GetModState()&KMOD_SHIFT))
						{
							int nbReq=(selBuild->unitStayRangeLocal+=1);
							orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
						}
					}
				}
			}
			else if (button==5)
			{
				if (selectionMode==BUILDING_SELECTION)
				{
					Building* selBuild=selection.building;
					if ((selBuild->owner->teamNumber==localTeamNo) &&
						(selBuild->buildingState==Building::ALIVE))
					{
						if ((selBuild->type->maxUnitWorking) &&
							(selBuild->maxUnitWorkingLocal>0)&&
							!(SDL_GetModState()&KMOD_SHIFT))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal-=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
						}
						else if ((selBuild->type->defaultUnitStayRange) &&
							(selBuild->unitStayRangeLocal>0) &&
							(SDL_GetModState()&KMOD_SHIFT))
						{
							int nbReq=(selBuild->unitStayRangeLocal-=1);
							orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
						}
					}
				}
			}
		}
		else if (event->type==SDL_MOUSEBUTTONUP)
		{
			if ((selectionMode==BUILDING_SELECTION) && selectionPushed && selection.building->type->isVirtual)
			{
				// update flag
				int mx, my;
				SDL_GetMouseState(&mx, &my);
				if (mx<globalContainer->gfx->getW()-128)
					moveFlag(mx, my, true);
			}
			// We send the order
			else if (selectionMode==BRUSH_SELECTION)
			{
				sendBrushOrders();
			}
	
			miniMapPushed=false;
			selectionPushed=false;
			panPushed=false;
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
		globalContainer->gfx->setRes(newW, newH, globalContainer->settings.screenDepth, globalContainer->settings.screenFlags, (DrawableSurface::GraphicContextType)globalContainer->settings.graphicType);
	}
}

void GameGUI::handleActivation(Uint8 state, Uint8 gain)
{
	if (gain==0)
	{
		viewportSpeedX=viewportSpeedY=0;
	}
}

void GameGUI::handleRightClick(void)
{
	// We cycle between views:
	if (selectionMode==NO_SELECTION)
	{
		nextDisplayMode();
	}
	// We deselect all, we want no tools activated:
	else
	{
		clearSelection();
	}
}

void GameGUI::nextDisplayMode(void)
{
	int t=0;
	do
	{
		displayMode=DisplayMode((displayMode + 1) % NB_VIEWS);
		if ((t++)==4)
		{
			displayMode=NB_VIEWS;
			break;
		}
	} while ((1<<((int)displayMode)) & hiddenGUIElements);
}

void GameGUI::handleKey(SDLKey key, bool pressed)
{
	int modifier;

	if (pressed)
		modifier=1;
	else
		modifier=-1;

	if (typingInputScreen == NULL)
	{
		switch (key)
		{
			case SDLK_ESCAPE:
				{
					if ((inGameMenu==IGM_NONE) && (!pressed))
					{
						gameMenuScreen=new InGameMainScreen(!(hiddenGUIElements & HIDABLE_ALLIANCE));
						inGameMenu=IGM_MAIN;
					}
				}
				break;
			case SDLK_PLUS:
			case SDLK_KP_PLUS:
				{
					if ((pressed) && (selectionMode==BUILDING_SELECTION))
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal+=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
						}
					}
				}
				break;
			case SDLK_MINUS:
			case SDLK_KP_MINUS:
				{
					if ((pressed) && (selectionMode==BUILDING_SELECTION))
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal>0))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal-=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
						}
					}
				}
				break;
			case SDLK_d:
				{
					if ((pressed) && (selectionMode==BUILDING_SELECTION))
					{
						Building* selBuild=selection.building;
						if (selBuild->owner->teamNumber==localTeamNo)
						{
							orderQueue.push_back(new OrderDelete(selBuild->gid));
						}
					}
				}
				break;
			case SDLK_u:
			case SDLK_a:
				{
					if ((pressed) && (selectionMode==BUILDING_SELECTION))
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->nextLevel!=-1) && (!selBuild->type->isBuildingSite))
						{
							orderQueue.push_back(new OrderConstruction(selBuild->gid));
						}
					}
				}
				break;
			case SDLK_r:
				{
					if ((pressed) && (selectionMode==BUILDING_SELECTION))
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->hp<selBuild->type->hpMax) && (!selBuild->type->isBuildingSite))
						{
							orderQueue.push_back(new OrderConstruction(selBuild->gid));
						}
					}
				}
				break;
			case SDLK_t :
				if (pressed)
					drawPathLines=!drawPathLines;
				break;
			case SDLK_i :
				if (pressed)
					drawHealthFoodBar=!drawHealthFoodBar;
				break;
			case SDLK_m :
				if (pressed)
				{
					putMark=true;
					globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_MARK);
				}
				break;

			case SDLK_RETURN :
				if (pressed)
				{
					typingInputScreen=new InGameTextInput(globalContainer->gfx);
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
			case SDLK_HOME:
				if (pressed)
				{
					int evX = localTeam->startPosX;
					int evY = localTeam->startPosY;
				    int sw = globalContainer->gfx->getW();
					int sh = globalContainer->gfx->getH();
					viewportX = evX-((sw-128)>>6);
					viewportY = evY-(sh>>6);
				}
				break;
			case SDLK_p:
			case SDLK_PAUSE:
				if (pressed)
					orderQueue.push_back(new PauseGameOrder(!gamePaused));
				break;
			case SDLK_SCROLLOCK:
				if (pressed)
					hardPause=!hardPause;
				break;
			default:
			break;
		}
	}
}

void GameGUI::handleKeyDump(SDL_KeyboardEvent key)
{
	if (key.keysym.sym == SDLK_PRINT)
	{
		if ((key.keysym.mod & KMOD_SHIFT) != 0)
		{
			OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("glob2.dump.txt"));
			if (stream->isEndOfStream())
			{
				std::cerr << "Can't dump full game memory to file glob2.dump.txt" << std::endl;
			}
			else
			{
				std::cerr << "Dump full game memory" << std::endl;
				save(stream, "glob2.dump.txt");
			}
			delete stream;
		}
		else
		{
			globalContainer->gfx->printScreen("screenshot.bmp");
		}
	}
}

void GameGUI::handleKeyAlways(void)
{
	SDL_PumpEvents();
	Uint8 *keystate = SDL_GetKeyState(NULL);

	if (keystate[SDLK_UP])
		viewportY--;
	if (keystate[SDLK_KP8])
		viewportY--;
	if (keystate[SDLK_DOWN])
		viewportY++;
	if (keystate[SDLK_KP2])
		viewportY++;
	if (keystate[SDLK_LEFT])
		viewportX--;
	if (keystate[SDLK_KP4])
		viewportX--;
	if (keystate[SDLK_RIGHT])
		viewportX++;
	if (keystate[SDLK_KP6])
		viewportX++;
	if (keystate[SDLK_KP7])
	{
		viewportX--;
		viewportY--;
	}
	if (keystate[SDLK_KP9])
	{
		viewportX++;
		viewportY--;
	}
	if (keystate[SDLK_KP1])
	{
		viewportX--;
		viewportY++;
	}
	if (keystate[SDLK_KP3])
	{
		viewportX++;
		viewportY++;
	}
}

void GameGUI::minimapMouseToPos(int mx, int my, int *cx, int *cy, bool forScreenViewport)
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
	*cx+=localTeam->startPosX-(game.map.getW()/2);
	*cy+=localTeam->startPosY-(game.map.getH()/2);
	if (forScreenViewport)
	{
		*cx-=((globalContainer->gfx->getW()-128)>>6);
		*cy-=((globalContainer->gfx->getH())>>6);
	}

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
		minimapMouseToPos(mx-globalContainer->gfx->getW()+128, my, &viewportX, &viewportY, true);
	}
	else
	{
		if (mx<scrollZoneWidth)
			viewportSpeedX=-1;
		else if ((mx>globalContainer->gfx->getW()-scrollZoneWidth) )
			viewportSpeedX=1;
		else
			viewportSpeedX=0;

		if (my<scrollZoneWidth)
			viewportSpeedY=-1;
		else if (my>globalContainer->gfx->getH()-scrollZoneWidth)
			viewportSpeedY=1;
		else
			viewportSpeedY=0;
	}
	
	if (panPushed)
	{
		// handle panning
		int dx=(mx-panMouseX)>>1;
		int dy=(my-panMouseY)>>1;
		viewportX=(panViewX+dx)&game.map.getMaskW();
		viewportY=(panViewY+dy)&game.map.getMaskH();
	}

	dragStep();
}

void GameGUI::handleMapClick(int mx, int my, int button)
{
	if (selectionMode==TOOL_SELECTION)
	{
		// we get the type of building
		// try to get the building site, if it doesn't exists, get the finished building (for flags)
		Sint32  typeNum=globalContainer->buildingsTypes.getTypeNum(selection.build, 0, true);
		if (typeNum==-1)
		{
			typeNum=globalContainer->buildingsTypes.getTypeNum(selection.build, 0, false);
			assert(globalContainer->buildingsTypes.get(typeNum)->isVirtual);
		}
		assert (typeNum!=-1);

		BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);

		int mapX, mapY;
		int tempX, tempY;
		bool isRoom;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		if (bt->isVirtual)
			isRoom=game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, localTeamNo);
		else
			isRoom=game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);

		if (isRoom)
			orderQueue.push_back(new OrderCreate(localTeamNo, mapX, mapY, typeNum));
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		brushStep(mouseX, mouseY);
	}
	else if (putMark)
	{
		int markx, marky;
		game.map.displayToMapCaseAligned(mx, my, &markx, &marky, viewportX, viewportY);
		orderQueue.push_back(new MapMarkOrder(localTeamNo, markx, marky));
		globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
		putMark = false;
	}
	else
	{
		int mapX, mapY;
		game.map.displayToMapCaseAligned(mx, my, &mapX, &mapY, viewportX, viewportY);
		selectionPushedPosX=mapX;
		selectionPushedPosY=mapY;
		// check for flag first
		for (std::list<Building *>::iterator virtualIt=localTeam->virtualBuildings.begin();
				virtualIt!=localTeam->virtualBuildings.end(); ++virtualIt)
			{
				Building *b=*virtualIt;
				if ((b->posX==mapX) && (b->posY==mapY))
				{
					setSelection(BUILDING_SELECTION, b);
					selectionPushed=true;
					return;
				}
			}
		// then for unit
		if (game.mouseUnit)
		{
			// a unit is selected:
			setSelection(UNIT_SELECTION, game.mouseUnit);
			selectionPushed = true;
			// handle dump of unit characteristics
			if ((SDL_GetModState() & KMOD_SHIFT) != 0)
			{
				OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("unit.dump.txt"));
				if (stream->isEndOfStream())
				{
					std::cerr << "Can't dump unit to file unit.dump.txt" << std::endl;
				}
				else
				{
					std::cerr << "Dump unit " << game.mouseUnit->gid << " memory" << std::endl;
					game.mouseUnit->save(stream);
					game.mouseUnit->saveCrossRef(stream);
				}
				delete stream;
			}
		}
		else 
		{
			// then for building
			Uint16 gbid=game.map.getBuilding(mapX, mapY);
			if (gbid != NOGBID)
			{
				int buildingTeam=Building::GIDtoTeam(gbid);
				// we can select for view buildings that are in shared vision
				if ((buildingTeam==localTeamNo)
					|| game.map.isFOWDiscovered(mapX, mapY, localTeam->me)
					|| (game.map.isMapDiscovered(mapX, mapY, localTeam->me)
						&& (game.teams[buildingTeam]->allies&(1<<localTeamNo))))
				{
					setSelection(BUILDING_SELECTION, gbid);
					selectionPushed=true;
					showUnitWorkingToBuilding=true;
					// handle dump of building characteristics
					if ((SDL_GetModState() & KMOD_SHIFT) != 0)
					{
						OutputStream *stream = new TextOutputStream(Toolkit::getFileManager()->openOutputStreamBackend("building.dump.txt"));
						if (stream->isEndOfStream())
						{
							std::cerr << "Can't dump unit to file building.dump.txt" << std::endl;
						}
						else
						{
							std::cerr << "Dump building " << selection.building->gid << " memory" << std::endl;
							selection.building->save(stream);
							selection.building->saveCrossRef(stream);
						}
						delete stream;
					}
				}
			}
			else
			{
				// and ressource
				if (game.map.isRessource(mapX, mapY) && game.map.isMapDiscovered(mapX, mapY, localTeam->me))
				{
					setSelection(RESSOURCE_SELECTION, mapY*game.map.getW()+mapX);
					selectionPushed=true;
				}
				else
				{
					if (selectionMode == RESSOURCE_SELECTION)
						clearSelection();
				}
			}
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
			minimapMouseToPos(mx, my, &markx, &marky, false);
			orderQueue.push_back(new MapMarkOrder(localTeamNo, markx, marky));
			globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_NORMAL);
			putMark = false;
		}
		else
		{
			miniMapPushed=true;
			minimapMouseToPos(mx, my, &viewportX, &viewportY, true);
		}
	}
	else if (my<128+32)
	{
		int dm=mx/32;
		if (!((1<<dm) & hiddenGUIElements))
		{
			displayMode=DisplayMode(dm);
			clearSelection();
		}
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		assert (selBuild);
		if (selBuild->owner->teamNumber!=localTeamNo)
			return;
		int ypos = YPOS_BASE_BUILDING +  YOFFSET_NAME + YOFFSET_ICON + YOFFSET_B_SEP;
		BuildingType *buildingType = selBuild->type;
	
		// working bar
		if (selBuild->type->maxUnitWorking)
		{
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& my>ypos+YOFFSET_TEXT_BAR
				&& my<ypos+YOFFSET_TEXT_BAR+16
				&& selBuild->buildingState==Building::ALIVE)
			{
				int nbReq;
				if (mx<18)
				{
					if(selBuild->maxUnitWorkingLocal>0)
					{
						nbReq=(selBuild->maxUnitWorkingLocal-=1);
						orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
					}
				}
				else if (mx<128-18)
				{
					nbReq=selBuild->maxUnitWorkingLocal=((mx-18)*MAX_UNIT_WORKING)/92;
					orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
				}
				else
				{
					if(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)
					{
						nbReq=(selBuild->maxUnitWorkingLocal+=1);
						orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
					}
				}
			}
			ypos += YOFFSET_BAR + YOFFSET_B_SEP;
		}
		
		// flag range bar
		if (buildingType->defaultUnitStayRange)
		{
			if (((selBuild->owner->allies)&(1<<localTeamNo))
				&& (my>ypos+YOFFSET_TEXT_BAR)
				&& (my<ypos+YOFFSET_TEXT_BAR+16))
			{
				int nbReq;
				if (mx<18)
				{
					if(selBuild->unitStayRangeLocal>0)
					{
						nbReq=(selBuild->unitStayRangeLocal-=1);
						orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
					}
				}
				else if (mx<128-18)
				{
					nbReq=selBuild->unitStayRangeLocal=((mx-18)*(unsigned)selBuild->type->maxUnitStayRange)/92;
					orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
				}
				else
				{
					// TODO : check in orderQueue to avoid useless orders.
					if (selBuild->unitStayRangeLocal<(unsigned)selBuild->type->maxUnitStayRange)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
					}
				}
			}
			ypos += YOFFSET_BAR+YOFFSET_B_SEP;
		}
		
		// flags specific options:
		if (((selBuild->owner->allies)&(1<<localTeamNo))
			&& mx>10
			&& mx<22)
		{
			ypos+=YOFFSET_B_SEP+YOFFSET_TEXT_PARA;
			
			// cleared ressources for clearing flags:
			if (buildingType->type == "clearingflag")
			{
				int j=0;
				for (int i=0; i<BASIC_COUNT; i++)
					if (i!=STONE)
					{
						if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
						{
							selBuild->clearingRessourcesLocal[i]=!selBuild->clearingRessourcesLocal[i];
							orderQueue.push_back(new OrderModifyClearingFlag(selBuild->gid, selBuild->clearingRessourcesLocal));
						}
						
						ypos+=YOFFSET_TEXT_PARA;
						j++;
					}
			}
			
			if (buildingType->type == "warflag")
				for (int i=0; i<4; i++)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->minLevelToFlagLocal=i;
						orderQueue.push_back(new OrderModifyWarFlag(selBuild->gid, selBuild->minLevelToFlagLocal));
					}
					
					ypos+=YOFFSET_TEXT_PARA;
				}
		}
		
		if (buildingType->armor)
			ypos+=YOFFSET_TEXT_LINE;
		if (buildingType->maxUnitInside)
			ypos += YOFFSET_INFOS;
		if (buildingType->shootDamage)
			ypos += YOFFSET_TOWER;
		ypos += YOFFSET_B_SEP;

		if (selBuild->type->canExchange && ((selBuild->owner->allies)&(1<<localTeamNo)))
		{
			int startY = ypos+YOFFSET_TEXT_PARA;
			int endY = startY+HAPPYNESS_COUNT*YOFFSET_TEXT_PARA;
			if ((my>startY) && (my<endY))
			{
				int r = (my-startY)/YOFFSET_TEXT_PARA;
				if ((mx>92) && (mx<104))
				{
					if (selBuild->receiveRessourceMask & (1<<r))
					{
						selBuild->receiveRessourceMaskLocal &= ~(1<<r);
					}
					else
					{
						selBuild->receiveRessourceMaskLocal |= (1<<r);
						selBuild->sendRessourceMaskLocal &= ~(1<<r);
					}
					orderQueue.push_back(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal));
				}

				if ((mx>110) && (mx<122))
				{
					if (selBuild->sendRessourceMask & (1<<r))
					{
						selBuild->sendRessourceMaskLocal &= ~(1<<r);
					}
					else
					{
						selBuild->receiveRessourceMaskLocal &= ~(1<<r);
						selBuild->sendRessourceMaskLocal |= (1<<r);
					}
					orderQueue.push_back(new OrderModifyExchange(selBuild->gid, selBuild->receiveRessourceMaskLocal, selBuild->sendRessourceMaskLocal));
				}
			}
		}

		if (selBuild->type->unitProductionTime)
		{
			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				if ((my>256+90+(i*20)+12)&&(my<256+90+(i*20)+16+12))
				{
					if (mx<18)
					{
						if (selBuild->ratioLocal[i]>0)
						{
							selBuild->ratioLocal[i]--;
							orderQueue.push_back(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal));
						}
					}
					else if (mx<128-18)
					{
						selBuild->ratioLocal[i]=((mx-18)*MAX_RATIO_RANGE)/92;
						orderQueue.push_back(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal));
					}
					else
					{
						if (selBuild->ratioLocal[i]<MAX_RATIO_RANGE)
						{
							selBuild->ratioLocal[i]++;
							orderQueue.push_back(new OrderModifySwarm(selBuild->gid, selBuild->ratioLocal));
						}
					}
					//printf("ratioLocal[%d]=%d\n", i, selBuild->ratioLocal[i]);
				}
			}
		}

		if ((my>globalContainer->gfx->getH()-48) && (my<globalContainer->gfx->getH()-32))
		{
			BuildingType *buildingType=selBuild->type;
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				orderQueue.push_back(new OrderCancelConstruction(selBuild->gid));
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				orderQueue.push_back(new OrderCancelConstruction(selBuild->gid));
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
			{
				if (selBuild->hp<buildingType->hpMax)
				{
					// repair
					if (selBuild->type->regenerationSpeed==0 && selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && (localTeam->maxBuildLevel()>=buildingType->level))
						orderQueue.push_back(new OrderConstruction(selBuild->gid));
				}
				else if (buildingType->nextLevel!=-1)
				{
					// upgrade
					if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
						orderQueue.push_back(new OrderConstruction(selBuild->gid));
				}
			}
		}

		if ((my>globalContainer->gfx->getH()-24) && (my<globalContainer->gfx->getH()-8))
		{
			if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
			{
				orderQueue.push_back(new OrderCancelDelete(selBuild->gid));
			}
			else if (selBuild->buildingState==Building::ALIVE)
			{
				orderQueue.push_back(new OrderDelete(selBuild->gid));
			}
		}
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		Unit* selUnit=selection.unit;
		assert(selUnit);
		selUnit->verbose=!selUnit->verbose;
		printf("unit gid=(%d) verbose %d\n", selUnit->gid, selUnit->verbose);
		printf(" pos=(%d, %d)\n", selUnit->posX, selUnit->posY);
		printf(" needToRecheckMedical=%d\n", selUnit->needToRecheckMedical);
		printf(" medical=%d\n", selUnit->medical);
		printf(" activity=%d\n", selUnit->activity);
		printf(" displacement=%d\n", selUnit->displacement);
		printf(" movement=%d\n", selUnit->movement);
		printf(" action=%d\n", selUnit->action);

		if (selUnit->attachedBuilding)
			printf(" attachedBuilding bgid=%d\n", selUnit->attachedBuilding->gid);
		else
			printf(" attachedBuilding NULL\n");
		printf(" destinationPurprose=%d\n", selUnit->destinationPurprose);
		printf(" subscribed=%d\n", selUnit->subscribed);
		printf(" caryedRessource=%d\n", selUnit->caryedRessource);
	}
	else if (displayMode==BUILDING_VIEW)
	{
		int xNum=mx>>6;
		int yNum=(my-YPOS_BASE_BUILDING)/46;
		int id=yNum*2+xNum;
		if (id<(int)buildingsChoiceName.size())
			if (buildingsChoiceState[id])
				setSelection(TOOL_SELECTION, (void *)buildingsChoiceName[id].c_str());
	}
	else if (displayMode==FLAG_VIEW)
	{
		my -= YPOS_BASE_FLAG;
		if (my > YOFFSET_BRUSH)
		{
			// change the brush type (forbidden, guard) if necessary
			if (my < YOFFSET_BRUSH+40)
				brushType = (BrushType)(mx>>6);
			// anyway, update the tool
			brush.handleClick(mx, my-YOFFSET_BRUSH-40);
			// set the selection
			setSelection(BRUSH_SELECTION);
		}
		else
		{
			int xNum=mx / (128/3);
			int yNum=my / 46;
			int id=yNum*3+xNum;
			if (id<(int)flagsChoiceName.size())
				if (flagsChoiceState[id])
					setSelection(TOOL_SELECTION, (void*)flagsChoiceName[id].c_str());
		}
	}
}

Order *GameGUI::getOrder(void)
{
	Order *order;
	if (orderQueue.size()==0)
		order=new NullOrder();
	else
	{
		order=orderQueue.front();
		orderQueue.pop_front();
	}
	order->gameCheckSum=game.checkSum();
	return order;
}

void GameGUI::drawPanelButtons(int pos)
{
	// draw buttons
	if (!(hiddenGUIElements & HIDABLE_BUILDINGS_LIST))
	{
		if (((selectionMode==NO_SELECTION) || (selectionMode==TOOL_SELECTION)) && (displayMode==BUILDING_VIEW))
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128, pos, globalContainer->gamegui, 1);
		else
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128, pos, globalContainer->gamegui, 0);
	}

	if (!(hiddenGUIElements & HIDABLE_FLAGS_LIST))
	{
		if (((selectionMode==NO_SELECTION) || (selectionMode==TOOL_SELECTION) || (selectionMode==BRUSH_SELECTION)) && (displayMode==FLAG_VIEW))
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-96, pos, globalContainer->gamegui, 1);
		else
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-96, pos, globalContainer->gamegui, 0);
	}

	if (!(hiddenGUIElements & HIDABLE_TEXT_STAT))
	{
		if ((selectionMode==NO_SELECTION) && (displayMode==STAT_TEXT_VIEW))
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-64, pos, globalContainer->gamegui, 3);
		else
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-64, pos, globalContainer->gamegui, 2);
	}

	if (!(hiddenGUIElements & HIDABLE_GFX_STAT))
	{
		if ((selectionMode==NO_SELECTION) && (displayMode==STAT_GRAPH_VIEW))
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32, pos, globalContainer->gamegui, 5);
		else
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32, pos, globalContainer->gamegui, 4);
	}

	// draw decoration
}

void GameGUI::drawChoice(int pos, std::vector<std::string> &types, std::vector<bool> &states, unsigned numberPerLine)
{
	assert(numberPerLine >= 2);
	assert(numberPerLine <= 3);
	int sel=-1;
	int width = (128/numberPerLine);
	size_t i;

	for (i=0; i<types.size(); i++)
	{
		std::string &type = types[i];

		if ((selectionMode==TOOL_SELECTION) && (type == (const char *)selection.build))
			sel = i;

		if (states[i])
		{
			BuildingType *bt = globalContainer->buildingsTypes.getByType(type.c_str(), 0, false);
			assert(bt);
			int imgid = bt->miniSpriteImage;
			int x, y;

			x=((i % numberPerLine)*width)+globalContainer->gfx->getW()-128;
			y=((i / numberPerLine)*46)+128+32;
			globalContainer->gfx->setClipRect(x, y, 64, 46);

			Sprite *buildingSprite;
			if (imgid >= 0)
			{
				buildingSprite = bt->miniSpritePtr;
			}
			else
			{
				buildingSprite = bt->gameSpritePtr;
				imgid = bt->gameSpriteImage;
			}
			
			int decX = (width-buildingSprite->getW(imgid))>>1;
			int decY = (46-buildingSprite->getW(imgid))>>1;

			buildingSprite->setBaseColor(localTeam->colorR, localTeam->colorG, localTeam->colorB);
			globalContainer->gfx->drawSprite(x+decX, y+decY, buildingSprite, imgid);
		}
	}
	int count = i;

	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128);

	// draw selection if needed
	if (selectionMode == TOOL_SELECTION)
	{
		assert(sel>=0);
		int x=((sel  % numberPerLine)*width)+globalContainer->gfx->getW()-128;
		int y=((sel / numberPerLine)*46)+128+32;
		if (numberPerLine == 2)
			globalContainer->gfx->drawSprite(x+4, y+1, globalContainer->gamegui, 8);
		else
			globalContainer->gfx->drawSprite(x+((width-40)>>1), y+4, globalContainer->gamegui, 23);
	}

	// draw infos
	if (mouseX>globalContainer->gfx->getW()-128)
	{
		if (mouseY>pos)
		{
			int xNum=(mouseX-globalContainer->gfx->getW()+128) / width;
			int yNum=(mouseY-pos)/46;
			int id=yNum*numberPerLine+xNum;
			if (id<count)
			{
				std::string &type = types[id];
				if (states[id])
				{
					int buildingInfoStart=globalContainer->gfx->getH()-50;

					int typeId = IntBuildingType::shortNumberFromType(type);
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[Building name]", typeId);
					
					globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 128, 128, 128));
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-20, "[Building short explanation]", typeId);
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-8, "[Building short explanation 2]", typeId);
					globalContainer->gfx->popFontStyle(globalContainer->littleFont);
					BuildingType *bt = globalContainer->buildingsTypes.getByType(type, 0, true);
					if (bt)
					{
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, buildingInfoStart+6, globalContainer->littleFont,
							GAGCore::nsprintf("%s: %d", Toolkit::getStringTable()->getString("[Wood]"), bt->maxRessource[0]).c_str());
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, buildingInfoStart+17, globalContainer->littleFont,
							GAGCore::nsprintf("%s: %d", Toolkit::getStringTable()->getString("[Stone]"), bt->maxRessource[3]).c_str());

						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, buildingInfoStart+6, globalContainer->littleFont,
							GAGCore::nsprintf("%s: %d", Toolkit::getStringTable()->getString("[Alga]"), bt->maxRessource[4]).c_str());
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, buildingInfoStart+17, globalContainer->littleFont,
							GAGCore::nsprintf("%s: %d", Toolkit::getStringTable()->getString("[Corn]"), bt->maxRessource[1]).c_str());

						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, buildingInfoStart+28, globalContainer->littleFont,
							GAGCore::nsprintf("%s: %d", Toolkit::getStringTable()->getString("[Papyrus]"), bt->maxRessource[2]).c_str());
					}
				}
			}
		}
	}
}


void GameGUI::drawUnitInfos(void)
{
	Unit* selUnit=selection.unit;
	assert(selUnit);
	int ypos = YPOS_BASE_UNIT;
	Uint8 r, g, b;

	// draw "unit" of "player"
	std::string title;
	title += Toolkit::getStringTable()->getString("[Unit type]", selUnit->typeNum);
	title += " (";

	const char *textT=selUnit->owner->getFirstPlayerName();
	if (!textT)
		textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
	title += textT;
	title += ")";

	if (localTeam->teamNumber == selUnit->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selUnit->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-128+((128-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos+5, globalContainer->littleFont, title.c_str());
	globalContainer->gfx->popFontStyle(globalContainer->littleFont);

	ypos += YOFFSET_NAME;

	// draw unit's image
	Unit* unit=selUnit;
	int imgid;
	UnitType *ut=unit->race->getUnitType(unit->typeNum, 0);
	assert(unit->action>=0);
	assert(unit->action<NB_MOVE);
	imgid=ut->startImage[unit->action];

	int dir=unit->direction;
	int delta=unit->delta;
	assert(dir>=0);
	assert(dir<9);
	assert(delta>=0);
	assert(delta<256);
	if (dir==8)
	{
		imgid+=8*(delta>>5);
	}
	else
	{
		imgid+=8*dir;
		imgid+=(delta>>5);
	}

	Sprite *unitSprite=globalContainer->units;
	unitSprite->setBaseColor(unit->owner->colorR, unit->owner->colorG, unit->owner->colorB);
	int decX = (32-unitSprite->getW(imgid))>>1;
	int decY = (32-unitSprite->getH(imgid))>>1;
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+14+decX, ypos+7+4+decY, unitSprite, imgid);
	
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+2, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s:", Toolkit::getStringTable()->getString("[hp]")).c_str());

	if (selUnit->hp<=selUnit->trigHP)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, GAGCore::nsprintf("%d/%d", selUnit->hp, selUnit->performance[HP]).c_str());
	globalContainer->gfx->popFontStyle(globalContainer->littleFont);

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos+YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, GAGCore::nsprintf("%s:", Toolkit::getStringTable()->getString("[food]")).c_str());

	// draw food
	if (selUnit->isUnitHungry())
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+2*YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, GAGCore::nsprintf("%2.0f %% (%d)", ((float)selUnit->hungry*100.0f)/(float)Unit::HUNGRY_MAX, selUnit->fruitCount).c_str());
	globalContainer->gfx->popFontStyle(globalContainer->littleFont);

	ypos += YOFFSET_ICON+10;

	if (selUnit->performance[HARVEST])
	{
		if (selUnit->caryedRessource>=0)
		{
			const RessourceType* r = globalContainer->ressourcesTypes.get(selUnit->caryedRessource);
			unsigned resImg = r->gfxId + r->sizesCount - 1;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[carry]"));
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-32-8, ypos, globalContainer->ressources, resImg);
		}
		else
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos+8, globalContainer->littleFont, Toolkit::getStringTable()->getString("[don't carry anything]"));
		}
	}
	ypos += YOFFSET_CARYING+10;

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s : %d", Toolkit::getStringTable()->getString("[current speed]"), selUnit->speed).c_str());
	ypos += YOFFSET_TEXT_PARA+10;

	if (selUnit->typeNum!=EXPLORER)
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s:", Toolkit::getStringTable()->getString("[levels]"), selUnit->speed).c_str());
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->performance[WALK])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d) : %d", Toolkit::getStringTable()->getString("[Walk]"), 1+selUnit->level[WALK], selUnit->performance[WALK]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[SWIM])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d) : %d", Toolkit::getStringTable()->getString("[Swim]"), selUnit->level[SWIM], selUnit->performance[SWIM]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[BUILD])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d) : %d", Toolkit::getStringTable()->getString("[Build]"), 1+selUnit->level[BUILD], selUnit->performance[BUILD]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[HARVEST])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d) : %d", Toolkit::getStringTable()->getString("[Harvest]"), 1+selUnit->level[HARVEST], selUnit->performance[HARVEST]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_SPEED])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d) : %d", Toolkit::getStringTable()->getString("[At. speed]"), 1+selUnit->level[ATTACK_SPEED], selUnit->performance[ATTACK_SPEED]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_STRENGTH])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d) : %d", Toolkit::getStringTable()->getString("[At. strength]"), 1+selUnit->level[ATTACK_STRENGTH], selUnit->performance[ATTACK_STRENGTH]).c_str());

}

void GameGUI::drawValueAlignedRight(int y, int v)
{
	std::string s = GAGCore::nsprintf("%d", v);
	int len = globalContainer->littleFont->getStringWidth(s.c_str());
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-len-2, y, globalContainer->littleFont, s.c_str());
}

void GameGUI::drawCosts(int ressources[BASIC_COUNT], Font *font)
{
	for (int i=0; i<BASIC_COUNT; i++)
	{
		int y = i>>1;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+(i&0x1)*64, 256+172-42+y*12,
			font,
			GAGCore::nsprintf("%s: %d", Toolkit::getStringTable()->getString("[ressources]", i), ressources[i]).c_str());
	}
}

void GameGUI::drawBuildingInfos(void)
{
	Building* selBuild = selection.building;
	assert(selBuild);
	BuildingType *buildingType = selBuild->type;
	int ypos = YPOS_BASE_BUILDING;
	Uint8 r, g, b;
	unsigned unitInsideBarYDec = 0;

	// draw "building" of "player"
	std::string title;
	title += Toolkit::getStringTable()->getString("[Building name]", buildingType->shortTypeNum);
	{
		title += " (";
		const char *textT=selBuild->owner->getFirstPlayerName();
		if (!textT)
			textT=Toolkit::getStringTable()->getString("[Uncontrolled]");
		title += textT;
		title += ")";
	}

	if (localTeam->teamNumber == selBuild->owner->teamNumber)
		{ r=160; g=160; b=255; }
	else if (localTeam->allies & selBuild->owner->me)
		{ r=255; g=210; b=20; }
	else
		{ r=255; g=50; b=50; }

	globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-128+((128-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos, globalContainer->littleFont, title.c_str());
	globalContainer->gfx->popFontStyle(globalContainer->littleFont);

	// building text
	title = "";
	if ((buildingType->nextLevel>=0) ||  (buildingType->prevLevel>=0))
	{
		const char *textT = Toolkit::getStringTable()->getString("[level]");
		title += GAGCore::nsprintf("%s %d", textT, buildingType->level+1);
	}
	if (buildingType->isBuildingSite)
	{
		title += " (";
		title += Toolkit::getStringTable()->getString("[building site]");
		title += ")";
	}
	if (buildingType->prestige)
	{
		title += " - ";
		title += Toolkit::getStringTable()->getString("[Prestige]");
	}
	titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	titlePos = globalContainer->gfx->getW()-128+((128-titleLen)>>1);
	globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 200, 200, 200));
	globalContainer->gfx->drawString(titlePos, ypos+YOFFSET_TEXT_PARA-1, globalContainer->littleFont, title.c_str());
	globalContainer->gfx->popFontStyle(globalContainer->littleFont);

	ypos += YOFFSET_NAME;


	// building icon
	Sprite *miniSprite;
	int imgid;
	if (buildingType->miniSpriteImage >= 0)
	{
		miniSprite = buildingType->miniSpritePtr;
		imgid = buildingType->miniSpriteImage;
	}
	else
	{
		miniSprite = buildingType->gameSpritePtr;
		imgid = buildingType->gameSpriteImage;
	}
	int dx = (56-miniSprite->getW(imgid))>>1;
	int dy = (46-miniSprite->getH(imgid))>>1;
	miniSprite->setBaseColor(selBuild->owner->colorR, selBuild->owner->colorG, selBuild->owner->colorB);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+2+dx, ypos+4+dy, miniSprite, imgid);
	globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+2, ypos+4, globalContainer->gamegui, 18);

	// draw HP
	if (buildingType->hpMax)
	{
		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[hp]"));
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);

		if (selBuild->hp <= buildingType->hpMax/5)
			{ r=255; g=0; b=0; }
		else
			{ r=0; g=255; b=0; }

		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, r, g, b));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, GAGCore::nsprintf("%d/%d", selBuild->hp, buildingType->hpMax).c_str());
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);
	}

	// inside
	if (buildingType->maxUnitInside && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE, globalContainer->littleFont, Toolkit::getStringTable()->getString("[inside]"));
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);
		if (selBuild->buildingState==Building::ALIVE)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, GAGCore::nsprintf("%d/%d", selBuild->unitsInside.size(), selBuild->maxUnitInside).c_str());
		}
		else
		{
			if (selBuild->unitsInside.size()>1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, GAGCore::nsprintf("%s%d",
					Toolkit::getStringTable()->getString("[Still (i)]"),
					selBuild->unitsInside.size()).c_str());
			}
			else if (selBuild->unitsInside.size()==1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont,
					Toolkit::getStringTable()->getString("[Still one]") );
			}
		}
	}

	// it is a unit ranged attractor (aka flag)
	if (buildingType->defaultUnitStayRange && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		// get flag stat
		int goingTo, onSpot;
		selBuild->computeFlagStatLocal(&goingTo, &onSpot);
		// display flag stat
		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s", Toolkit::getStringTable()->getString("[In way]")).c_str());
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, GAGCore::nsprintf("%d", goingTo).c_str());
		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE,
		globalContainer->littleFont, GAGCore::nsprintf("%s", Toolkit::getStringTable()->getString("[On the spot]")).c_str());
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, GAGCore::nsprintf("%d", onSpot).c_str());
	}

	ypos += YOFFSET_ICON+YOFFSET_B_SEP;

	// working bar
	if (buildingType->maxUnitWorking)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			if (selBuild->buildingState==Building::ALIVE)
			{
				const char *working = Toolkit::getStringTable()->getString("[working]");
				const int len = globalContainer->littleFont->getStringWidth(working)+4;
				globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, working);
				globalContainer->gfx->popFontStyle(globalContainer->littleFont);
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+len, ypos, globalContainer->littleFont, GAGCore::nsprintf("%d/%d", (int)selBuild->unitsWorking.size(), selBuild->maxUnitWorkingLocal).c_str());
				drawScrollBox(globalContainer->gfx->getW()-128, ypos+YOFFSET_TEXT_BAR, selBuild->maxUnitWorkingLocal, selBuild->maxUnitWorkingLocal, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
			}
			else
			{
				if (selBuild->unitsWorking.size()>1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s%d%s",
						Toolkit::getStringTable()->getString("[still (w)]"),
						selBuild->unitsWorking.size(),
						Toolkit::getStringTable()->getString("[units working]")).c_str());
				}
				else if (selBuild->unitsWorking.size()==1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont,
						Toolkit::getStringTable()->getString("[still one unit working]") );
				}
			}
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}
	
	// flag range bar
	if (buildingType->defaultUnitStayRange)
	{
		if ((selBuild->owner->allies)&(1<<localTeamNo))
		{
			const char *range = Toolkit::getStringTable()->getString("[range]");
			const int len = globalContainer->littleFont->getStringWidth(range)+4;
			globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, range);
			globalContainer->gfx->popFontStyle(globalContainer->littleFont);
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+len, ypos, globalContainer->littleFont, GAGCore::nsprintf("%d", selBuild->unitStayRange).c_str());
			drawScrollBox(globalContainer->gfx->getW()-128, ypos+YOFFSET_TEXT_BAR, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, selBuild->type->maxUnitStayRange);
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}
	
	// cleared ressources for clearing flags:
	if ((buildingType->type == "clearingflag") && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Clearing:]"));
		ypos += YOFFSET_TEXT_PARA;
		int j=0;
		for (int i=0; i<BASIC_COUNT; i++)
			if (i!=STONE)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+28, ypos, globalContainer->littleFont,
					Toolkit::getStringTable()->getString("[ressources]", i));
				int spriteId;
				if (selBuild->clearingRessourcesLocal[i])
					spriteId=20;
				else
					spriteId=19;
				globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+10, ypos+2, globalContainer->gamegui, spriteId);
				
				ypos+=YOFFSET_TEXT_PARA;
				j++;
			}
	}
	
	// min war level for war flags:
	if ((buildingType->type == "warflag") && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		ypos += YOFFSET_B_SEP;
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont,
			Toolkit::getStringTable()->getString("[Min required level:]"));
		ypos += YOFFSET_TEXT_PARA;
		for (int i=0; i<4; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+28, ypos, globalContainer->littleFont, 1+i);
			int spriteId;
			if (i==selBuild->minLevelToFlag)
				spriteId=20;
			else
				spriteId=19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+10, ypos+2, globalContainer->gamegui, spriteId);
			
			ypos+=YOFFSET_TEXT_PARA;
		}
	}

	// other infos
	if (buildingType->armor)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s : %d", Toolkit::getStringTable()->getString("[armor]"), buildingType->armor).c_str());
		ypos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	if (buildingType->shootDamage)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+1, globalContainer->littleFont, GAGCore::nsprintf("%s : %d", Toolkit::getStringTable()->getString("[damage]"), buildingType->shootDamage).c_str());
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+12, globalContainer->littleFont, GAGCore::nsprintf("%s : %d", Toolkit::getStringTable()->getString("[range]"), buildingType->shootingRange).c_str());
		ypos += YOFFSET_TOWER;
	}

	// There is unit inside, show time to leave
	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// we select food buildings, heal buildings, and upgrade buildings:
		int maxTimeTo=0;
		if (buildingType->timeToFeedUnit)
			maxTimeTo=buildingType->timeToFeedUnit;
		else if (buildingType->timeToHealUnit)
			maxTimeTo=buildingType->timeToHealUnit;
		else
			for (int i=0; i<NB_ABILITY; i++)
				if (buildingType->upgradeTime[i])
					maxTimeTo=std::max(maxTimeTo, buildingType->upgradeTime[i]);
		if (maxTimeTo)
		{
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, ypos, 128, 7, 168, 150, 90);
			for (std::list<Unit *>::iterator it=selBuild->unitsInside.begin(); it!=selBuild->unitsInside.end(); ++it)
			{
				Unit *u=*it;
				assert(u);
				if (u->displacement==Unit::DIS_INSIDE)
				{
					int dividend=-u->insideTimeout*128+128-u->delta/2;
					int divisor=1+maxTimeTo;
					int left=dividend/divisor;
					int alpha=((dividend%divisor)*255)/divisor;
					
					if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
					{
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left, ypos, 7, 63, 111, 149);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1, ypos, 7, 17, 30, 64);
					}
					else
					{
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-2, ypos, 7, 17, 30, 64, alpha);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1, ypos, 7, 17, 30, 64);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+2, ypos, 7, 17, 30, 64, 255-alpha);
						
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left-1, ypos, 7, 63, 111, 149, alpha);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left, ypos, 7, 63, 111, 149);
						globalContainer->gfx->drawVertLine(globalContainer->gfx->getW()-left+1, ypos, 7, 63, 111, 149, 255-alpha);
					}
				}
			}
			
			ypos += YOFFSET_PROGRESS_BAR;
			unitInsideBarYDec = YOFFSET_PROGRESS_BAR;
		}
	}
	
	ypos += YOFFSET_B_SEP;

	// exchange building
	if (buildingType->canExchange && ((selBuild->owner->sharedVisionExchange)&(1<<localTeamNo)))
	{
		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 185, 195, 121));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[market]"));
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36-3, ypos+1, globalContainer->gamegui, EXCHANGE_BUILDING_ICONS);
		ypos += YOFFSET_TEXT_PARA;
		for (unsigned i=0; i<HAPPYNESS_COUNT; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, GAGCore::nsprintf("%s (%d/%d)", Toolkit::getStringTable()->getString("[ressources]", i+HAPPYNESS_BASE), selBuild->ressources[i+HAPPYNESS_BASE], buildingType->maxRessource[i+HAPPYNESS_BASE]).c_str());

			int inId, outId;
			if (selBuild->receiveRessourceMaskLocal & (1<<i))
				inId = 20;
			else
				inId = 19;
			if (selBuild->sendRessourceMaskLocal & (1<<i))
				outId = 20;
			else
				outId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36, ypos+2, globalContainer->gamegui, inId);
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-18, ypos+2, globalContainer->gamegui, outId);

			ypos += YOFFSET_TEXT_PARA;
		}
	}

	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		if (buildingType->unitProductionTime) // swarm
		{
			int left=(selBuild->productionTimeout*128)/buildingType->unitProductionTime;
			int elapsed=128-left;
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 256+65+12, elapsed, 7, 100, 100, 255);
			globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128+elapsed, 256+65+12, left, 7, 128, 128, 128);

			for (int i=0; i<NB_UNIT_TYPE; i++)
			{
				drawScrollBox(globalContainer->gfx->getW()-128, 256+90+(i*20)+12, selBuild->ratio[i], selBuild->ratioLocal[i], 0, MAX_RATIO_RANGE);
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+24, 256+90+(i*20)+12, globalContainer->littleFont, Toolkit::getStringTable()->getString("[Unit type]", i));
			}
		}
		
		// ressorces for every building except exchange building
		if (!buildingType->canExchange)
		{

			// ressources in
			unsigned j = 0;
			for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
			{
				if (buildingType->maxRessource[i])
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+(j*11), globalContainer->littleFont, GAGCore::nsprintf("%s : %d/%d", Toolkit::getStringTable()->getString("[ressources]", i), selBuild->ressources[i], buildingType->maxRessource[i]).c_str());
					j++;
				}
			}
			if (buildingType->maxBullets)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+(j*11), globalContainer->littleFont, GAGCore::nsprintf("%s : %d/%d", Toolkit::getStringTable()->getString("[Bullets]"), selBuild->bullets, buildingType->maxBullets).c_str());
				j++;
			}
		}

		// repair and upgrade
		if (selBuild->constructionResultState==Building::REPAIR)
		{
			if (buildingType->isBuildingSite)
				assert(buildingType->nextLevel!=-1);
			drawBlueButton(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-48, "[cancel repair]");
		}
		else if (selBuild->constructionResultState==Building::UPGRADE)
		{
			assert(buildingType->nextLevel!=-1);
			if (buildingType->isBuildingSite)
				assert(buildingType->prevLevel!=-1);
			drawBlueButton(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-48, "[cancel upgrade]");
		}
		else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE) && !buildingType->isBuildingSite)
		{
			if (selBuild->hp<buildingType->hpMax)
			{
				// repair
				if (selBuild->type->regenerationSpeed==0 && selBuild->isHardSpaceForBuildingSite(Building::REPAIR) && (localTeam->maxBuildLevel()>=buildingType->level))
				{
					drawBlueButton(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-48, "[repair]");
					if ( mouseX>globalContainer->gfx->getW()-128+12 && mouseX<globalContainer->gfx->getW()-12
						&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
						{
							globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
							int ressources[BASIC_COUNT];
							selBuild->getRessourceCountToRepair(ressources);
							drawCosts(ressources, globalContainer->littleFont);
							globalContainer->gfx->popFontStyle(globalContainer->littleFont);
						}
				}
			}
			else if (buildingType->nextLevel!=-1)
			{
				// upgrade
				if (selBuild->isHardSpaceForBuildingSite(Building::UPGRADE) && (localTeam->maxBuildLevel()>buildingType->level))
				{
					drawBlueButton(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-48, "[upgrade]");
					if ( mouseX>globalContainer->gfx->getW()-128+12 && mouseX<globalContainer->gfx->getW()-12
						&& mouseY>globalContainer->gfx->getH()-48 && mouseY<globalContainer->gfx->getH()-48+16 )
						{
							globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 200, 200, 255));

							// We draw the ressources cost.
							int typeNum=buildingType->nextLevel;
							BuildingType *bt=globalContainer->buildingsTypes.get(typeNum);
							drawCosts(bt->maxRessource, globalContainer->littleFont);

							// We draw the new abilities:
							int blueYpos = YPOS_BASE_BUILDING + YOFFSET_NAME;

							bt=globalContainer->buildingsTypes.get(bt->nextLevel);

							if (bt->hpMax)
								drawValueAlignedRight(blueYpos+YOFFSET_TEXT_LINE, bt->hpMax);
							if (bt->maxUnitInside)
								drawValueAlignedRight(blueYpos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, bt->maxUnitInside);
							blueYpos += YOFFSET_ICON+YOFFSET_B_SEP;

							if (buildingType->maxUnitWorking)
								blueYpos += YOFFSET_BAR+YOFFSET_B_SEP;

							if (bt->armor)
							{
								if (!buildingType->armor)
									globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, blueYpos-1, globalContainer->littleFont, Toolkit::getStringTable()->getString("[armor]"));
								drawValueAlignedRight(blueYpos-1, bt->armor);
								blueYpos+=YOFFSET_TEXT_LINE;
							}
							if (buildingType->maxUnitInside)
								blueYpos += YOFFSET_INFOS;
							if (bt->shootDamage)
							{
								drawValueAlignedRight(blueYpos+1, bt->shootDamage);
								drawValueAlignedRight(blueYpos+12, bt->shootingRange);
								blueYpos += YOFFSET_TOWER;
							}
							blueYpos += unitInsideBarYDec;
							blueYpos += YOFFSET_B_SEP;

							unsigned j = 0;
							for (unsigned i=0; i<globalContainer->ressourcesTypes.size(); i++)
							{
								if (buildingType->maxRessource[i])
								{
									drawValueAlignedRight(blueYpos+(j*11), bt->maxRessource[i]);
									j++;
								}
							}
							
							if (bt->maxBullets)
							{
								drawValueAlignedRight(blueYpos+(j*11), bt->maxBullets);
								j++;
							}

							globalContainer->gfx->popFontStyle(globalContainer->littleFont);
						}
				}
			}
		}

		// building destruction
		if (selBuild->buildingState==Building::WAITING_FOR_DESTRUCTION)
		{
			drawRedButton(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-24, "[cancel destroy]");
		}
		else if (selBuild->buildingState==Building::ALIVE)
		{
			drawRedButton(globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-24, "[destroy]");
		}
	}
}

void GameGUI::drawRessourceInfos(void)
{
	const Ressource &r = game.map.getRessource(selection.ressource);
	int ypos = YPOS_BASE_RESSOURCE;
	if (r.type!=NO_RES_TYPE)
	{
		// Draw ressource name
		const std::string &ressourceName = Toolkit::getStringTable()->getString("[ressources]", r.type);
		int titleLen = globalContainer->littleFont->getStringWidth(ressourceName.c_str());
		int titlePos = globalContainer->gfx->getW()-128+((128-titleLen)>>1);
		globalContainer->gfx->drawString(titlePos, ypos+(YOFFSET_TEXT_PARA>>1), globalContainer->littleFont, ressourceName.c_str());
		ypos += 2*YOFFSET_TEXT_PARA;
		
		// Draw ressource image
		const RessourceType* rt = globalContainer->ressourcesTypes.get(r.type);
		unsigned resImg = rt->gfxId + r.variety*rt->sizesCount + r.amount;
		if (!rt->eternal)
			resImg--;
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+16, ypos, globalContainer->ressources, resImg);
		
		// Draw ressource count
		if (rt->granular)
		{
			int sizesCount=rt->sizesCount;
			int amount=r.amount;
			const std::string amountS = GAGCore::nsprintf("%d/%d", amount, sizesCount);
			int amountSH = globalContainer->littleFont->getStringHeight(amountS.c_str());
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-64, ypos+((32-amountSH)>>1), globalContainer->littleFont, amountS.c_str());
		}
	}
	else
	{
		clearSelection();
	}
}

void GameGUI::drawPanel(void)
{
	// ensure we have a valid selection and associate pointers
	checkSelection();

	// set the clipping rectangle
	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128);

	// draw menu background, black if low speed graphics, transparent otherwise
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(globalContainer->gfx->getW()-128, 128, 128, globalContainer->gfx->getH()-128, 0, 0, 40, 180);

	// draw the buttons in the panel
	drawPanelButtons(128);

	if (selectionMode==BUILDING_SELECTION)
	{
		drawBuildingInfos();
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		drawUnitInfos();
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		drawRessourceInfos();
	}
	else if (displayMode==BUILDING_VIEW)
	{
		drawChoice(YPOS_BASE_BUILDING, buildingsChoiceName, buildingsChoiceState);
	}
	else if (displayMode==FLAG_VIEW)
	{
		// draw flags
		drawChoice(YPOS_BASE_FLAG, flagsChoiceName, flagsChoiceState, 3);
		// draw choice of area
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-112, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 13);
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-48, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 14);
		if (brush.getType() != BrushTool::MODE_NONE)
		{
			int decX = (brushType == GUARD_AREA_BRUSH) ? 80 : 16;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+decX, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 22);
		}
		// draw brush
		brush.draw(globalContainer->gfx->getW()-128, YPOS_BASE_FLAG+YOFFSET_BRUSH+40);
		// draw brush help text
		if ((mouseX>globalContainer->gfx->getW()-128) && (mouseY>YPOS_BASE_FLAG+YOFFSET_BRUSH))
		{
			int buildingInfoStart = globalContainer->gfx->getH()-50;
			if (mouseY<YPOS_BASE_FLAG+YOFFSET_BRUSH+40)
			{
				if (mouseX < globalContainer->gfx->getW()-64)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[forbidden area]");
				else
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[guard area]");
			}
			else
			{
				if (brushType == FORBIDDEN_BRUSH)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[forbidden area]");
				else if (brushType == GUARD_AREA_BRUSH)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[guard area]");
				else
					assert(false);
			}
		}
	}
	else if (displayMode==STAT_TEXT_VIEW)
	{
		teamStats->drawText(YPOS_BASE_STAT);
	}
	else if (displayMode==STAT_GRAPH_VIEW)
	{
		teamStats->drawStat(YPOS_BASE_STAT);
	}
}

int intSquare(int i) { return i*i; }

void GameGUI::drawTopScreenBar(void)
{
	// bar background 
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 16, 0, 0, 0);
	else
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, 16, 0, 0, 40, 180);

	// draw unit stats
	Uint8 redC[]={200, 0, 0};
	Uint8 greenC[]={0, 200, 0};
	Uint8 whiteC[]={200, 200, 200};
	Uint8 yellowC[]={200, 200, 0};
	Uint8 actC[3];
	int free, tot;

	int dec = (globalContainer->gfx->getW()-640)>>2;
	dec += 10;

	globalContainer->unitmini->setBaseColor(localTeam->colorR, localTeam->colorG, localTeam->colorB);
	for (int i=0; i<3; i++)
	{
		free = teamStats->getFreeUnits(i);
		// worker is a special case
		if (i==0)
			free -= teamStats->getWorkersNeeded();
		tot = teamStats->getTotalUnits(i);
		if (free<0)
			memcpy(actC, redC, sizeof(redC));
		else if (free>0)
			memcpy(actC, greenC, sizeof(greenC));
		else
			memcpy(actC, whiteC, sizeof(whiteC));

		globalContainer->gfx->drawSprite(dec+2, -1, globalContainer->unitmini, i);
		globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, actC[0], actC[1], actC[2]));
		globalContainer->gfx->drawString(dec+22, 0, globalContainer->littleFont, GAGCore::nsprintf("%d / %d", free, tot).c_str());
		globalContainer->gfx->popFontStyle(globalContainer->littleFont);

		dec += 70;
	}

	// draw prestige stats
	globalContainer->gfx->drawString(dec+0, 0, globalContainer->littleFont, GAGCore::nsprintf("%d / %d / %d", localTeam->prestige, game.totalPrestige, game.prestigeToReach).c_str());
	
	dec += 90;
	
	// draw unit conversion stats
	globalContainer->gfx->drawString(dec, 0, globalContainer->littleFont, GAGCore::nsprintf("+%d / -%d", localTeam->unitConversionGained, localTeam->unitConversionLost).c_str());
	
	// draw CPU load
	dec += 70;
	int cpuLoadMax=0;
	int cpuLoadMaxIndex=0;
	for (int i=0; i<SMOOTH_CPU_LOAD_WINDOW_LENGTH; i++)
		if (cpuLoadMax<smoothedCpuLoad[i])
		{
			cpuLoadMax=smoothedCpuLoad[i];
			cpuLoadMaxIndex=i;
		}
	int cpuLoad=0;
	for (int i=0; i<SMOOTH_CPU_LOAD_WINDOW_LENGTH; i++)
		if (i!=cpuLoadMaxIndex && cpuLoad<smoothedCpuLoad[i])
			cpuLoad=smoothedCpuLoad[i];
	if (cpuLoad<game.session.gameTPF-8)
		memcpy(actC, greenC, sizeof(greenC));
	else if (cpuLoad<game.session.gameTPF)
		memcpy(actC, yellowC, sizeof(yellowC));
	else
		memcpy(actC, redC, sizeof(redC));
	
	globalContainer->gfx->drawFilledRect(dec, 4, cpuLoad, 8, actC[0], actC[1], actC[2]);
	globalContainer->gfx->drawVertLine(dec, 2, 12, 200, 200, 200);
	globalContainer->gfx->drawVertLine(dec+40, 2, 12, 200, 200, 200);
	
	// draw window bar
	int pos=globalContainer->gfx->getW()-128-32;
	for (int i=0; i<pos; i+=32)
	{
		globalContainer->gfx->drawSprite(i, 16, globalContainer->gamegui, 16);
	}
	for (int i=16; i<globalContainer->gfx->getH(); i+=32)
	{
		globalContainer->gfx->drawSprite(pos+28, i, globalContainer->gamegui, 17);
	}

	// draw main menu button
	if (gameMenuScreen)
		globalContainer->gfx->drawSprite(pos, 0, globalContainer->gamegui, 7);
	else
		globalContainer->gfx->drawSprite(pos, 0, globalContainer->gamegui, 6);
}

void GameGUI::drawOverlayInfos(void)
{
	if (selectionMode==TOOL_SELECTION)
	{
		// we get the type of building
		int typeNum = globalContainer->buildingsTypes.getTypeNum(selection.build, 0, false);
		BuildingType *bt = globalContainer->buildingsTypes.get(typeNum);
		Sprite *sprite = bt->gameSpritePtr;
		
		// we translate dimensions and situation
		int tempX, tempY;
		int mapX, mapY;
		bool isRoom;
		game.map.cursorToBuildingPos(mouseX, mouseY, bt->width, bt->height, &tempX, &tempY, viewportX, viewportY);
		if (bt->isVirtual)
			isRoom = game.checkRoomForBuilding(tempX, tempY, bt, &mapX, &mapY, localTeamNo);
		else
			isRoom = game.checkHardRoomForBuilding(tempX, tempY, bt, &mapX, &mapY);
		
		// we get the screen dimensions of the building
		int batW = (bt->width)<<5;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((mapX-viewportX)&(game.map.wMask))<<5);
		int batY = (((mapY-viewportY)&(game.map.hMask))<<5)-(batH-(bt->height<<5));
		
		// we draw the building
		sprite->setBaseColor(localTeam->colorR, localTeam->colorG, localTeam->colorB);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage);
		
		if (!bt->isVirtual)
		{
			if (localTeam->noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX, batY, batX+batW-1, batY+batH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX+batW-1, batY, batX, batY+batH-1, 255, 0, 0, 127);
				
				globalContainer->gfx->pushFontStyle(globalContainer->littleFont, Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(batX, batY-12, globalContainer->littleFont, GAGCore::nsprintf("%d.%d", localTeam->noMoreBuildingSitesCountdown/40, (localTeam->noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->gfx->popFontStyle(globalContainer->littleFont);
			}
			else
			{
				if (isRoom)
					globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				
				// We look for its maximum extension size
				// we find last's level type num:
				BuildingType *lastbt=globalContainer->buildingsTypes.get(typeNum);
				int lastTypeNum=typeNum;
				int max=0;
				while (lastbt->nextLevel>=0)
				{
					lastTypeNum=lastbt->nextLevel;
					lastbt=globalContainer->buildingsTypes.get(lastTypeNum);
					if (max++>200)
					{
						printf("GameGUI: Error: nextLevel architecture is broken.\n");
						assert(false);
						break;
					}
				}
					
				int exMapX, exMapY; // ex prefix means EXtended building; the last level building type.
				bool isExtendedRoom = game.checkHardRoomForBuilding(tempX, tempY, lastbt, &exMapX, &exMapY);
				int exBatX=((exMapX-viewportX)&(game.map.wMask))<<5;
				int exBatY=((exMapY-viewportY)&(game.map.hMask))<<5;
				int exBatW=(lastbt->width)<<5;
				int exBatH=(lastbt->height)<<5;

				if (isRoom && isExtendedRoom)
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 255, 255, 127);
				else
					globalContainer->gfx->drawRect(exBatX-1, exBatY-1, exBatW+2, exBatH+2, 255, 0, 0, 127);
			}
		}

	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		brush.drawBrush(mouseX, mouseY);
	}
	else if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
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
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		int rx = selection.ressource & game.map.getMaskW();
		int ry = selection.ressource >> game.map.getShiftW();
		int px, py;
		game.map.mapCaseToDisplayable(rx, ry, &px, &py, viewportX, viewportY);
		globalContainer->gfx->drawCircle(px+16, py+16, 16, 0, 0, 190);
	}

	// draw message List
	if (game.anyPlayerWaited && game.maskAwayPlayer && game.anyPlayerWaitedTimeFor>2)
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
				globalContainer->gfx->drawString(44, 44+pnb*20, globalContainer->standardFont, GAGCore::nsprintf(Toolkit::getStringTable()->getString("[waiting for %s]"), game.players[pi2]->name).c_str());
				pnb++;
			}
			pm=pm<<1;
		}
	}
	else
	{
		int ymesg = 32;
		int yinc = 0;

		// show script text
		if (game.script.isTextShown)
		{
			std::vector<std::string> lines;
			setMultiLine(game.script.textShown, &lines);
			for (unsigned i=0; i<lines.size(); i++)
			{
				globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, lines[i].c_str());
				yinc += 20;
			}
		}

		// show script counter
		if (game.script.getMainTimer())
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-165, ymesg, globalContainer->standardFont, GAGCore::nsprintf("%d", game.script.getMainTimer()).c_str());
			yinc = std::max(yinc, 32);
		}

		ymesg += yinc+2;
		
		// display messages
		for (std::list <Message>::iterator it=messagesList.begin(); it!=messagesList.end();)
		{
			globalContainer->gfx->pushFontStyle(globalContainer->standardFont, Font::Style(Font::STYLE_BOLD, it->r, it->g, it->b, it->a));
			globalContainer->gfx->drawString(32, ymesg, globalContainer->standardFont, it->text.c_str());
			globalContainer->gfx->popFontStyle(globalContainer->standardFont);
			ymesg += 20;

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

			//int ray = Mark::DEFAULT_MARK_SHOW_TICKS-it->showTicks;
			int ray = (int)(sin((double)(it->showTicks)/(double)(Mark::DEFAULT_MARK_SHOW_TICKS)*3.141592)*Mark::DEFAULT_MARK_SHOW_TICKS/2);
			int ray2 = (int)(cos((double)(it->showTicks)/(double)(Mark::DEFAULT_MARK_SHOW_TICKS)*3.141592)*Mark::DEFAULT_MARK_SHOW_TICKS/2);
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

			Utilities::computeMinimapData(100, game.map.getW(), game.map.getH(), &mMax, &szX, &szY, &decX, &decY);
			GameUtilities::globalCoordToLocalView(&game, localTeamNo, it->x, it->y, &x, &y);
			x = (x*100)/mMax;
			y = (y*100)/mMax;
			x += globalContainer->gfx->getW()-128+14+decX;
			y += 14+decY;
			
			a = (it->showTicks*DrawableSurface::ALPHA_OPAQUE)/(Mark::DEFAULT_MARK_SHOW_TICKS);
			globalContainer->gfx->drawCircle(x, y, ray, it->r, it->g, it->b, a);
			globalContainer->gfx->drawCircle(x, y, (ray2*11)/8, it->r, it->g, it->b, a);

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

	// Draw the bar contining number of units, CPU load, etc...
	drawTopScreenBar();
}

void GameGUI::drawInGameMenu(void)
{
	gameMenuScreen->dispatchPaint();
	globalContainer->gfx->drawSurface(gameMenuScreen->decX, gameMenuScreen->decY, gameMenuScreen->getSurface());
	
	// Draw a-la-aqua drop shadows
	if ((globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX) == 0)
	{
		int x = gameMenuScreen->decX;
		int y = gameMenuScreen->decY;
		int w = gameMenuScreen->getSurface()->getW();
		int h = gameMenuScreen->getSurface()->getH();
		
		globalContainer->gfx->drawSprite(x-8, y+h, globalContainer->terrainShader, 17);
		globalContainer->gfx->drawSprite(x+w, y+h, globalContainer->terrainShader, 18);
		globalContainer->gfx->setClipRect(x, y+h, w, 16);
		for (int i=0; i<w+31; i+=32)
		{
			globalContainer->gfx->drawSprite(x+i, y+h, globalContainer->terrainShader, 16);
		}
		globalContainer->gfx->setClipRect(x-8, y, w+16, h);
		for (int i=0; i<h+31; i+=32)
		{
			globalContainer->gfx->drawSprite(x-8, y+i, globalContainer->terrainShader, 19);
			globalContainer->gfx->drawSprite(x+w, y+i, globalContainer->terrainShader, 20);
		}
	}
}

void GameGUI::drawInGameTextInput(void)
{
	typingInputScreen->decX=(globalContainer->gfx->getW()-128-492)/2;
	typingInputScreen->decY=globalContainer->gfx->getH()-typingInputScreenPos;
	typingInputScreen->dispatchPaint();
	globalContainer->gfx->drawSurface(typingInputScreen->decX, typingInputScreen->decY, typingInputScreen->getSurface());
	if (typingInputScreenInc>0)
	{
		if (typingInputScreenPos<TYPING_INPUT_MAX_POS-TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			typingInputScreenPos=TYPING_INPUT_MAX_POS;
		}
	}
	else if (typingInputScreenInc<0)
	{
		if (typingInputScreenPos>TYPING_INPUT_BASE_INC)
			typingInputScreenPos+=typingInputScreenInc;
		else
		{
			typingInputScreenInc=0;
			delete typingInputScreen;
			typingInputScreen=NULL;
		}
	}
}


void GameGUI::drawAll(int team)
{
	// draw the map
	Uint32 drawOptions =	(drawHealthFoodBar ? Game::DRAW_HEALTH_FOOD_BAR : 0) |
								(drawPathLines ?  Game::DRAW_PATH_LINE : 0) |
								((selectionMode==TOOL_SELECTION) ? Game::DRAW_BUILDING_RECT : 0) |
								Game::DRAW_AREA;
	
	if (globalContainer->settings.optionFlags & GlobalContainer::OPTION_LOW_SPEED_GFX)
	{
		globalContainer->gfx->setClipRect(0, 16, globalContainer->gfx->getW()-128, globalContainer->gfx->getH()-16);
		game.drawMap(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH(),viewportX, viewportY, localTeamNo, drawOptions);
	}
	else
	{
		globalContainer->gfx->setClipRect();
		game.drawMap(0, 0, globalContainer->gfx->getW(), globalContainer->gfx->getH(),viewportX, viewportY, localTeamNo, drawOptions);
	}

	// if paused, tint the game area
	if (gamePaused)
	{
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH(), 0, 0, 0, 127);
		const char *s = Toolkit::getStringTable()->getString("[Paused]");
		int x = (globalContainer->gfx->getW()-globalContainer->menuFont->getStringWidth(s))>>1;
		globalContainer->gfx->drawString(x, globalContainer->gfx->getH()-80, globalContainer->menuFont, s);
	}

	// draw the panel
	globalContainer->gfx->setClipRect();
	drawPanel();

	// draw the minimap
	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 0, 128, 128);
	game.drawMiniMap(globalContainer->gfx->getW()-128, 0, 128, 128, viewportX, viewportY, team);

	// draw the top bar and other infos
	globalContainer->gfx->setClipRect();
	drawOverlayInfos();

	// draw menu if any
	if (inGameMenu)
	{
		globalContainer->gfx->setClipRect();
		drawInGameMenu();
	}

	// draw input box if any
	if (typingInputScreen)
	{
		globalContainer->gfx->setClipRect();
		drawInGameTextInput();
	}
}

void GameGUI::checkWonConditions(void)
{
	if (hasEndOfGameDialogBeenShown)
		return;
		
	if (localTeam->isAlive==false)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have lost]"), true);
			hasEndOfGameDialogBeenShown=true;
		}
	}
	else if (localTeam->hasWon==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have won]"), true);
			hasEndOfGameDialogBeenShown=true;
		}
	}
	else if (game.totalPrestigeReached)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[Total prestige reached]"), true);
		}
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
					addMessage(99, 255, 242, "<%s%s> %s", Toolkit::getStringTable()->getString("[from:]"), game.players[sp]->name, mo->getText());
				else if (sp==localPlayer)
				{
					Uint32 rm=mo->recepientsMask;
					int k;
					for (k=0; k<32; k++)
						if (rm==1)
						{
							addMessage(99, 255, 242, "<%s%s> %s", Toolkit::getStringTable()->getString("[to:]"), game.players[k]->name, mo->getText());
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
			delete order;
		}
		break;
		case ORDER_DECONNECTED :
		{
			int qp=order->sender;
			addMessage(200, 200, 200, Toolkit::getStringTable()->getString("[%s has been deconnected of the game]"), game.players[qp]->name);
			game.executeOrder(order, localPlayer);
			delete order;
		}
		break;
		case ORDER_PLAYER_QUIT_GAME :
		{
			int qp=order->sender;
			addMessage(200, 200, 200, Toolkit::getStringTable()->getString("[%s has left the game]"), game.players[qp]->name);
			game.executeOrder(order, localPlayer);
		}
		break;
		
		case ORDER_MAP_MARK:
		{
			MapMarkOrder *mmo=(MapMarkOrder *)order;

			assert(game.teams[mmo->teamNumber]->teamNumber<game.session.numberOfTeam);
			if (game.teams[mmo->teamNumber]->allies & (game.teams[localTeamNo]->me))
				addMark(mmo);
		}
		break;
		case ORDER_PAUSE_GAME:
		{
			PauseGameOrder *pgo=(PauseGameOrder *)order;
			gamePaused=pgo->pause;
		}
		break;
		default:
		{
			game.executeOrder(order, localPlayer);
		}
	}
}

bool GameGUI::loadBase(const SessionInfo *initial)
{
	init();
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
		InputStream *stream = new BinaryInputStream(Toolkit::getFileManager()->openInputStreamBackend(initial->getFileName()));
		if (stream->isEndOfStream())
		{
			std::cerr << "GameGUI::loadBase() : error, can't open file " << initial->getFileName() << std::endl;
			delete stream;
			return false;
		}
		else
		{
			bool res = load(stream);
			delete stream;
			if (!res)
				return false;
			
			game.setBase(initial);
		}
	}

	return true;
}

bool GameGUI::load(GAGCore::InputStream *stream)
{
	init();

	bool result = game.load(stream);

	if (result == false)
	{
		std::cerr << "GameGUI::load : can't load game" << std::endl;
		return false;
	}

	if (!game.session.fileIsAMap)
	{
		// load gui's specific infos
		stream->readEnterSection("GameGUI");
		
		chatMask = stream->readUint32("chatMask");

		localPlayer = stream->readSint32("localPlayer");
		localTeamNo = stream->readSint32("localTeamNo");

		viewportX = stream->readSint32("viewportX");
		viewportY = stream->readSint32("viewportY");

		hiddenGUIElements = stream->readUint32("hiddenGUIElements");
		Uint32 buildingsChoiceMask = stream->readUint32("buildingsChoiceMask");
		Uint32 flagsChoiceMask = stream->readUint32("flagsChoiceMask");
		
		// invert value if hidden
		for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
		{
			int id = IntBuildingType::shortNumberFromType(buildingsChoiceName[i]);
			buildingsChoiceState[i] = ((1<<id) & buildingsChoiceMask) != 0;
		}
		for (unsigned i=0; i<flagsChoiceState.size(); ++i)
		{
			int id = IntBuildingType::shortNumberFromType(flagsChoiceName[i]);
			flagsChoiceState[i] = ((1<<id) & flagsChoiceMask) != 0;
		}
		
		stream->readLeaveSection();
	}

	return true;
}

void GameGUI::save(GAGCore::OutputStream *stream, const char *name)
{
	// Game is can't be no more automatically generated
	if (game.session.mapGenerationDescriptor)
		delete game.session.mapGenerationDescriptor;
	game.session.mapGenerationDescriptor=NULL;

	game.save(stream, false, name);
	
	stream->writeEnterSection("GameGUI");
	stream->writeUint32(chatMask, "chatMask");
	stream->writeSint32(localPlayer, "localPlayer");
	stream->writeSint32(localTeamNo, "localTeamNo");
	stream->writeSint32(viewportX, "viewportX");
	stream->writeSint32(viewportY, "viewportY");
	stream->writeUint32(hiddenGUIElements, "hiddenGUIElements");
	Uint32 buildingsChoiceMask = 0;
	Uint32 flagsChoiceMask = 0;
	// save one if visible
	for (unsigned i=0; i<buildingsChoiceState.size(); ++i)
	{
		if (buildingsChoiceState[i])
		{
			int id = IntBuildingType::shortNumberFromType(buildingsChoiceName[i]);
			buildingsChoiceMask |= (1<<id);
		}
	}
	for (unsigned i=0; i<flagsChoiceState.size(); ++i)
	{
		if (flagsChoiceState[i])
		{
			int id = IntBuildingType::shortNumberFromType(flagsChoiceName[i]);
			flagsChoiceMask |= (1<<id);
		}
	}
	stream->writeUint32(buildingsChoiceMask, "buildingsChoiceMask");
	stream->writeUint32(flagsChoiceMask, "flagsChoiceMask");
	stream->writeLeaveSection();
}

// TODO : merge thoses 3 functions into one

void GameGUI::drawButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 12);
	globalContainer->gfx->drawFilledRect(x+17, y+3, 94, 10, 128, 128, 128);

	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=Toolkit::getStringTable()->getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+17+((94-len)>>1), y+((16-h)>>1), globalContainer->littleFont, textToDraw);
}

void GameGUI::drawBlueButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 12);
	globalContainer->gfx->drawFilledRect(x+17, y+3, 94, 10, 128, 128, 192);

	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=Toolkit::getStringTable()->getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+17+((94-len)>>1), y+((16-h)>>1), globalContainer->littleFont, textToDraw);
}

void GameGUI::drawRedButton(int x, int y, const char *caption, bool doLanguageLookup)
{
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 12);
	globalContainer->gfx->drawFilledRect(x+17, y+3, 94, 10, 192, 128, 128);

	const char *textToDraw;
	if (doLanguageLookup)
		textToDraw=Toolkit::getStringTable()->getString(caption);
	else
		textToDraw=caption;
	int len=globalContainer->littleFont->getStringWidth(textToDraw);
	int h=globalContainer->littleFont->getStringHeight(textToDraw);
	globalContainer->gfx->drawString(x+17+((94-len)>>1), y+((16-h)>>1), globalContainer->littleFont, textToDraw);
}

void GameGUI::drawTextCenter(int x, int y, const char *caption, int i)
{
	const char *text;

	if (i==-1)
		text=Toolkit::getStringTable()->getString(caption);
	else
		text=Toolkit::getStringTable()->getString(caption, i);

	int dec=(128-globalContainer->littleFont->getStringWidth(text))>>1;
	globalContainer->gfx->drawString(x+dec, y, globalContainer->littleFont, text);
}

void GameGUI::drawScrollBox(int x, int y, int value, int valueLocal, int act, int max)
{
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);
	globalContainer->gfx->drawSprite(x+8, y, globalContainer->gamegui, 9);

	int size=(valueLocal*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);
	
	size=(act*92)/max;
	globalContainer->gfx->setClipRect(x+18, y, size, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);
	
	globalContainer->gfx->setClipRect();
	/*globalContainer->gfx->drawFilledRect(x, y, 128, 16, 128, 128, 128);
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
	globalContainer->gfx->drawFilledRect(x+16+1, y+5, size, 6, 28, 200, 28);*/
}

void GameGUI::cleanOldSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
		game.selectedBuilding=NULL;
	else if (selectionMode==UNIT_SELECTION)
		game.selectedUnit=NULL;
	else if (selectionMode==BRUSH_SELECTION)
		brush.unselect();
}

void GameGUI::setSelection(SelectionMode newSelMode, unsigned newSelection)
{
	if (selectionMode!=newSelMode)
	{
		cleanOldSelection();
		selectionMode=newSelMode;
	}

	if (selectionMode==BUILDING_SELECTION)
	{
		int id=Building::GIDtoID(newSelection);
		int team=Building::GIDtoTeam(newSelection);
		selection.building=game.teams[team]->myBuildings[id];
		game.selectedBuilding=selection.building;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		int id=Unit::GIDtoID(newSelection);
		int team=Unit::GIDtoTeam(newSelection);
		selection.unit=game.teams[team]->myUnits[id];
		game.selectedUnit=selection.unit;
	}
	else if (selectionMode==RESSOURCE_SELECTION)
	{
		selection.ressource=newSelection;
	}
}

void GameGUI::setSelection(SelectionMode newSelMode, void* newSelection)
{
	if (selectionMode!=newSelMode)
	{
		cleanOldSelection();
		selectionMode=newSelMode;
	}

	if (selectionMode==BUILDING_SELECTION)
	{
		selection.building=(Building*)newSelection;
		game.selectedBuilding=selection.building;
	}
	else if (selectionMode==UNIT_SELECTION)
	{
		selection.unit=(Unit*)newSelection;
		game.selectedUnit=selection.unit;
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		selection.build=(char*)newSelection;
	}
}

void GameGUI::checkSelection(void)
{
	if ((selectionMode==BUILDING_SELECTION) && (game.selectedBuilding==NULL))
	{
		clearSelection();
	}
	else if ((selectionMode==UNIT_SELECTION) && (game.selectedUnit==NULL))
	{
		clearSelection();
	}
}

void GameGUI::iterateSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		Building* selBuild=selection.building;
		Uint16 selectionGBID=selBuild->gid;
		assert(selBuild);
		assert(selectionGBID!=NOGBID);
		int pos=Building::GIDtoID(selectionGBID);
		int team=Building::GIDtoTeam(selectionGBID);
		int i=pos;
		if (team==localTeamNo)
		{
			while (i<pos+1024)
			{
				i++;
				Building *b=game.teams[team]->myBuildings[i&0x1FF];
				if (b && b->typeNum==selBuild->typeNum)
				{
					setSelection(BUILDING_SELECTION, b);
					centerViewportOnSelection();
					break;
				}
			}
		}
	}
	else if (selectionMode==TOOL_SELECTION)
	{
		Sint32 typeNum=globalContainer->buildingsTypes.getTypeNum(selection.build, 0, false);
		for (int i=0; i<1024; i++)
		{
			Building *b=game.teams[localTeamNo]->myBuildings[i];
			if (b && b->typeNum==typeNum)
			{
				setSelection(BUILDING_SELECTION, b);
				centerViewportOnSelection();
				break;
			}
		}
	}
}

void GameGUI::centerViewportOnSelection(void)
{
	if (selectionMode==BUILDING_SELECTION)
	{
		Building* b=selection.building;
		//assert (selBuild);
		//Building *b=game.teams[Building::GIDtoTeam(selectionGBID)]->myBuildings[Building::GIDtoID(selectionGBID)];
		assert(b);
		viewportX=b->getMidX()-((globalContainer->gfx->getW()-128)>>6);
		viewportY=b->getMidY()-((globalContainer->gfx->getH())>>6);
		viewportX=viewportX&game.map.getMaskW();
		viewportY=viewportY&game.map.getMaskH();
	}
}

void GameGUI::enableBuildingsChoice(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
			buildingsChoiceState[i] = true;
	}
}

void GameGUI::disableBuildingsChoice(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
			buildingsChoiceState[i] = false;
	}
}

void GameGUI::enableFlagsChoice(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
			flagsChoiceState[i] = true;
	}
}

void GameGUI::disableFlagsChoice(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
			flagsChoiceState[i] = false;
	}
}

void GameGUI::enableGUIElement(int id)
{
	hiddenGUIElements &= ~(1<<id);
}

void GameGUI::disableGUIElement(int id)
{
	hiddenGUIElements |= (1<<id);
	if (displayMode==id)
		nextDisplayMode();
}

void GameGUI::setCpuLoad(int s)
{
	smoothedCpuLoad[smoothedCpuLoadPos]=s;
	smoothedCpuLoadPos=(smoothedCpuLoadPos+1)%SMOOTH_CPU_LOAD_WINDOW_LENGTH;
}

void GameGUI::setMultiLine(const std::string &input, std::vector<std::string> *output)
{
	unsigned pos = 0;
	int length = globalContainer->gfx->getW()-128-64;
	
	std::string lastWord;
	std::string lastLine;
	
	while (pos<input.length())
	{
		if (input[pos] == ' ')
		{
			int actLineLength = globalContainer->standardFont->getStringWidth(lastLine.c_str());
			int actWordLength = globalContainer->standardFont->getStringWidth(lastWord.c_str());
			int spaceLength = globalContainer->standardFont->getStringWidth(" ");
			if (actWordLength+actLineLength+spaceLength < length)
			{
				if (lastLine.length())
					lastLine += " ";
				lastLine += lastWord;
				lastWord.clear();
			}
			else
			{
				output->push_back(lastLine);
				lastLine = lastWord;
				lastWord.clear();
			}
		}
		else
		{
			lastWord += input[pos];
		}
		pos++;
	}
	if (lastLine.length())
		lastLine += " ";
	lastLine += lastWord;
	if (lastLine.length())
		output->push_back(lastLine);
}

void GameGUI::addMessage(Uint8 r, Uint8 g, Uint8 b, const char *msgText, ...)
{
	Message message;
	message.showTicks=Message::DEFAULT_MESSAGE_SHOW_TICKS;
	message.r = r;
	message.g = g;
	message.b = b;
	message.a = DrawableSurface::ALPHA_OPAQUE;
	char fullText[1024];
	
	va_list ap;
	va_start(ap, msgText);
	vsnprintf (fullText, 1024, msgText, ap);
	va_end(ap);
	fullText[1023]=0;
	
	std::string fullTextStr(fullText);
	std::vector<std::string> messages;
	
	globalContainer->gfx->pushFontStyle(globalContainer->standardFont, Font::Style(Font::STYLE_BOLD, 255, 255, 255));
	setMultiLine(fullTextStr, &messages);
	globalContainer->gfx->popFontStyle(globalContainer->standardFont);
	
	for (unsigned i=0; i<messages.size(); i++)
	{
		message.text = messages[i];
		messagesList.push_back(message);
	}
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
