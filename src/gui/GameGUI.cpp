// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <sstream>
#include <iostream>
#include <algorithm>
#include <optional>

#include <FileManager.h>
#include <GUITextInput.h>
#include <GUIList.h>
#include <GUIStyle.h>
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
#include "GameGUIInternal.h"
#include "GameGUILoadSave.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "IRC.h"
#include "SoundMixer.h"
#include "VoiceRecorder.h"
#include "GameGUIKeyActions.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "config.h"
#include "Order.h"

#include <SDL_keycode.h>

using std::shared_ptr;
using std::static_pointer_cast;

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
	: keyboardManager(GameGUIShortcuts), game(this), toolManager(game, brush, defaultAssign, ghostManager),
	  minimap(globalContainer->runNoX,
	         RIGHT_MENU_WIDTH, // width of the menu
	         (globalContainer->runNoX ? 0 : globalContainer->gfx->getW()), // width of the screen
	         20, // x offset
	         10, // y offset
	         128, // width
	         128, //height
	         Minimap::ShowFOW), // minimap mode

	  ghostManager(game)
{
}

GameGUI::~GameGUI()
{
	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); ++it)
		delete *it;
}

Sint32 GameGUI::displayedPosX(const Building& b) const { return ::displayedPosX(buildingGuiState, b); }
Sint32 GameGUI::displayedPosY(const Building& b) const { return ::displayedPosY(buildingGuiState, b); }
Sint32 GameGUI::displayedMaxUnitWorking(const Building& b) const { return ::displayedMaxUnitWorking(buildingGuiState, b); }
Sint32 GameGUI::displayedUnitStayRange(const Building& b) const { return ::displayedUnitStayRange(buildingGuiState, b); }

void GameGUI::init()
{
	notmenu = false;
	isRunning=true;
	gamePaused=false;
	hardPause=false;
	exitGlobCompletely=false;
	flushOutgoingAndExit=false;
	drawHealthFoodBar=true;
	drawPathLines=false;
	drawAccessibilityAids=false;
	viewportX=0;
	viewportY=0;
	mouseX=0;
	mouseY=0;
	displayMode=CONSTRUCTION_VIEW;
	replayDisplayMode=RDM_REPLAY_VIEW;
	selectionMode=NO_SELECTION;
	selectionPushed=false;
	selection.building = NULL;
	selection.unit = NULL;
	miniMapPushed=false;
	putMark=false;
	showUnitWorkingToBuilding=true;
	chatMask=0xFFFFFFFF;
	hasSpaceBeenClicked=false;
	swallowSpaceKey=false;
	scriptTextUpdated = false;

	viewportSpeedX=0;
	viewportSpeedY=0;

	showStarvingMap=false;
	showDamagedMap=false;
	showDefenseMap=false;
	showFertilityMap=false;

	inGameMenu=IGM_NONE;
	gameMenuScreen=NULL;
	typingInputScreen=NULL;
	scrollableText=NULL;
	typingInputScreenPos=0;

	eventGoTypeIterator = 0;
	localTeam=NULL;
	teamStats=NULL;

	hasEndOfGameDialogBeenShown=false;
	panPushed=false;

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

 	for (size_t i=0; i<SMOOTHED_CPU_SIZE; i++)
		smoothedCPULoad[i]=0;
	smoothedCPUPos=0;

	campaign=NULL;
	missionName="";

	scrollWheelChanges=0;

	hilights.clear();

	musicController.reset();
}

void GameGUI::adjustLocalTeam()
{
	assert(localTeamNo>=0);
	assert(localTeamNo<Team::MAX_COUNT);
	assert(game.gameHeader.getNumberOfPlayers()>0);
	assert(game.gameHeader.getNumberOfPlayers()<=Team::MAX_COUNT);
	assert(localTeamNo<game.mapHeader.getNumberOfTeams());

	localTeam = game.teams[localTeamNo];
	assert(localTeam);
	teamStats = &localTeam->stats;

	// Mirror the local-team identity onto Map so sim code can consult it without
	// reaching into the GUI layer.
	game.map.setLocalTeam(localTeamNo);

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
	viewportX=localTeam->startPosX-((globalContainer->gfx->getW()-RIGHT_MENU_WIDTH)>>6);
	viewportY=localTeam->startPosY-(globalContainer->gfx->getH()>>6);
	viewportX&=game.map.getMaskW();
	viewportY&=game.map.getMaskH();
}

std::shared_ptr<Order> GameGUI::getOrder(void)
{
	std::shared_ptr<Order> order;
	if (orderQueue.size()==0)
		order=shared_ptr<Order>(new NullOrder());
	else
	{
		order=orderQueue.front();
		orderQueue.pop_front();
	}
	return order;
}

void GameGUI::setMultiLine(const std::string &input, std::vector<std::string> *output, std::string indent)
{
	unsigned pos = 0;
	int length = globalContainer->gfx->getW()-RIGHT_MENU_WIDTH-64;

	std::string lastWord;
	std::string lastLine;
	std::string ninput=input;
	if(!ninput.empty() && ninput.back() != ' ')
		ninput += " ";

	while (pos<ninput.length())
	{
		if (ninput[pos] == ' ')
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
				lastLine = indent+lastWord;
				lastWord.clear();
			}
		}
		else
		{
			lastWord += ninput[pos];
		}
		pos++;
	}
	if (lastLine.length())
		lastLine += " ";
	lastLine += lastWord;
	if (lastLine.length())
		output->push_back(lastLine);
}

void GameGUI::publishMessageHistoryLines(const std::string& text, HistoryList target,
	const GAGCore::Color& lineColor, int lineTimeoutMs, const std::string& indent)
{
	std::vector<std::string> lines;
	setMultiLine(text, &lines, indent);

	// Reverse iteration is load-bearing: GameGUIMessageManager::addChatMessage
	// and addGameMessage both push_front, so feeding wrapped lines in
	// natural top-to-bottom order would flip them on screen. Iterate
	// tail-first so the on-screen reading order matches the source text.
	for (auto it = lines.rbegin(); it != lines.rend(); ++it)
	{
		const InGameMessage line(*it, lineColor, lineTimeoutMs);
		if (target == HistoryList::Chat)
			messageManager.addChatMessage(line);
		else
			messageManager.addGameMessage(line);
	}
}

void GameGUI::addMessage(const GAGCore::Color& color, const std::string &msgText, bool chat)
{
	// Wrap-measure the text in bold so the line breaks match the bold
	// rendering used by InGameMessage::draw. The font color pushed here is
	// irrelevant to glyph widths but matches the historical call site.
	globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_BOLD, 255, 255, 255));
	if (chat)
		publishMessageHistoryLines(msgText, HistoryList::Chat, color, kChatBroadcastTimeoutMs, "");
	else
		publishMessageHistoryLines(msgText, HistoryList::Game, color, kGameMessageDefaultTimeoutMs, "");
	globalContainer->standardFont->popStyle();
}

void GameGUI::addMark(shared_ptr<MapMarkOrder>mmo)
{
	markManager.addMark(Mark(mmo->x, mmo->y, game.teams[mmo->teamNumber]->color));
}
