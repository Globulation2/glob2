/*
  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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


#include <sstream>
#include <iostream>
#include <algorithm>

#include <FileManager.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GraphicContext.h>
#include <StringTable.h>
#include <SupportFunctions.h>
#include <Toolkit.h>
#include <Stream.h>
#include <BinaryStream.h>
#include <TextStream.h>
#include <FormatableString.h>

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
#include "VoiceRecorder.h"

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
	const char *getText(void) const { return textInput->getText().c_str(); }
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

//! The screen that contains the message history In Game 
class InGameScrollableText:public OverlayScreen
{
protected:
	//! The textarea widget
	List *messageList;
	
public:
	//! InGameScrollableText constructor
	InGameScrollableText(GraphicContext *parentCtx, std::vector<std::string> messageHistory);
	//! InGameScrollableText destructor
	virtual ~InGameScrollableText() { }
	//! Dummy
	virtual void onAction(Widget *source, Action action, int par1, int par2) { }
	//! read messageHistory and update widget 
	void readHistory(std::vector<std::string> messageHistory);
};

InGameScrollableText::InGameScrollableText(GraphicContext *parentCtx, std::vector<std::string> messageHistory)
:OverlayScreen(parentCtx, (globalContainer->gfx->getW()-152), 100)
{
	messageList=new List(0, 0, (globalContainer->gfx->getW()-152), 100, 0, 0, "standard");
	addWidget(messageList);
	readHistory(messageHistory);
	dispatchInit();
}

void InGameScrollableText::readHistory(std::vector<std::string> messageHistory)
{
	messageList->clear();
	if (messageHistory.capacity() > 0){
		for (int i = messageHistory.size() -1; i>=0; i--){
			messageList->addText(messageHistory[i]);
		}
	}
}

GameGUI::GameGUI()
:game(this)
{
}

GameGUI::~GameGUI()
{

}

bool noRawMousewheel = false;

void GameGUI::init()
{
	notmenu = false;
	isRunning=true;
	gamePaused=false;
	hardPause=false;
	exitGlobCompletely=false;
	toLoadGameFileName[0]=0;
	drawHealthFoodBar=true;
	drawPathLines=false;
	drawAccessibilityAids=false;
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
	highlightSelection = 0.0f;
	miniMapPushed=false;
	putMark=false;
	showUnitWorkingToBuilding=true;
	chatMask=0xFFFFFFFF;
	hasSpaceBeenClicked=false;
	swallowSpaceKey=false;

	viewportSpeedX=0;
	viewportSpeedY=0;

	showStarvingMap=false;
	showDamagedMap=false;
	showDefenseMap=false;
	
	inGameMenu=IGM_NONE;
	gameMenuScreen=NULL;
	typingInputScreen=NULL;
	scrollableText=NULL;
	typingInputScreenPos=0;

	messagesList.clear();
	eventGoTypeIterator = 0;
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

 	for (int i=0; i<NUMBER_BUILDING_TYPE_NUM_WITH_PREDEFINED_UNIT_COUNT; i++)
		unitCount[i] = 1;

	campaign=NULL;
	missionName="";
	
	initUnitCount();

        if (getenv ("GLOB2_NO_RAW_MOUSEWHEEL")) {
          noRawMousewheel = true; }
}

void GameGUI::adjustLocalTeam()
{
	assert(localTeamNo>=0);
	assert(localTeamNo<32);
	assert(game.session.numberOfPlayer>0);
	assert(game.session.numberOfPlayer<32);
	assert(localTeamNo<game.session.numberOfTeam);
	
	localTeam = game.teams[localTeamNo];
	assert(localTeam);
	teamStats = &localTeam->stats;
	
	// recompute local forbidden and guard areas
	game.map.computeLocalForbidden(localTeamNo);
	game.map.computeLocalGuardArea(localTeamNo);
	game.map.computeLocalClearArea(localTeamNo);
	
	// set default event position
	eventGoPosX = localTeam->startPosX;
	eventGoPosY = localTeam->startPosY;
	eventGoType = 0;
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

void GameGUI::brushStep(bool maybeToggleMode, int mx, int my)
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
        /* We treat any brush of size 1 by 1 specially.  If an attempt
           is made to use the brush which would have no effect, we
           first toggle the state of whether we are adding or removing
           the area, so the use of the brush will have an effect.
           This allows quickly repairing errors without having to
           explicitly change the mode by hand.  We don't do this when
           dragging, so only explicit mouse clicks can change the
           mode. */
        if (maybeToggleMode && (brush.getBrushHeight(fig) == 1) && (brush.getBrushWidth(fig) == 1)) {
          int isAlreadyOn =
            ((brushType == FORBIDDEN_BRUSH) && game.map.isForbiddenLocal(mapX, mapY))
            || ((brushType == GUARD_AREA_BRUSH) && game.map.isGuardAreaLocal(mapX, mapY))
            || ((brushType == CLEAR_AREA_BRUSH) && game.map.isClearAreaLocal(mapX, mapY));
          unsigned mode = brush.getType();
          if (((mode == BrushTool::MODE_ADD) && isAlreadyOn)
              || ((mode == BrushTool::MODE_DEL) && !isAlreadyOn)) {
            sendBrushOrders();
            brush.setType((mode == BrushTool::MODE_ADD) ? BrushTool::MODE_DEL : BrushTool::MODE_ADD); 
            brushStep(false, mx,my); // restart action after changing mode; set maybeToggleMode to false to guarantee no further recursion
            return; }}
	brushAccumulator.applyBrush(&game.map, BrushApplication(mapX, mapY, fig));
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
					else if (brushType == CLEAR_AREA_BRUSH)
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), true);
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
					else if (brushType == CLEAR_AREA_BRUSH)
						game.map.localClearAreaMap.set(game.map.w*(y&game.map.hMask)+(x&game.map.wMask), false);
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
		else if (brushType == CLEAR_AREA_BRUSH)
			orderQueue.push_back(new OrderAlterateClearArea(localTeamNo, brush.getType(), &brushAccumulator));
		else
			assert(false);
		brushAccumulator.clear();
	}
}

void GameGUI::dragStep(int mx, int my, int button)
{
        /* We used to use SDL_GetMouseState, like the following
           commented-out code, but that was buggy and prevented
           dragging from correctly going through intermediate cells.
           It is vital to use the mouse position and button status as
           it was at the time in the middle of the event stream, not
           as it is now.  So instead we make sure the correct data is
           passed to us as a parameter. */
	// int mx, my;
	// Uint8 button = SDL_GetMouseState(&mx, &my);
        // fprintf (stderr, "enter dragStep: button: %d, mx: %d, selectionMode: %d\n", button, mx, selectionMode);
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
			brushStep(false, mx, my);
		}
	}
        // fprintf (stderr, "exit dragStep\n");
}

/* We need to keep track of the last recorded mouse position for use
   in drag steps.  We can't simply use SDL_GetMouseState to get this
   information, because we need the information as it was in the
   middle of the event stream.  (There may be many later events we
   have not yet processed.) */
int lastMouseX = 0, lastMouseY = 0; // can't make these Uint16 because of SDL_GetMouseState
Uint16 lastMouseButtonState = 0;

void GameGUI::step(void)
{
	SDL_Event event, mouseMotionEvent, windowEvent;
	bool wasMouseMotion=false;
	bool wasWindowEvent=false;
        int oldMouseMapX = -1, oldMouseMapY = -1; // hopefully the values here will never matter
	// we get all pending events but for mousemotion we only keep the last one
	while (SDL_PollEvent(&event))
	{
		if (event.type==SDL_MOUSEMOTION)
		{
                        lastMouseX = event.motion.x;
                        lastMouseY = event.motion.y;
                        lastMouseButtonState = event.motion.state;
                        int mouseMapX, mouseMapY;
                        bool onViewport = (lastMouseX < globalContainer->gfx->getW()-128);
                        /* We keep track for each mouse motion event
                           of which map cell it corresponds to.  When
                           dragging, we will use this to make sure we
                           process at least one event per map cell,
                           and only discard multiple events when they
                           are for the same map cell.  This is
                           necessary to make dragging work correctly
                           when drawing areas with the brush. */
                        if (onViewport) {
                          game.map.cursorToBuildingPos (lastMouseX, lastMouseY, 1, 1, &mouseMapX, &mouseMapY, viewportX, viewportY); }
                        else {
                          /* We interpret all locations outside the
                             viewport as being equivalent, and
                             distinct from any map location. */
                          mouseMapX = -1;
                          mouseMapY = -1; }
                        // fprintf (stderr, "mouse motion: (lastMouseX,lastMouseY): (%d,%d), (mouseMapX,mouseMapY): (%d,%d), (oldMouseMapX,oldMouseMapY): (%d,%d)\n", lastMouseX, lastMouseY, mouseMapX, mouseMapY, oldMouseMapX, oldMouseMapY);
                        /* Make sure dragging does not skip over map cells by
                           processing the old stored event rather than throwing
                           it away. */
                        if (wasMouseMotion
                            && (lastMouseButtonState & SDL_BUTTON(1)) // are we dragging? (should not be hard-coding this condition but should be abstract somehow)
                            && ((mouseMapX != oldMouseMapX)
                                || (mouseMapY != oldMouseMapY))) {
                          // fprintf (stderr, "processing old event instead of discarding it\n");
                          processEvent(&mouseMotionEvent); }
                        oldMouseMapX = mouseMapX;
                        oldMouseMapY = mouseMapY;
			mouseMotionEvent=event;
			wasMouseMotion=true;
		}
                else if ((event.type == SDL_MOUSEBUTTONDOWN) || (event.type == SDL_MOUSEBUTTONUP)) {
                  lastMouseButtonState = SDL_GetMouseState (&lastMouseX, &lastMouseY);
                  /* We ignore what SDL_GetMouseState does to
                     lastMouseX and lastMouseY, because that may
                     reflect many subsequent events that we have not
                     yet processed.  Technically, we shouldn't use
                     SDL_GetMouseState at all but should calculate the
                     button state by keeping track of what has
                     happened.  However, I haven't had the programming
                     energy to do this, so I am cheating in the line
                     above. */
                  lastMouseX = event.button.x;
                  lastMouseY = event.button.y;
                  processEvent (&event); }
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
		dragStep(lastMouseX, lastMouseY, lastMouseButtonState);

	assert(localTeam);
	if (localTeam->wasEvent(Team::UNIT_UNDER_ATTACK_EVENT))
	{
		Uint16 gid = localTeam->getEvent(Team::UNIT_UNDER_ATTACK_EVENT).id;
		int team = Unit::GIDtoTeam(gid);
		int id = Unit::GIDtoID(gid);
		Unit *u = game.teams[team]->myUnits[id];
		if (u)
		{
			int strDec=(int)(u->typeNum);
			addMessage(200, 30, 30, FormatableString(Toolkit::getStringTable()->getString("[Your %0 are under attack]")).arg(Toolkit::getStringTable()->getString("[units type]", strDec)));
			eventGoPosX = localTeam->getEvent(Team::UNIT_UNDER_ATTACK_EVENT).posX;
			eventGoPosY = localTeam->getEvent(Team::UNIT_UNDER_ATTACK_EVENT).posY;
			eventGoType = Team::UNIT_UNDER_ATTACK_EVENT;
		}
	}
	if (localTeam->wasEvent(Team::UNIT_CONVERTED_LOST))
	{
		int team = localTeam->getEvent(Team::UNIT_CONVERTED_LOST).team;
		const char *teamName = game.teams[team]->getFirstPlayerName();
		addMessage(140, 0, 0, FormatableString(Toolkit::getStringTable()->getString("[Your unit got converted to %0's team]")).arg(teamName));
		eventGoPosX = localTeam->getEvent(Team::UNIT_CONVERTED_LOST).posX;
		eventGoPosY = localTeam->getEvent(Team::UNIT_CONVERTED_LOST).posY;
		eventGoType = Team::UNIT_CONVERTED_LOST;
	}
	if (localTeam->wasEvent(Team::UNIT_CONVERTED_ACQUIERED))
	{
		int team = localTeam->getEvent(Team::UNIT_CONVERTED_ACQUIERED).team;
		const char *teamName = game.teams[team]->getFirstPlayerName();
		addMessage(100, 255, 100, FormatableString(Toolkit::getStringTable()->getString("[%0's team unit got converted to your team]")).arg(teamName));
		eventGoPosX = localTeam->getEvent(Team::UNIT_CONVERTED_ACQUIERED).posX;
		eventGoPosY = localTeam->getEvent(Team::UNIT_CONVERTED_ACQUIERED).posY;
		eventGoType = Team::UNIT_CONVERTED_ACQUIERED;
	}
	if (localTeam->wasEvent(Team::BUILDING_UNDER_ATTACK_EVENT))
	{
		Uint16 gid = localTeam->getEvent(Team::BUILDING_UNDER_ATTACK_EVENT).id;
		int team = Building::GIDtoTeam(gid);
		int id = Building::GIDtoID(gid);
		Building *b = game.teams[team]->myBuildings[id];
		if (b)
		{
			int strDec=b->type->shortTypeNum;
			addMessage(255, 0, 0, Toolkit::getStringTable()->getString("[the building is under attack]", strDec));
			eventGoPosX = localTeam->getEvent(Team::BUILDING_UNDER_ATTACK_EVENT).posX;
			eventGoPosY = localTeam->getEvent(Team::BUILDING_UNDER_ATTACK_EVENT).posY;
			eventGoType = Team::BUILDING_UNDER_ATTACK_EVENT;
		}
	}
	if (localTeam->wasEvent(Team::BUILDING_FINISHED_EVENT))
	{
		Uint16 gid = localTeam->getEvent(Team::BUILDING_FINISHED_EVENT).id;
		int team = Building::GIDtoTeam(gid);
		int id = Building::GIDtoID(gid);
		Building *b = game.teams[team]->myBuildings[id];
		if (b)
		{
			int strDec=b->type->shortTypeNum;
			addMessage(30, 255, 30, Toolkit::getStringTable()->getString("[the building is finished]", strDec));
			eventGoPosX = localTeam->getEvent(Team::BUILDING_FINISHED_EVENT).posX;
			eventGoPosY = localTeam->getEvent(Team::BUILDING_FINISHED_EVENT).posY;
			eventGoType = Team::BUILDING_FINISHED_EVENT;
		}
	}
	
	// voice step
	OrderVoiceData *orderVoiceData;
	while ((orderVoiceData = globalContainer->voiceRecorder->getNextOrder()) != NULL)
	{
		orderVoiceData->recepientsMask = chatMask;
		orderQueue.push_back(orderVoiceData);
	}
	
	// music step
	musicStep();
	
	// do a yog step
	yog->step();
	
	// do a irc step if IRC is enabled
	if (ircPtr)
	{
		ircPtr->step();
		// display IRC messages
		while (ircPtr->isChatMessage())
		{
			addMessage(99, 255, 242, FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[from:]")).arg(ircPtr->getChatMessageSource()).arg(ircPtr->getChatMessage()));
			ircPtr->freeChatMessage();
		}
	}

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
					addMessage(99, 255, 242, FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[from:]")).arg(m->userName).arg(m->text));
				break;
				case YCMT_ADMIN_MESSAGE:
					addMessage(138, 99, 255, FormatableString("<%0> %1").arg(m->userName).arg(m->text));
				break;
				case YCMT_PRIVATE_RECEIPT:
					addMessage(99, 255, 242, FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[to:]")).arg(m->userName).arg(m->text));
				break;
				case YCMT_PRIVATE_RECEIPT_BUT_AWAY:
					addMessage(99, 255, 242, FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[away:]")).arg(m->userName).arg(m->text));
				break;
				case YCMT_EVENT_MESSAGE:
					addMessage(99, 143, 255, m->text);
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

bool GameGUI::processScrollableWidget(SDL_Event *event)
{
	scrollableText->translateAndProcessEvent(event);
	return true;
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
					delete gameMenuScreen;
					inGameMenu=IGM_NONE;
					gameMenuScreen=NULL;
					return true;
				}
				break;
				case InGameMainScreen::QUIT_GAME:
				{
					delete gameMenuScreen;
					inGameMenu=IGM_NONE;
					gameMenuScreen=NULL;
					orderQueue.push_back(new PlayerQuitsGameOrder(localPlayer));
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
		if ((event->type==SDL_KEYDOWN) && (event->key.keysym.sym == SDLK_ESCAPE))
		{
			typingInputScreenInc=-TYPING_INPUT_BASE_INC;
			typingInputScreen->endValue=1;
		}
		
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
				{
					yog->sendMessage(message);
					if (ircPtr && (message[1]=='/'))
						ircPtr->sendCommand(&message[1]);
				}
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
		notmenu=true;
		processGameMenu(event);
	}
	else
	{
		notmenu=false;
		if (scrollableText)
		{
			processScrollableWidget(event);
		}
		if (event->type==SDL_KEYDOWN)
		{
			handleKey(event->key.keysym.sym, true, (event->key.keysym.mod & KMOD_SHIFT) != 0, (event->key.keysym.mod & KMOD_CTRL) != 0);
		}
		else if (event->type==SDL_KEYUP)
		{
			handleKey(event->key.keysym.sym, false, (event->key.keysym.mod & KMOD_SHIFT) != 0, (event->key.keysym.mod & KMOD_CTRL) != 0);
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
//					selBuild->verbose=(selBuild->verbose+1)%5;
//					printf("building gid=(%d)\n", selBuild->gid);
//					if (selBuild->verbose==0)
//						printf(" verbose off\n");
//					else if (selBuild->verbose==1 || selBuild->verbose==2)
//						printf(" verbose global [%d]\n", selBuild->verbose&1);
//					else if (selBuild->verbose==3 || selBuild->verbose==4)
//						printf(" verbose local [%d]\n", selBuild->verbose&1);
//					else
//						assert(false);
//					printf(" pos=(%d, %d)\n", selBuild->posX, selBuild->posY);
//					printf(" dirtyLocalGradient=[%d, %d]\n", selBuild->dirtyLocalGradient[0], selBuild->dirtyLocalGradient[1]);
//					printf(" globalGradient=[%p, %p]\n", selBuild->globalGradient[0], selBuild->globalGradient[1]);
//					printf(" locked=[%d, %d]\n", selBuild->locked[0], selBuild->locked[1]);
					
				}
				else
				{
					// Enable panning
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
                                                /* fprintf (stderr, "s=SDL_GetModState(): %d, S=KMOD_SHIFT: %d, C=KMOD_CTRL: %d\n",
                                                         SDL_GetModState(), KMOD_SHIFT, KMOD_CTRL); */
						if ((selBuild->type->maxUnitWorking) &&
							(selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING)&&
                                                        (noRawMousewheel ? (SDL_GetModState() & KMOD_CTRL) : !(SDL_GetModState()&KMOD_SHIFT)))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal+=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
							setUnitCount(selBuild->typeNum, nbReq);
						}
						else if ((selBuild->type->defaultUnitStayRange) &&
							(selBuild->unitStayRangeLocal < selBuild->type->maxUnitStayRange) &&
							(SDL_GetModState()&KMOD_SHIFT))
						{
							int nbReq=(selBuild->unitStayRangeLocal+=1);
							orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
							setUnitCount(selBuild->typeNum, nbReq);
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
                                                        (noRawMousewheel ? (SDL_GetModState() & KMOD_CTRL) : !(SDL_GetModState()&KMOD_SHIFT)))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal-=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
							setUnitCount(selBuild->typeNum, nbReq);
						}
						else if ((selBuild->type->defaultUnitStayRange) &&
							(selBuild->unitStayRangeLocal>0) &&
							(SDL_GetModState()&KMOD_SHIFT))
						{
							int nbReq=(selBuild->unitStayRangeLocal-=1);
							orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
							setUnitCount(selBuild->typeNum, nbReq);
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
			// showUnitWorkingToBuilding=false;
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
		globalContainer->gfx->setRes(newW, newH);
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

void GameGUI::repairAndUpgradeBuilding(Building *building, bool repair, bool upgrade)
{
	BuildingType *buildingType = building->type;
	
	// building site can't be repaired nor upgraded
	if (buildingType->isBuildingSite)
		return;
	// we can upgrade or repair only building from our team
	if (building->owner->teamNumber != localTeamNo)
		return;
	int typeNum = building->typeNum + 1; //determines type of updated building
	int unitWorking = getUnitCount(typeNum);
	int unitWorkingFuture = getUnitCount(typeNum+1);
	if ((building->hp < buildingType->hpMax) && repair)
	{
		// repair
		if ((building->type->regenerationSpeed == 0) &&
			(building->isHardSpaceForBuildingSite(Building::REPAIR)) &&
			(localTeam->maxBuildLevel() >= buildingType->level))
			orderQueue.push_back(new OrderConstruction(building->gid, 1, 1));
	}
	else if (upgrade)
	{
		// upgrade
		if ((buildingType->nextLevel != -1) &&
			(building->isHardSpaceForBuildingSite(Building::UPGRADE)) &&
			(localTeam->maxBuildLevel() > buildingType->level))
			orderQueue.push_back(new OrderConstruction(building->gid, unitWorking, unitWorkingFuture));
	}
}

enum TwoKeyMode
{
  TWOKEY_NONE = 0,
  TWOKEY_BUILDING = 1,
  TWOKEY_FLAG = 2,
  TWOKEY_AREA = 3,
} twoKeyMode = TWOKEY_NONE;

void GameGUI::handleKey(SDLKey key, bool pressed, bool shift, bool ctrl)
{
	int modifier;

	if (pressed)
		modifier=1;
	else
		modifier=-1;

	if (typingInputScreen == NULL)
	{
		std::string action="";
                // fprintf (stderr, "twoKeyMode: %d, key: %d\n", twoKeyMode, key);
                if (twoKeyMode != TWOKEY_NONE) {
                  if (pressed) {
                    switch (twoKeyMode) {
                    case TWOKEY_BUILDING:
                      switch (key) {
                      case SDLK_a: /* swArm */
                        action = "select make swarm tool";
                        break;
                      case SDLK_i: /* Inn */
                        action = "select make inn tool";
                        break;
                      case SDLK_h: /* Hospital */
                        action = "select make hospital tool";
                        break;
                      case SDLK_r: /* Racetrack */
                        action = "select make racetrack tool";
                        break;
                      case SDLK_p: /* swimming Pool */
                        action = "select make swimming pool tool";
                        break;
                      case SDLK_b: /* Barracks */
                        action = "select make barracks tool";
                        break;
                      case SDLK_s: /* School */
                        action = "select make school tool";
                        break;
                      case SDLK_d: /* Defense tower */
                        action = "select make defense tower tool";
                        break;
                      case SDLK_w: /* stone Wall */
                        action = "select make stone wall tool";
                        break;
                      case SDLK_m: /* Market */
                        action = "select make market tool";
                        break;
			default:break;}
                      break; 
                    case TWOKEY_FLAG:
                      switch (key) {
                      case SDLK_e: /* Exploration */
                        action = "select make exploration flag tool";
                        break;
                      case SDLK_w: /* War */
                        action = "select make war flag tool";
                        break;
                      case SDLK_c: /* Clearing */
                        action = "select make clearing flag tool";
                        break;
			default:break;}
                      break; 
                    case TWOKEY_AREA:
                      switch (key) {
                      case SDLK_f: /* Forbidden */
                        action = "select forbidden area tool";
                        break;
                      case SDLK_g: /* Guard */
                        action = "select guard area tool";
                        break;
                      case SDLK_c: /* Clearing */
                        action = "select clearing area tool";
                        break;
                      case SDLK_a: /* Add */
                        action = "switch to adding areas";
                        break;
                      case SDLK_d: /* Delete */
                        action = "switch to deleting areas";
                        break;
                      case SDLK_1:
                        action = "switch to area brush 1";
                        break;
                      case SDLK_2:
                        action = "switch to area brush 2";
                        break;
                      case SDLK_3:
                        action = "switch to area brush 3";
                        break;
                      case SDLK_4:
                        action = "switch to area brush 4";
                        break;
                      case SDLK_5:
                        action = "switch to area brush 5";
                        break;
                      case SDLK_6:
                        action = "switch to area brush 6";
                        break;
                      case SDLK_7:
                        action = "switch to area brush 7";
                        break;
                      case SDLK_8:
                        action = "switch to area brush 8";
                        break; 
			default:break;}
                      break;
		      default:break;}
                    key = SDLK_UNKNOWN;
                    twoKeyMode = TWOKEY_NONE; }
                  else {
                    /* this case happens when the initial prefix key is released */
                  }}
                // fprintf (stderr, "action: [%s]\n", action.c_str());
                
		switch (key)
		{
			case SDLK_ESCAPE:
				{
					if ((inGameMenu==IGM_NONE) && (pressed))
					{
						gameMenuScreen=new InGameMainScreen(!(hiddenGUIElements & HIDABLE_ALLIANCE));
						inGameMenu=IGM_MAIN;
					}
				}
				break;
			case SDLK_PLUS:
			case SDLK_KP_PLUS:
			case SDLK_EQUALS:
				{
					if ((pressed) && (selectionMode==BUILDING_SELECTION))
					{
						Building* selBuild=selection.building;
						if ((selBuild->owner->teamNumber==localTeamNo) && (selBuild->type->maxUnitWorking) && (selBuild->maxUnitWorkingLocal<MAX_UNIT_WORKING))
						{
							int nbReq=(selBuild->maxUnitWorkingLocal+=1);
							orderQueue.push_back(new OrderModifyBuilding(selBuild->gid, nbReq));
							setUnitCount(selBuild->typeNum, nbReq);
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
							setUnitCount(selBuild->typeNum, nbReq);
						}
					}
				}
				break;
			case SDLK_a :
				action=globalContainer->settings.keyboard_shortcuts["akey"];
				break;
			case SDLK_b :
				action=globalContainer->settings.keyboard_shortcuts["bkey"];
				break;
			case SDLK_c :
				action=globalContainer->settings.keyboard_shortcuts["ckey"];
				break;
			case SDLK_d :
				action=globalContainer->settings.keyboard_shortcuts["dkey"];
				break;
			case SDLK_e :
				action=globalContainer->settings.keyboard_shortcuts["ekey"];
				break;
			case SDLK_f :
				action=globalContainer->settings.keyboard_shortcuts["fkey"];
				break;
			case SDLK_g :
				action=globalContainer->settings.keyboard_shortcuts["gkey"];
				break;
			case SDLK_h :
				action=globalContainer->settings.keyboard_shortcuts["hkey"];
				break;
			case SDLK_i :
				action=globalContainer->settings.keyboard_shortcuts["ikey"];
				break;
			case SDLK_j :
				action=globalContainer->settings.keyboard_shortcuts["jkey"];
				break;
			case SDLK_k :
				action=globalContainer->settings.keyboard_shortcuts["kkey"];
				break;
			case SDLK_l :
				action=globalContainer->settings.keyboard_shortcuts["lkey"];
				break;
			case SDLK_m :
				action=globalContainer->settings.keyboard_shortcuts["mkey"];
				break;
			case SDLK_n :
				action=globalContainer->settings.keyboard_shortcuts["nkey"];
				break;
			case SDLK_o :
				action=globalContainer->settings.keyboard_shortcuts["okey"];
				break;
			case SDLK_p :
				action=globalContainer->settings.keyboard_shortcuts["pkey"];
				break;
			case SDLK_q :
				action=globalContainer->settings.keyboard_shortcuts["qkey"];
				break;
			case SDLK_r :
				action=globalContainer->settings.keyboard_shortcuts["rkey"];
				break;
			case SDLK_s :
				action=globalContainer->settings.keyboard_shortcuts["skey"];
				break;
			case SDLK_t :
				action=globalContainer->settings.keyboard_shortcuts["tkey"];
				break;
			case SDLK_u :
				action=globalContainer->settings.keyboard_shortcuts["ukey"];
				break;
			case SDLK_v :
				action=globalContainer->settings.keyboard_shortcuts["vkey"];
				break;
			case SDLK_w :
				action=globalContainer->settings.keyboard_shortcuts["wkey"];
				break;
			case SDLK_x :
				action=globalContainer->settings.keyboard_shortcuts["xkey"];
				break;
			case SDLK_y :
				action=globalContainer->settings.keyboard_shortcuts["ykey"];
				break;
			case SDLK_z :
				action=globalContainer->settings.keyboard_shortcuts["zkey"];
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
					setIsSpaceSet(true);
					if(!swallowSpaceKey)
					{
						int evX, evY;
						int sw, sh;
						
						if (ctrl)
						{
							eventGoTypeIterator = (eventGoTypeIterator+1) % Team::EVENT_TYPE_SIZE;
							
							if (!localTeam->getEvent((Team::EventType)eventGoTypeIterator).validPosition)
								break;
								
							evX = localTeam->getEvent((Team::EventType)eventGoTypeIterator).posX;
							evY = localTeam->getEvent((Team::EventType)eventGoTypeIterator).posY;
						}
						else
						{
							eventGoTypeIterator = eventGoType;
							evX = eventGoPosX;
							evY = eventGoPosY;
						}
					
						sw=globalContainer->gfx->getW();
						sh=globalContainer->gfx->getH();
						viewportX=evX-((sw-128)>>6);
						viewportY=evY-(sh>>6);
					}
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
			case SDLK_PAUSE:
				action="pause game";
				break;
			case SDLK_SCROLLOCK:
				if (pressed)
					hardPause=!hardPause;
				break;
			default:
			break;
		}
		if(action=="toggle draw unit paths")
		{
			if (pressed)
				drawPathLines=!drawPathLines;
		}
		else if(action=="destroy building")
		{
			if ((pressed) && (selectionMode==BUILDING_SELECTION))
			{
				Building* selBuild=selection.building;
				if (selBuild->owner->teamNumber==localTeamNo)
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
		}
		else if(action=="upgrade building")
		{			
			if ((pressed) && (selectionMode==BUILDING_SELECTION))
			{
				Building* selBuild = selection.building;
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = getUnitCount(typeNum - 1);
				if (selBuild->constructionResultState == Building::UPGRADE)
					orderQueue.push_back(new OrderCancelConstruction(selBuild->gid, unitWorking));
				else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
					repairAndUpgradeBuilding(selBuild, false, true);
			}
		}
		else if(action=="repair building")
		{
			if ((pressed) && (selectionMode==BUILDING_SELECTION))
			{
				Building* selBuild = selection.building;
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = getUnitCount(typeNum);
				if (selBuild->constructionResultState == Building::REPAIR)
					orderQueue.push_back(new OrderCancelConstruction(selBuild->gid, unitWorking));
				else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
					repairAndUpgradeBuilding(selBuild, true, false);
			}
		}
		else if(action=="toggle draw information")
		{
			if (pressed)
				drawHealthFoodBar=!drawHealthFoodBar;
		}
		else if(action=="toggle draw accessibility aids")
		{
			if (pressed)
				drawAccessibilityAids = true;
			else
				drawAccessibilityAids = false;
		}
		else if(action=="mark map")
		{
			if (pressed)
			{
				putMark=true;
				globalContainer->gfx->cursorManager.setNextType(CursorManager::CURSOR_MARK);
			}
		}
		else if(action=="record voice")
		{
			if (shift)
			{
				if (pressed)
				{
					if (globalContainer->voiceRecorder->recordingNow)
						globalContainer->voiceRecorder->stopRecording();
					else
						globalContainer->voiceRecorder->startRecording();
				}
			}
			else
			{
				if (pressed)
					globalContainer->voiceRecorder->startRecording();
				else if (!shift)
					globalContainer->voiceRecorder->stopRecording();
			}
		}
		else if(action=="view history")
		{
			if (pressed)
			{
				if ( ! scrollableText)
					scrollableText=new InGameScrollableText(globalContainer->gfx, messageHistory);
				else 
				{
					delete scrollableText;
					scrollableText=NULL;
				}
			}
		}
		else if(action=="pause game")
		{
			if (pressed)
				orderQueue.push_back(new PauseGameOrder(!gamePaused));
		}
                else if ((action == "prefix key select area tool") && pressed) {
                  twoKeyMode = TWOKEY_AREA; }
                else if ((action == "prefix key select building tool") && pressed) {
                  twoKeyMode = TWOKEY_BUILDING; }
                else if ((action == "prefix key select flag tool") && pressed) {
                  twoKeyMode = TWOKEY_FLAG; }
                else if (pressed
                         && ((action == "select make swarm tool")
                             || (action == "select make inn tool")
                             || (action == "select make hospital tool")
                             || (action == "select make racetrack tool")
                             || (action == "select make swimming pool tool")
                             || (action == "select make barracks tool")
                             || (action == "select make school tool")
                             || (action == "select make defense tower tool")
                             || (action == "select make stone wall tool")
                             || (action == "select make market tool")
                             || (action == "select make exploration flag tool")
                             || (action == "select make war flag tool")
                             || (action == "select make clearing flag tool")
                             || (action == "select forbidden area tool")
                             || (action == "select guard area tool")
                             || (action == "select clearing area tool")
                             || (action == "switch to adding areas")
                             || (action == "switch to deleting areas")
                             || (action == "switch to area brush 1")
                             || (action == "switch to area brush 2")
                             || (action == "switch to area brush 3")
                             || (action == "switch to area brush 4")
                             || (action == "switch to area brush 5")
                             || (action == "switch to area brush 6")
                             || (action == "switch to area brush 7")
                             || (action == "switch to area brush 8"))) {
                  clearSelection();
                  char * buildingType = NULL;
                  BrushType tmpBrushType;
                  bool isArea = false;
                  BrushTool::Mode tmpBrushMode = BrushTool::MODE_NONE;
                  int brushFigure;
                  bool isBrush = false;
                  char * flagType = NULL;
                  if (action == "select make swarm tool") {
                    buildingType = "swarm"; }
                  else if (action == "select make inn tool") {
                    buildingType = "inn"; }
                  else if (action == "select make hospital tool") {
                    buildingType = "hospital"; }
                  else if (action == "select make racetrack tool") {
                    buildingType = "racetrack"; }
                  else if (action == "select make swimming pool tool") {
                    buildingType = "swimmingpool"; }
                  else if (action == "select make barracks tool") {
                    buildingType = "barracks"; }
                  else if (action == "select make school tool") {
                    buildingType = "school"; }
                  else if (action == "select make defense tower tool") {
                    buildingType = "defencetower"; }
                  else if (action == "select make stone wall tool") {
                    buildingType = "stonewall"; }
                  else if (action == "select make market tool") {
                    buildingType = "market"; }
                  else if (action == "select make exploration flag tool") {
                    flagType = "explorationflag"; }
                  else if (action == "select make war flag tool") {
                    flagType = "warflag"; }
                  else if (action == "select make clearing flag tool") {
                    flagType = "clearingflag"; }
                  else if (action == "select forbidden area tool") {
                    isArea = true;
                    tmpBrushType = FORBIDDEN_BRUSH; }
                  else if (action == "select guard area tool") {
                    isArea = true;
                    tmpBrushType = GUARD_AREA_BRUSH; }
                  else if (action == "select clearing area tool") {
                    isArea = true;
                    tmpBrushType = CLEAR_AREA_BRUSH; }
                  else if (action == "switch to adding areas") {
                    tmpBrushMode = BrushTool::MODE_ADD; }
                  else if (action == "switch to deleting areas") {
                    tmpBrushMode = BrushTool::MODE_DEL; }
                  else if (action == "switch to area brush 1") {
                    brushFigure = 0;
                    isBrush = true; }
                  else if (action == "switch to area brush 2") {
                    brushFigure = 1;
                    isBrush = true; }
                  else if (action == "switch to area brush 3") {
                    brushFigure = 2;
                    isBrush = true; }
                  else if (action == "switch to area brush 4") {
                    brushFigure = 3;
                    isBrush = true; }
                  else if (action == "switch to area brush 5") {
                    brushFigure = 4;
                    isBrush = true; }
                  else if (action == "switch to area brush 6") {
                    brushFigure = 5;
                    isBrush = true; }
                  else if (action == "switch to area brush 7") {
                    brushFigure = 6;
                    isBrush = true; }
                  else if (action == "switch to area brush 8") {
                    brushFigure = 7;
                    isBrush = true; }
                  if (buildingType) {
                    if (isBuildingEnabled(std::string(buildingType))) {
                      displayMode = BUILDING_VIEW; // Can hiddenGUIElements forbid showing building view if the particular building type is enabled?  Do I need to check this?
                      setSelection(TOOL_SELECTION, (void *) buildingType); }}
                  else if (flagType) {
                    if (isFlagEnabled(std::string(flagType))) {
                      displayMode = FLAG_VIEW;  // Can hiddenGUIElements forbid showing flag view if the particular flag type is enabled?  Do I need to check this?
                      setSelection(TOOL_SELECTION, (void*) flagType); }}
                  else if (isArea || (tmpBrushMode != BrushTool::MODE_NONE) || isBrush) {
                    // Do I need to check if the GUI is enabled for this?
                    displayMode = FLAG_VIEW;
                    if (isArea) {
                      brushType = tmpBrushType; }
                    if (tmpBrushMode != BrushTool::MODE_NONE) {
                      brush.setType(tmpBrushMode); }
                    else if (brush.getType() == BrushTool::MODE_NONE) {
                      brush.setType(BrushTool::MODE_ADD); }
                    if (isBrush) {
                      brush.setFigure (brushFigure); }
                    setSelection(BRUSH_SELECTION); }}
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
	if (notmenu == false)
	{
                SDLMod modState = SDL_GetModState();
                int xMotion = 1;
                int yMotion = 1;
                /* We check that only Control is held to avoid accidentally
                   matching window manager bindings for switching windows
                   and/or desktops. */
                if ((modState & KMOD_CTRL) && ! (modState & (KMOD_ALT|KMOD_SHIFT))) {
                  /* It violates good abstraction principles that I
                     have to do the calculations in the next two
                     lines.  There should be methods that abstract
                     these computations. */
                  /* We move by half screens if Control is held while
                     the arrow keys are held.  So we shift by 6
                     instead of 5.  (If we shifted by 5, it would be
                     good to subtract 1 so that there would be a small
                     overlap between what is viewable both before and
                     after the motion.) */
                  xMotion = ((globalContainer->gfx->getW()-128)>>6);
                  yMotion = ((globalContainer->gfx->getH())>>6); }
                else if (modState) {
                  /* Probably some keys held down as part of window
                     manager operations. */
                  xMotion = 0;
                  yMotion = 0; }
                // int oldViewportX = viewportX;
                // int oldViewportY = viewportY;
		if (keystate[SDLK_UP])
			viewportY -= yMotion;
		if (keystate[SDLK_KP8])
			viewportY -= yMotion;
		if (keystate[SDLK_DOWN])
			viewportY += yMotion;
		if (keystate[SDLK_KP2])
			viewportY += yMotion;
		if ((keystate[SDLK_LEFT]) && (typingInputScreen == NULL)) // we haave a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			viewportX -= xMotion;
		if (keystate[SDLK_KP4])
			viewportX -= xMotion;
		if ((keystate[SDLK_RIGHT]) && (typingInputScreen == NULL)) // we haave a test in handleKeyAlways, that's not very clean, but as every key check based on key states and not key events are here, it is much simpler and thus easier to understand and thus cleaner ;-)
			viewportX += xMotion;
		if (keystate[SDLK_KP6])
			viewportX += xMotion;
		if (keystate[SDLK_KP7])
		{
			viewportX -= xMotion;
			viewportY -= yMotion;
		}
		if (keystate[SDLK_KP9])
		{
			viewportX += xMotion;
			viewportY -= yMotion;
		}
		if (keystate[SDLK_KP1])
		{
			viewportX -= xMotion;
			viewportY += yMotion;
		}
		if (keystate[SDLK_KP3])
		{
			viewportX += xMotion;
			viewportY += yMotion;
		}
                // if ((oldViewportX != viewportX) || (oldViewportY != viewportY)) {
                //   fprintf (stderr, "xMotion: %d, yMotion: %d, viewportX: %d, viewportY: %d\n", xMotion, yMotion, viewportX, viewportY); }
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
	const int scrollZoneWidth = 10;
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
		// handle paning
		int dx=(mx-panMouseX)>>1;
		int dy=(my-panMouseY)>>1;
		viewportX=(panViewX+dx)&game.map.getMaskW();
		viewportY=(panViewY+dy)&game.map.getMaskH();
	}

	dragStep(mx, my, button);
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
		
		int unitWorking = getUnitCount(typeNum);
		int unitWorkingFuture = getUnitCount(typeNum+1);
		
		if (isRoom)
		{
			orderQueue.push_back(new OrderCreate(localTeamNo, mapX, mapY, typeNum, unitWorking, unitWorkingFuture));
		}
		
	}
	else if (selectionMode==BRUSH_SELECTION)
	{
		brushStep(true, mouseX, mouseY);
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
					if (game.mouseUnit->attachedBuilding)
					{
						game.mouseUnit->attachedBuilding->save(stream);
						game.mouseUnit->attachedBuilding->saveCrossRef(stream);
					}
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
					// showUnitWorkingToBuilding=true;
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
				setUnitCount(selBuild->typeNum, nbReq);
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
					if (selBuild->unitStayRangeLocal < selBuild->type->maxUnitStayRange)
					{
						nbReq=(selBuild->unitStayRangeLocal+=1);
						orderQueue.push_back(new OrderModifyFlag(selBuild->gid, nbReq));
					}
				}
				setUnitCount(selBuild->typeNum, nbReq);
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
						orderQueue.push_back(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal));
					}
					
					ypos+=YOFFSET_TEXT_PARA;
				}
				
			if (buildingType->type == "explorationflag")
				// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
				// must be able to do to be accepted at this flag
				// 0 == any explorer
				// 1 == must be able to attack ground
				for (int i=0; i<2; i++)
				{
					if (my>ypos && my<ypos+YOFFSET_TEXT_PARA)
					{
						selBuild->minLevelToFlagLocal=i;
						orderQueue.push_back(new OrderModifyMinLevelToFlag(selBuild->gid, selBuild->minLevelToFlagLocal));
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
			if (selBuild->constructionResultState==Building::REPAIR)
			{
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = getUnitCount(typeNum);
				orderQueue.push_back(new OrderCancelConstruction(selBuild->gid, unitWorking));
			}
			else if (selBuild->constructionResultState==Building::UPGRADE)
			{
				int typeNum = selBuild->typeNum; //determines type of updated building
				int unitWorking = getUnitCount(typeNum - 1);
				orderQueue.push_back(new OrderCancelConstruction(selBuild->gid, unitWorking));
			}
			else if ((selBuild->constructionResultState==Building::NO_CONSTRUCTION) && (selBuild->buildingState==Building::ALIVE))
			{
				repairAndUpgradeBuilding(selBuild, true, true);
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
			// change the brush type (forbidden, guard, clear) if necessary
			if (my < YOFFSET_BRUSH+40)
			{
				if (mx < 44)
					brushType = FORBIDDEN_BRUSH;
				else if (mx < 84)
					brushType = GUARD_AREA_BRUSH;
				else
					brushType = CLEAR_AREA_BRUSH;
			}
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
	else if (displayMode==STAT_GRAPH_VIEW)
	{
//		drawCheckButton(globalContainer->gfx->getW()-128+8, YPOS_BASE_STAT+140+64, Toolkit::getStringTable()->getString("[Starving Map]"), showStarvingMap);
		if(mx > 8 && mx < 24)
		{
			if(my > YPOS_BASE_STAT+140+64 && my < YPOS_BASE_STAT+140+80)
			{
				showDamagedMap=false;
				showDefenseMap=false;
				showStarvingMap=!showStarvingMap;
			}

			if(my > YPOS_BASE_STAT+140+88 && my < YPOS_BASE_STAT+140+104)
			{
				showDamagedMap=!showDamagedMap;
				showStarvingMap=false;
				showDefenseMap=false;
			}

			if(my > YPOS_BASE_STAT+140+112 && my < YPOS_BASE_STAT+140+128)
			{
				showDefenseMap=!showDefenseMap;
				showStarvingMap=false;
				showDamagedMap=false;
			}
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
					
					globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 128, 128, 128));
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-20, "[Building short explanation]", typeId);
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-8, "[Building short explanation 2]", typeId);
					globalContainer->littleFont->popStyle();
					BuildingType *bt = globalContainer->buildingsTypes.getByType(type, 0, true);
					if (bt)
					{
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, buildingInfoStart+6, globalContainer->littleFont,
							FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Wood]")).arg(bt->maxRessource[0]).c_str());
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, buildingInfoStart+17, globalContainer->littleFont,
							FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Stone]")).arg(bt->maxRessource[3]).c_str());

						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, buildingInfoStart+6, globalContainer->littleFont,
							FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Alga]")).arg(bt->maxRessource[4]).c_str());
						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+64, buildingInfoStart+17, globalContainer->littleFont,
							FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Corn]")).arg(bt->maxRessource[1]).c_str());

						globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, buildingInfoStart+28, globalContainer->littleFont,
							FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[Papyrus]")).arg(bt->maxRessource[2]).c_str());
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

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-128+((128-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos+5, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

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
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[hp]")).c_str());

	if (selUnit->hp<=selUnit->trigHP)
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selUnit->hp).arg(selUnit->performance[HP]).c_str());
	globalContainer->littleFont->popStyle();

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos+YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[food]")).c_str());

	// draw food
	if (selUnit->isUnitHungry())
		{ r=255; g=0; b=0; }
	else
		{ r=0; g=255; b=0; }

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+2*YOFFSET_TEXT_LINE+YOFFSET_TEXT_PARA, globalContainer->littleFont, FormatableString("%0 % (%1)").arg(((float)selUnit->hungry*100.0f)/(float)Unit::HUNGRY_MAX, 0, 0).arg(selUnit->fruitCount).c_str());
	globalContainer->littleFont->popStyle();

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

	globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[current speed]")).arg(selUnit->speed).c_str());
	ypos += YOFFSET_TEXT_PARA+10;
	
	if (selUnit->performance[ARMOR])
	{
		int armorReductionPerHappyness = selUnit->race->getUnitType(selUnit->typeNum, selUnit->level[ARMOR])->armorReductionPerHappyness;
		int realArmor = selUnit->performance[ARMOR] - selUnit->fruitCount * armorReductionPerHappyness;
		if (realArmor < 0)
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 : %1 = %2 - %3 * %4").arg(Toolkit::getStringTable()->getString("[armor]")).arg(realArmor).arg(selUnit->performance[ARMOR]).arg(selUnit->fruitCount).arg(armorReductionPerHappyness).c_str());
		if (realArmor < 0)
			globalContainer->littleFont->popStyle();
	}
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->typeNum!=EXPLORER)
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0:").arg(Toolkit::getStringTable()->getString("[levels]")).c_str());
	ypos += YOFFSET_TEXT_PARA;

	if (selUnit->performance[WALK])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Walk]")).arg((1+selUnit->level[WALK])).arg(selUnit->performance[WALK]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[SWIM])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Swim]")).arg(selUnit->level[SWIM]).arg(selUnit->performance[SWIM]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[BUILD])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Build]")).arg(1+selUnit->level[BUILD]).arg(selUnit->performance[BUILD]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[HARVEST])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[Harvest]")).arg(1+selUnit->level[HARVEST]).arg(selUnit->performance[HARVEST]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_SPEED])
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1) : %2").arg(Toolkit::getStringTable()->getString("[At. speed]")).arg(1+selUnit->level[ATTACK_SPEED]).arg(selUnit->performance[ATTACK_SPEED]).c_str());
	ypos += YOFFSET_TEXT_LINE;

	if (selUnit->performance[ATTACK_STRENGTH])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[At. strength]")).arg(1+selUnit->level[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).arg(selUnit->performance[ATTACK_STRENGTH]).arg(selUnit->experienceLevel).c_str());
		
		ypos += YOFFSET_TEXT_PARA + 2;
	}
	
	if (selUnit->performance[MAGIC_ATTACK_AIR])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Air]")).arg(1+selUnit->level[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_AIR]).arg(selUnit->experienceLevel).c_str());
		
		ypos += YOFFSET_TEXT_PARA + 2;
	}
	
	if (selUnit->performance[MAGIC_ATTACK_GROUND])
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-124, ypos, globalContainer->littleFont, FormatableString("%0 (%1+%2) : %3+%4").arg(Toolkit::getStringTable()->getString("[Magic At. Ground]")).arg(1+selUnit->level[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).arg(selUnit->performance[MAGIC_ATTACK_GROUND]).arg(selUnit->experienceLevel).c_str());
		
		ypos += YOFFSET_TEXT_PARA + 2;
	}
	
	if (selUnit->performance[ATTACK_STRENGTH] || selUnit->performance[MAGIC_ATTACK_AIR] || selUnit->performance[MAGIC_ATTACK_GROUND])
		drawXPProgressBar(globalContainer->gfx->getW()-128, ypos, selUnit->experience, selUnit->getNextLevelThreshold());
}

void GameGUI::drawValueAlignedRight(int y, int v)
{
	FormatableString s("%0");
	s.arg(v);
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
			FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[ressources]", i)).arg(ressources[i]).c_str());
	}
}

void GameGUI::drawCheckButton(int x, int y, const char* caption, bool isSet)
{
	globalContainer->gfx->drawRect(x, y, 16, 16, Color::white);
	if(isSet)
	{
		globalContainer->gfx->drawLine(x+4, y+4, x+12, y+12, Color::white);
		globalContainer->gfx->drawLine(x+12, y+4, x+4, y+12, Color::white);
	}
	globalContainer->gfx->drawString(x+20, y, globalContainer->littleFont, caption); 
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

	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
	int titleLen = globalContainer->littleFont->getStringWidth(title.c_str());
	int titlePos = globalContainer->gfx->getW()-128+((128-titleLen)>>1);
	globalContainer->gfx->drawString(titlePos, ypos, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

	// building text
	title = "";
	if ((buildingType->nextLevel>=0) ||  (buildingType->prevLevel>=0))
	{
		const char *textT = Toolkit::getStringTable()->getString("[level]");
		title += FormatableString("%0 %1").arg(textT).arg(buildingType->level+1);
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
	
	globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 200));
	globalContainer->gfx->drawString(titlePos, ypos+YOFFSET_TEXT_PARA-1, globalContainer->littleFont, title.c_str());
	globalContainer->littleFont->popStyle();

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
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[hp]"));
		globalContainer->littleFont->popStyle();

		if (selBuild->hp <= buildingType->hpMax/5)
			{ r=255; g=0; b=0; }
		else
			{ r=0; g=255; b=0; }

		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, r, g, b));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->hp).arg(buildingType->hpMax).c_str());
		globalContainer->littleFont->popStyle();
	}

	// inside
	if (buildingType->maxUnitInside && ((selBuild->owner->allies)&(1<<localTeamNo)))
	{
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE, globalContainer->littleFont, Toolkit::getStringTable()->getString("[inside]"));
		globalContainer->littleFont->popStyle();
		if (selBuild->buildingState==Building::ALIVE)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0/%1").arg(selBuild->unitsInside.size()).arg(selBuild->maxUnitInside).c_str());
		}
		else
		{
			if (selBuild->unitsInside.size()>1)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0%1").arg(Toolkit::getStringTable()->getString("[Still (i)]")).arg(selBuild->unitsInside.size()).c_str());
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
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos, globalContainer->littleFont, FormatableString("%0").arg(Toolkit::getStringTable()->getString("[In way]")).c_str());
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(goingTo).c_str());
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-68, ypos+YOFFSET_TEXT_PARA+YOFFSET_TEXT_LINE,
		globalContainer->littleFont, FormatableString(Toolkit::getStringTable()->getString("[On the spot]")).c_str());
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-66, ypos+YOFFSET_TEXT_PARA+2*YOFFSET_TEXT_LINE, globalContainer->littleFont, FormatableString("%0").arg(onSpot).c_str());
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
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, working);
				globalContainer->littleFont->popStyle();
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+len, ypos, globalContainer->littleFont, FormatableString("%0/%1").arg((int)selBuild->unitsWorking.size()).arg(selBuild->maxUnitWorkingLocal).c_str());
				drawScrollBox(globalContainer->gfx->getW()-128, ypos+YOFFSET_TEXT_BAR, selBuild->maxUnitWorkingLocal, selBuild->maxUnitWorkingLocal, selBuild->unitsWorking.size(), MAX_UNIT_WORKING);
			}
			else
			{
				if (selBuild->unitsWorking.size()>1)
				{
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, FormatableString("%0%1%2").arg(Toolkit::getStringTable()->getString("[still (w)]")).arg(selBuild->unitsWorking.size()).arg(Toolkit::getStringTable()->getString("[units working]")).c_str());
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
			globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, range);
			globalContainer->littleFont->popStyle();
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4+len, ypos, globalContainer->littleFont, FormatableString("%0").arg(selBuild->unitStayRange).c_str());
			drawScrollBox(globalContainer->gfx->getW()-128, ypos+YOFFSET_TEXT_BAR, selBuild->unitStayRange, selBuild->unitStayRangeLocal, 0, selBuild->type->maxUnitStayRange);
		}
		ypos += YOFFSET_BAR+YOFFSET_B_SEP;
	}
	
	// flag control of team and allies
	if ((selBuild->owner->allies) & (1<<localTeamNo))
	{
		// cleared ressources for clearing flags:
		if (buildingType->type == "clearingflag")
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
		else if (buildingType->type == "warflag")
		{
			ypos += YOFFSET_B_SEP;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Min required level:]"));
			ypos += YOFFSET_TEXT_PARA;
			for (int i=0; i<4; i++)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+28, ypos, globalContainer->littleFont, 1+i);
				int spriteId;
				if (i==selBuild->minLevelToFlagLocal)
					spriteId=20;
				else
					spriteId=19;
				globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+10, ypos+2, globalContainer->gamegui, spriteId);
				
				ypos+=YOFFSET_TEXT_PARA;
			}
		}
		else if (buildingType->type == "explorationflag")
		{
			int spriteId;
			
			ypos += YOFFSET_B_SEP;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont,
				Toolkit::getStringTable()->getString("[Min required level:]"));
			ypos += YOFFSET_TEXT_PARA;
			
			// we use minLevelToFlag as an int which says what magic effect at minimum an explorer
			// must be able to do to be accepted at this flag
			// 0 == any explorer
			// 1 == must be able to attack ground
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+28, ypos, globalContainer->littleFont,Toolkit::getStringTable()->getString("[any explorer]"));
			if (selBuild->minLevelToFlagLocal == 0)
				spriteId = 20;
			else
				spriteId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+10, ypos+2, globalContainer->gamegui, spriteId);
			
			ypos += YOFFSET_TEXT_PARA;
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+28, ypos, globalContainer->littleFont,Toolkit::getStringTable()->getString("[ground attack]"));
			if (selBuild->minLevelToFlagLocal == 1)
				spriteId = 20;
			else
				spriteId = 19;
			globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+10, ypos+2, globalContainer->gamegui, spriteId);
		}
	}

	// other infos
	if (buildingType->armor)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, FormatableString("%0: %1").arg(Toolkit::getStringTable()->getString("[armor]")).arg(buildingType->armor).c_str());
		ypos+=YOFFSET_TEXT_LINE;
	}
	if (buildingType->maxUnitInside)
		ypos += YOFFSET_INFOS;
	if (buildingType->shootDamage)
	{
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+1, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[damage]")).arg(buildingType->shootDamage).c_str());
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+12, globalContainer->littleFont, FormatableString("%0 : %1").arg(Toolkit::getStringTable()->getString("[range]")).arg(buildingType->shootingRange).c_str());
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
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 185, 195, 21));
		globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, Toolkit::getStringTable()->getString("[market]"));
		globalContainer->littleFont->popStyle();
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-36-3, ypos+1, globalContainer->gamegui, EXCHANGE_BUILDING_ICONS);
		ypos += YOFFSET_TEXT_PARA;
		for (unsigned i=0; i<HAPPYNESS_COUNT; i++)
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos, globalContainer->littleFont, FormatableString("%0 (%1/%2)").arg(Toolkit::getStringTable()->getString("[ressources]", i+HAPPYNESS_BASE)).arg(selBuild->ressources[i+HAPPYNESS_BASE]).arg(buildingType->maxRessource[i+HAPPYNESS_BASE]).c_str());

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
					globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+(j*11), globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[ressources]", i)).arg(selBuild->ressources[i]).arg(buildingType->maxRessource[i]).c_str());
					j++;
				}
			}
			if (buildingType->maxBullets)
			{
				globalContainer->gfx->drawString(globalContainer->gfx->getW()-128+4, ypos+(j*11), globalContainer->littleFont, FormatableString("%0 : %1/%2").arg(Toolkit::getStringTable()->getString("[Bullets]")).arg(selBuild->bullets).arg(buildingType->maxBullets).c_str());
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
							globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));
							int ressources[BASIC_COUNT];
							selBuild->getRessourceCountToRepair(ressources);
							drawCosts(ressources, globalContainer->littleFont);
							globalContainer->littleFont->popStyle();
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
							globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 200, 200, 255));

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

							globalContainer->littleFont->popStyle();
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
			const std::string amountS = FormatableString("%0/%1").arg(amount).arg(sizesCount);
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
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+8, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 13);
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+48, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 14);
		globalContainer->gfx->drawSprite(globalContainer->gfx->getW()-128+88, YPOS_BASE_FLAG+YOFFSET_BRUSH, globalContainer->gamegui, 25);
		if (brush.getType() != BrushTool::MODE_NONE)
		{
			int decX = 8 + ((int)brushType) * 40;
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
				int panelMouseX = mouseX - globalContainer->gfx->getW() + 128;
				if (panelMouseX < 44)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[forbidden area]");
				else if (panelMouseX < 84)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[guard area]");
				else
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[clear area]");
			}
			else
			{
				if (brushType == FORBIDDEN_BRUSH)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[forbidden area]");
				else if (brushType == GUARD_AREA_BRUSH)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[guard area]");
				else if (brushType == CLEAR_AREA_BRUSH)
					drawTextCenter(globalContainer->gfx->getW()-128, buildingInfoStart-32, "[clear area]");
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
		drawCheckButton(globalContainer->gfx->getW()-128+8, YPOS_BASE_STAT+140+64, Toolkit::getStringTable()->getString("[Starving Map]"), showStarvingMap);
		drawCheckButton(globalContainer->gfx->getW()-128+8, YPOS_BASE_STAT+140+88, Toolkit::getStringTable()->getString("[Damaged Map]"), showDamagedMap);
		drawCheckButton(globalContainer->gfx->getW()-128+8, YPOS_BASE_STAT+140+112, Toolkit::getStringTable()->getString("[Defense Map]"), showDefenseMap);
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
		globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, actC[0], actC[1], actC[2]));
		globalContainer->gfx->drawString(dec+22, 0, globalContainer->littleFont, FormatableString("%0 / %1").arg(free).arg(tot).c_str());
		globalContainer->littleFont->popStyle();

		dec += 70;
	}

	// draw prestige stats
	globalContainer->gfx->drawString(dec+0, 0, globalContainer->littleFont, FormatableString("%0 / %1 / %2").arg(localTeam->prestige).arg(game.totalPrestige).arg(game.prestigeToReach).c_str());
	
	dec += 90;
	
	// draw unit conversion stats
	globalContainer->gfx->drawString(dec, 0, globalContainer->littleFont, FormatableString("+%0 / -%1").arg(localTeam->unitConversionGained).arg(localTeam->unitConversionLost).c_str());
	
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
			
		// modifiy highlight given room
		if (isRoom)
			highlightSelection = std::min(highlightSelection + 0.1f, 1.0f);
		else
			highlightSelection = std::max(highlightSelection - 0.1f, 0.0f);
		
		// we get the screen dimensions of the building
		int batW = (bt->width)<<5;
		int batH = sprite->getH(bt->gameSpriteImage);
		int batX = (((mapX-viewportX)&(game.map.wMask))<<5);
		int batY = (((mapY-viewportY)&(game.map.hMask))<<5)-(batH-(bt->height<<5));
		
		// we draw the building
		sprite->setBaseColor(localTeam->colorR, localTeam->colorG, localTeam->colorB);
		globalContainer->gfx->setClipRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH());
		int spriteIntensity = 127+static_cast<int>(128.0f*splineInterpolation(1.f, 0.f, 1.f, highlightSelection));
		globalContainer->gfx->drawSprite(batX, batY, sprite, bt->gameSpriteImage, spriteIntensity);
		
		if (!bt->isVirtual)
		{
			if (localTeam->noMoreBuildingSitesCountdown>0)
			{
				globalContainer->gfx->drawRect(batX, batY, batW, batH, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX, batY, batX+batW-1, batY+batH-1, 255, 0, 0, 127);
				globalContainer->gfx->drawLine(batX+batW-1, batY, batX, batY+batH-1, 255, 0, 0, 127);
				
				globalContainer->littleFont->pushStyle(Font::Style(Font::STYLE_NORMAL, 255, 0, 0, 127));
				globalContainer->gfx->drawString(batX, batY-12, globalContainer->littleFont, FormatableString("%0.%1").arg(localTeam->noMoreBuildingSitesCountdown/40).arg((localTeam->noMoreBuildingSitesCountdown%40)/4).c_str());
				globalContainer->littleFont->popStyle();
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
                /* Instead of using a dimmer intensity to indicate
                   removing of areas, this should rather use dashed
                   lines.  (The intensities used below are 2/3 as
                   bright for the case of removing areas.) */
                /* This reasoning should be abstracted out and reused
                   in MapEdit.cpp to choose a color for those cases
                   where areas are being drawn. */
                unsigned mode = brush.getType();
                Color c = Color(0,0,0);
                /* The following colors have been chosen to match the
                   colors in the .png files for the animations of
                   areas as of 2007-04-29.  If those .png files are
                   updated with different colors, then the following
                   code should change accordingly. */
                if (brushType == FORBIDDEN_BRUSH) {
                  if (mode == BrushTool::MODE_ADD) {
                    c = Color(255,0,0); }
                  else {
                    c = Color(170,0,0); }}
                else if (brushType == GUARD_AREA_BRUSH) {
                  if (mode == BrushTool::MODE_ADD) {
                    c = Color(27,0,255); }
                  else {
                    c = Color(18,0,170); }}
                else if (brushType == CLEAR_AREA_BRUSH) {
                  if (mode == BrushTool::MODE_ADD) {
                    /* some of the clearing area images use (252,207,0) instead */
                    c = Color(251,206,0); }
                  else {
                    c = Color(167,137,0); }}
		brush.drawBrush(mouseX, mouseY, c);
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
				globalContainer->gfx->drawString(44, 44+pnb*20, globalContainer->standardFont, FormatableString(Toolkit::getStringTable()->getString("[waiting for %0]")).arg(game.players[pi2]->name).c_str());
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
		
		if (swallowSpaceKey)
		{
			globalContainer->gfx->drawString(32, ymesg+yinc, globalContainer->standardFont, Toolkit::getStringTable()->getString("[press space]"));
			yinc += 20;
		}

		// show script counter
		if (game.script.getMainTimer())
		{
			globalContainer->gfx->drawString(globalContainer->gfx->getW()-165, ymesg, globalContainer->standardFont, FormatableString("%0").arg(game.script.getMainTimer()).c_str());
			yinc = std::max(yinc, 32);
		}

		ymesg += yinc+2;
		
		// display messages
		for (std::list <Message>::iterator it=messagesList.begin(); it!=messagesList.end();)
		{
			globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_BOLD, it->r, it->g, it->b, it->a));
			if (scrollableText) 
				globalContainer->gfx->drawString(32, ymesg + 105, globalContainer->standardFont, it->text.c_str());
			else
				globalContainer->gfx->drawString(32, ymesg, globalContainer->standardFont, it->text.c_str());
			globalContainer->standardFont->popStyle();
			ymesg += 20;

			// delete old messages
			if (!(--(it->showTicks)))
			{
				// update message history
				messageHistory.push_back(it->text.c_str());
				// update in-game history if it's there
				if (scrollableText) {
					scrollableText->readHistory(messageHistory);
				}
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
			int ray = (int)(sin((double)(it->showTicks * 2.0)/(double)(Mark::DEFAULT_MARK_SHOW_TICKS)*3.141592)*Mark::DEFAULT_MARK_SHOW_TICKS/2);
			//int ray2 = (int)(cos((double)(it->showTicks * 2.0)/(double)(Mark::DEFAULT_MARK_SHOW_TICKS)*3.141592)*Mark::DEFAULT_MARK_SHOW_TICKS/2);
			ray = (abs(ray) * it->showTicks) / Mark::DEFAULT_MARK_SHOW_TICKS;
			//ray2 = (abs(ray2) * it->showTicks) / Mark::DEFAULT_MARK_SHOW_TICKS;
			Uint8 a = Color::ALPHA_OPAQUE;
			
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
			
			globalContainer->gfx->drawCircle(x, y, ray, it->r, it->g, it->b, a);
			globalContainer->gfx->drawHorzLine(x+ray-4+1, y, 8, it->r, it->g, it->b, a);
			globalContainer->gfx->drawHorzLine(x-ray-4, y, 8, it->r, it->g, it->b, a);
			globalContainer->gfx->drawVertLine(x, y+ray-4+1, 8, it->r, it->g, it->b, a);
			globalContainer->gfx->drawVertLine(x, y-ray-4, 8, it->r, it->g, it->b, a);
			//globalContainer->gfx->drawCircle(x, y, (ray2*11)/8, it->r, it->g, it->b, a);

			//Draw it not just on the minimap
			game.map.mapCaseToDisplayable(it->x, it->y, &x, &y, viewportX, viewportY);
			globalContainer->gfx->drawCircle(x, y, (ray*2), it->r, it->g, it->b, a);
			globalContainer->gfx->drawHorzLine(x+(ray*2)-4+1, y, 8, it->r, it->g, it->b, a);
			globalContainer->gfx->drawHorzLine(x-(ray*2)-4, y, 8, it->r, it->g, it->b, a);
			globalContainer->gfx->drawVertLine(x, y+(ray*2)-4+1, 8, it->r, it->g, it->b, a);
			globalContainer->gfx->drawVertLine(x, y-(ray*2)-4, 8, it->r, it->g, it->b, a);

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

	// Draw icon if trasmitting
	if (globalContainer->voiceRecorder->recordingNow)
		globalContainer->gfx->drawSprite(5, globalContainer->gfx->getH()-50, globalContainer->gamegui, 24);
	
	// Draw the bar contining number of units, CPU load, etc...
	drawTopScreenBar();
}

void GameGUI::drawInGameMenu(void)
{
	gameMenuScreen->dispatchPaint();
	globalContainer->gfx->drawSurface((int)gameMenuScreen->decX, (int)gameMenuScreen->decY, gameMenuScreen->getSurface());
	
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
	globalContainer->gfx->drawSurface((int)typingInputScreen->decX, (int)typingInputScreen->decY, typingInputScreen->getSurface());
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

void GameGUI::drawInGameScrollableText(void)
{
	scrollableText->decX=10;
	scrollableText->decY=32;
	scrollableText->dispatchPaint();
	globalContainer->gfx->drawSurface(scrollableText->decX, scrollableText->decY, scrollableText->getSurface());
}

void GameGUI::drawAll(int team)
{
	// draw the map
	Uint32 drawOptions =	(drawHealthFoodBar ? Game::DRAW_HEALTH_FOOD_BAR : 0) |
								(drawPathLines ?  Game::DRAW_PATH_LINE : 0) |
								(drawAccessibilityAids ? Game::DRAW_ACCESSIBILITY : 0 ) |
								((selectionMode==TOOL_SELECTION) ? Game::DRAW_BUILDING_RECT : 0) |
								((showStarvingMap) ? Game::DRAW_STARVING_OVERLAY : 0) |
								((showDamagedMap) ? Game::DRAW_DAMAGED_OVERLAY : 0) |
								((showDefenseMap) ? Game::DRAW_DEFENSE_OVERLAY : 0) |
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
		globalContainer->gfx->drawFilledRect(0, 0, globalContainer->gfx->getW()-128, globalContainer->gfx->getH(), 0, 0, 0, 20);
		const char *s = Toolkit::getStringTable()->getString("[Paused]");
		int x = (globalContainer->gfx->getW()-globalContainer->menuFont->getStringWidth(s))>>1;
		globalContainer->gfx->drawString(x, globalContainer->gfx->getH()-80, globalContainer->menuFont, s);
	}

	// draw the panel
	globalContainer->gfx->setClipRect();
	drawPanel();

	// draw the minimap
	drawOptions =0;
	globalContainer->gfx->setClipRect(globalContainer->gfx->getW()-128, 0, 128, 128);
	game.drawMiniMap(globalContainer->gfx->getW()-128, 0, 128, 128, viewportX, viewportY, team, drawOptions);

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
	if (scrollableText)
		drawInGameScrollableText();
}

void GameGUI::checkWonConditions(void)
{
	if (hasEndOfGameDialogBeenShown)
		return;
		
	if (game.totalPrestigeReached)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[Total prestige reached]"), true);
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->isAlive==false)
	{
		if (inGameMenu==IGM_NONE)
		{
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have lost]"), true);
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
		}
	}
	else if (localTeam->hasWon==true)
	{
		if (inGameMenu==IGM_NONE)
		{
			if(campaign!=NULL)
			{
				campaign->unlockAllFrom(missionName);
			}
			inGameMenu=IGM_END_OF_GAME;
			gameMenuScreen=new InGameEndOfGameScreen(Toolkit::getStringTable()->getString("[you have won]"), true);
			hasEndOfGameDialogBeenShown=true;
			miniMapPushed=false;
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
					addMessage(230, 230, 230, FormatableString("%0 : %1").arg(game.players[sp]->name).arg(mo->getText()));
			}
			else if (messageOrderType==MessageOrder::PRIVATE_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(99, 255, 242, FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[from:]")).arg(game.players[sp]->name).arg(mo->getText()));
				else if (sp==localPlayer)
				{
					Uint32 rm=mo->recepientsMask;
					int k;
					for (k=0; k<32; k++)
						if (rm==1)
						{
							addMessage(99, 255, 242, FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[to:]")).arg(game.players[k]->name).arg(mo->getText()));
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
		case ORDER_VOICE_DATA:
		{
			OrderVoiceData *ov = (OrderVoiceData *)order;
			if (ov->recepientsMask & (1<<localPlayer))
				globalContainer->mix->addVoiceData(ov);
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
		case ORDER_DECONNECTED :
		{
			int qp=order->sender;
			addMessage(200, 200, 200, FormatableString(Toolkit::getStringTable()->getString("[%0 has been deconnected of the game]")).arg(game.players[qp]->name));
			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_PLAYER_QUIT_GAME :
		{
			int qp=order->sender;
			addMessage(200, 200, 200, FormatableString(Toolkit::getStringTable()->getString("[%0 has left the game]")).arg(game.players[qp]->name));
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

void GameGUI::drawXPProgressBar(int x, int y, int act, int max)
{
	globalContainer->gfx->setClipRect(x+8, y, 112, 16);
	
	globalContainer->gfx->setClipRect(x+18, y, 92, 16);
	globalContainer->gfx->drawSprite(x+18, y+3, globalContainer->gamegui, 10);
	
	globalContainer->gfx->setClipRect(x+18, y, (act*92)/max, 16);
	globalContainer->gfx->drawSprite(x+18, y+4, globalContainer->gamegui, 11);
	
	globalContainer->gfx->setClipRect();
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
        else if (selectionMode == UNIT_SELECTION)
          {
            Unit * selUnit = selection.unit;
            assert(selUnit);
            Uint16 gid = selUnit->gid;
            /* to be safe should check if gid is valid here? */
            /* if looking at one of our pieces, continue with the next
               one of our pieces of same type, otherwise start at the
               beginning of our pieces of that type. */
            Sint32 id = ((Unit::GIDtoTeam(gid) == localTeamNo) ? Unit::GIDtoID(gid) : 0);
            /* It violates good abstraction principles that we know
               that the size of the myUnits array is 1024.  This
               information should be abstracted by some method that we
               call instead to get the next unit. */
            id %= 1024; /* just in case! */
            // std::cerr << "starting id: " << id << std::endl;
            Sint32 i = id;
            while (1)
              {
                i = ((i + 1) % 1024);
                if (i == id) break;
                // std::cerr << "trying id: " << i << std::endl;
                Unit * u = game.teams[localTeamNo]->myUnits[i];
                if (u && (u->typeNum == selUnit->typeNum))
                  {
                    // std::cerr << "found id: " << i << std::endl;
                    setSelection(UNIT_SELECTION, u);
                    centerViewportOnSelection();
                    break;
                  }
              }
          }
}

void GameGUI::centerViewportOnSelection(void)
{
  if ((selectionMode==BUILDING_SELECTION) || (selectionMode==UNIT_SELECTION))
    {
      Sint32 posX, posY;
      if (selectionMode==BUILDING_SELECTION)
        {
          Building* b=selection.building;
          //assert (selBuild);
          //Building *b=game.teams[Building::GIDtoTeam(selectionGBID)]->myBuildings[Building::GIDtoID(selectionGBID)];
          assert(b);
          posX = b->getMidX();
          posY = b->getMidY();
        }
      else if (selectionMode==UNIT_SELECTION)
        {
          Unit * u = selection.unit;
          assert (u);
          posX = u->posX;
          posY = u->posY;
        }
      /* It violates good abstraction principles that we know here
         that the size of the right panel is 128 pixels, and that each
         map cell is 32 pixels.  This information should be
         abstracted. */
      viewportX = posX - ((globalContainer->gfx->getW()-128)>>6);
      viewportY = posY - ((globalContainer->gfx->getH())>>6);
      viewportX = viewportX & game.map.getMaskW();
      viewportY = viewportY & game.map.getMaskH();
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

bool GameGUI::isBuildingEnabled(const std::string &name)
{
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
	{
		if (name == buildingsChoiceName[i])
                  return buildingsChoiceState[i];
	}
        assert (false);
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

bool GameGUI::isFlagEnabled(const std::string &name)
{
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
	{
		if (name == flagsChoiceName[i])
                  return flagsChoiceState[i];
	}
        assert (false);
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



void GameGUI::setCampaignGame(Campaign& campaign, const std::string& missionName)
{
	this->campaign=&campaign;
	this->missionName=missionName;
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

void GameGUI::addMessage(Uint8 r, Uint8 g, Uint8 b, const std::string &msgText)
{
	Message message;
	message.showTicks=Message::DEFAULT_MESSAGE_SHOW_TICKS;
	message.r = r;
	message.g = g;
	message.b = b;
	message.a = Color::ALPHA_OPAQUE;
	
	std::vector<std::string> messages;
	
	globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_BOLD, 255, 255, 255));
	setMultiLine(msgText, &messages);
	globalContainer->standardFont->popStyle();
	
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

void GameGUI::initUnitCount(void)
{
	unitCount[0] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SWARM_BUILDING][0];
	unitCount[1] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SWARM_BUILDING][1];
	unitCount[2] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][0];
	unitCount[3] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][1];
	unitCount[4] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][2];
	unitCount[5] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][3];
	unitCount[6] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][4];
	unitCount[7] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::FOOD_BUILDING][5];
	unitCount[8] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::HEAL_BUILDING][0];
	unitCount[9] = 1; // not used in settings
	unitCount[10] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::HEAL_BUILDING][2];
	unitCount[11] = 1; // not used in settings
	unitCount[12] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::HEAL_BUILDING][4];
	unitCount[13] = 1; // not used in settings
	unitCount[14] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::WALKSPEED_BUILDING][0];
	unitCount[15] = 1; // not used in settings
	unitCount[16] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::WALKSPEED_BUILDING][2];
	unitCount[17] = 1; // not used in settings
	unitCount[18] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::WALKSPEED_BUILDING][4];
	unitCount[19] = 1; // not used in settings
	unitCount[20] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SWIMSPEED_BUILDING][0];
	unitCount[21] = 1; // not used in settings
	unitCount[22] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SWIMSPEED_BUILDING][2];
	unitCount[23] = 1; // not used in settings
	unitCount[24] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SWIMSPEED_BUILDING][4];
	unitCount[25] = 1; // not used in settings
	unitCount[26] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][0];
	unitCount[27] = 1; // not used in settings
	unitCount[28] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][2];
	unitCount[29] = 1; // not used in settings
	unitCount[30] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::ATTACK_BUILDING][4];
	unitCount[31] = 1; // not used in settings
	unitCount[32] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SCIENCE_BUILDING][0];
	unitCount[33] = 1; // not used in settings
	unitCount[34] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SCIENCE_BUILDING][2];
	unitCount[35] = 1; // not used in settings
	unitCount[36] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::SCIENCE_BUILDING][4];
	unitCount[37] = 1; // not used in settings
	unitCount[38] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][0];
	unitCount[39] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][1];
	unitCount[40] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][2];
	unitCount[41] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][3];
	unitCount[42] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][4];
	unitCount[43] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::DEFENSE_BUILDING][5];
	unitCount[44] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::EXPLORATION_FLAG][0];
	unitCount[45] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::WAR_FLAG][0];
	unitCount[46] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::CLEARING_FLAG][0];
	unitCount[47] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::STONE_WALL][0];
	unitCount[48] = 1; // not used in settings
	unitCount[49] = globalContainer->settings.defaultUnitsAssigned[IntBuildingType::MARKET_BUILDING][0];
}

int GameGUI::getUnitCount(unsigned typeNum)
{
	if (typeNum < NUMBER_BUILDING_TYPE_NUM_WITH_PREDEFINED_UNIT_COUNT)
		return unitCount[typeNum];
	else
		return 1;
}

void GameGUI::setUnitCount(unsigned typeNum, int nbReq)
{
	if (typeNum < NUMBER_BUILDING_TYPE_NUM_WITH_PREDEFINED_UNIT_COUNT)
		unitCount[typeNum] = nbReq;
}
