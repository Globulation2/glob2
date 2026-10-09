#include "hive/HiveClient.h"
#include "hive/HiveDialog.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include "ConnectionOverlay.h"
#include <stdarg.h>
#include <math.h>


#include <BackgroundFileWriter.h>
#include <SDL3/SDL.h>
#include <StringTable.h>
#include <Toolkit.h>

#include "Game.h"
#include "GameGUI.h"
#include "online/SkinDownloads.h"
#include "render/ColonySkinPreview.h"
#include "GameGUITouch.h"
#include "HudUnitConversionIcon.h"
#include "GameGUIDialog.h"
#include "GameGUIInternal.h"
#include "LoadSaveDialog.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include <glob2/BuildConfig.h>
#include "Order.h"


using std::shared_ptr;
using std::static_pointer_cast;

GameGUI::GameGUI(bool persistPreferences)
	: keyboardManager(GameGUIShortcuts), game(this), defaultAssign(game), toolManager(game, brush, defaultAssign, ghostManager),
	  minimap(globalContainer->runNoX,
	         RIGHT_MENU_WIDTH, // width of the menu
	         (globalContainer->runNoX ? 0 : globalContainer->gfx->getW()), // width of the screen
	         20, // x offset
	         10, // y offset
	         128, // width
	         128, //height
	         Minimap::ShowFOW) // minimap mode
{
	this->persistPreferences = persistPreferences;
}

void GameGUI::addNotice(const std::string &text)
{
	addMessage(GAGCore::Color(200, 200, 200), text, false);
}

bool GameGUI::pauseAvailable() const
{
	if (gamePaused || !networkMatch.active || !pauseState)
		return true; // anyone may resume; elsewhere pausing is unlimited
	const PauseState state = pauseState();
	return !state.limited || (state.pausesLeft > 0 && state.secondsLeft > 0);
}

void GameGUI::requestPause(bool pause)
{
	if (pause && !pauseAvailable())
	{
		addNotice(Toolkit::getStringTable()->getString("[turn no pauses left]"));
		return;
	}
	enqueueOrder(std::make_shared<PauseGameOrder>(pause));
}

GameGUI::~GameGUI()
{
	if (!globalContainer->runNoX) Sprite::requestHighResolution(false);
	for (ParticleSet::iterator it = particles.begin(); it != particles.end(); ++it)
		delete *it;
	if (persistPreferences && globalContainer->settings.rememberUnit)
		globalContainer->settings.save();
}

Sint32 GameGUI::displayedPosX(const Building& b) const { return ::displayedPosX(buildingGuiState, b); }
Sint32 GameGUI::displayedPosY(const Building& b) const { return ::displayedPosY(buildingGuiState, b); }
Sint32 GameGUI::displayedMaxUnitWorking(const Building& b) const { return ::displayedMaxUnitWorking(buildingGuiState, b); }
Sint32 GameGUI::displayedUnitStayRange(const Building& b) const { return ::displayedUnitStayRange(buildingGuiState, b); }
Sint32 GameGUI::displayedPriority(const Building& b) const { return ::displayedPriority(buildingGuiState, b); }
bool GameGUI::displayedClearingResource(const Building& b, int i) const { return ::displayedClearingResource(buildingGuiState, b, i); }
Sint32 GameGUI::displayedMinLevelToFlag(const Building& b) const { return ::displayedMinLevelToFlag(buildingGuiState, b); }
std::array<Sint32, NB_UNIT_TYPE> GameGUI::displayedRatio(const Building& b) const { return ::displayedRatio(buildingGuiState, b); }

void GameGUI::init()
{
	torusView.reset();
	torusPointerDown = false;
	camera=MapCamera();zoomControlPushed=false;
	if (!globalContainer->runNoX) Sprite::requestHighResolution(globalContainer->settings.highResolutionArtwork);
    touch = std::make_unique<GameGUITouch>(*this);
	unitConversionIcon = std::make_unique<HudUnitConversionIcon>();
	notmenu = false;
	isRunning=true;
	gamePaused=false;
	hardPause=false;
	anyPlayerWaitedTimeFor=0;
	lastAutosaveStep=-1;
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
	selection = std::monostate{};
	// Reset the per-client view scratch (selection, mouse and map render state)
	// the render path reads. Formerly cleared by Game::clearGame when these fields lived on
	// Game; now front-end-owned, so reset it here. See CS-661.
	view = Game::ViewState{};
	miniMapPushed=false;
	putMark=false;
	showUnitWorkingToBuilding=true;
	chatMask=0xFFFFFFFF;
	// A new game starts with empty client channels.
	clientEvents.reset();
	clientRequests.reset();
	clientRequests.publishDisplaySize(globalContainer->gfx ? std::max(0, globalContainer->gfx->getW()-RIGHT_MENU_WIDTH) : 0,
                                     globalContainer->gfx ? globalContainer->gfx->getH() : 0);
	for (auto &queue : pendingTeamEvents)
		queue.clear();
	eventFeed.clear();
	eventFeedHits.clear();
	swallowSpaceKey=false;
	scriptText.clear();
	scriptTextUpdated = false;

	viewportSpeedX=0;
	viewportSpeedY=0;
	lastViewportStep=SDL_GetTicks();

	showStarvingMap=false;
	showDamagedMap=false;
	showDefenseMap=false;
	showFertilityMap=false;

	inGameMenu=IGM_NONE;
	gameMenuScreen.reset();
	typingInputScreen.reset();
	scrollableText.reset();

	eventGoTypeIterator = 0;
	localTeam=NULL;
	teamStats=NULL;

	hasEndOfGameDialogBeenShown=false;
	panPushed=false;
	mapPanPushed=false;

	rebuildBuildingChoices();

	hiddenGUIElements=0;


	campaign=NULL;
	missionName="";


	highlights.clear();

	musicController.reset();
}

void GameGUI::rebuildBuildingChoices(bool preserve)
{
	buildingChoiceRow=flagChoiceRow=0;
	std::map<std::string,bool> previous;
	if (preserve)
	{
		for (size_t i=0; i<buildingsChoiceName.size(); ++i) previous[buildingsChoiceName[i]]=buildingsChoiceState[i];
		for (size_t i=0; i<flagsChoiceName.size(); ++i) previous[flagsChoiceName[i]]=flagsChoiceState[i];
	}
	buildingsChoiceName.clear(); flagsChoiceName.clear();
	choiceCatalog = game.buildingsTypes.retainTypes();
	for (size_t id=0; id<game.buildingsTypes.size(); ++id)
	{
		const auto* type=game.buildingsTypes.get(id);
		if (!type->semantics.placeable || !type->runtimeAvailable) continue;
		(type->isVirtual ? flagsChoiceName : buildingsChoiceName).push_back(type->key);
	}
	buildingsChoiceState.assign(buildingsChoiceName.size(),true);
	flagsChoiceState.assign(flagsChoiceName.size(),true);
	for (size_t i=0; i<buildingsChoiceName.size(); ++i)
		if (auto found=previous.find(buildingsChoiceName[i]); found!=previous.end()) buildingsChoiceState[i]=found->second;
	for (size_t i=0; i<flagsChoiceName.size(); ++i)
		if (auto found=previous.find(flagsChoiceName[i]); found!=previous.end()) flagsChoiceState[i]=found->second;
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

	// set default event position
	eventGoPosX = localTeam->startPosX;
	eventGoPosY = localTeam->startPosY;
	eventGoType = 0;
}

void GameGUI::selectViewedTeam(int team)
{
    const auto& frame = drawnScene();
    if (!frame.world.teams || !frame.world.session || team < 0 ||
        size_t(team) >= frame.world.teams->values.size()) return;
    clearSelection();
    localTeamNo = team;
    const auto& shown = frame.world.teams->values[team];
    eventGoPosX = shown.startX;
    eventGoPosY = shown.startY;
    eventGoType = 0;
    const auto& players = frame.world.session->players;
    for (size_t i = 0; i < players.size(); ++i)
        if (players[i].teamNumber == team) { localPlayer = int(i); break; }
    if (globalContainer->replayVisibleTeams != 0xffffffff)
        globalContainer->replayVisibleTeams = shown.mask;
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
	if (globalContainer->liveSpectating) { orderQueue.clear(); return std::make_shared<NullOrder>(); }
	while (auto order = orderQueue.take())
	{
		if (order->clientWorld && order->clientWorld != game.map.identity()) continue;
		if (order->clientTarget && !game.resolveBuilding(*order->clientTarget)) continue;
		return order;
	}
	return std::make_shared<NullOrder>();
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
    // Headless simulations execute the order but have no font or message UI.
    if (globalContainer->runNoX) return;
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
	if (mmo->teamNumber < drawnScene().entities.teamCount)
		markManager.addMark(Mark(mmo->x, mmo->y, presentationColor(drawnScene().entities.teams[mmo->teamNumber].color)));
}

void GameGUI::updateCamera()
{
    // Client input can run before the first asynchronous scene arrives. Keep
    // its camera origin in sync so scrolling cannot overwrite the initial view.
    if (camera.tileX()!=viewportX) camera.originX=viewportX*32.0+camera.fractionX();
    if (camera.tileY()!=viewportY) camera.originY=viewportY*32.0+camera.fractionY();
    const auto& map = drawnScene().map;
    if (!map.getW() || !map.getH()) return;
    if (touch && touch->usesHUD()) {
        const auto bounds=touch->worldBounds();
        camera.resize(bounds.w,bounds.h,map.getW()*32.0,map.getH()*32.0,bounds.x,bounds.y);
    } else camera.resize(globalContainer->gfx->getW()-RIGHT_MENU_WIDTH,globalContainer->gfx->getH(),map.getW()*32.0,map.getH()*32.0);
    viewportX=camera.tileX();viewportY=camera.tileY();
    clientRequests.publishDisplaySize(std::ceil(camera.visibleW()+camera.fractionX()),
                                      std::ceil(camera.visibleH()+camera.fractionY()));
    view.mouseX=mapMouseX(mouseX);view.mouseY=mapMouseY(mouseY);
}
bool GameGUI::zoomMap(double steps,int x,int y)
{
    updateCamera();
    if (torusView.active())
    {
        // The ring shares the camera's zoom and stays centred on its focus,
        // so it zooms about that point wherever the pointer is. An automatic
        // reveal is over too soon to zoom.
        const int width=globalContainer->gfx->getW()-RIGHT_MENU_WIDTH, height=globalContainer->gfx->getH();
        if (!torusView.enabled() || x<0 || x>=width || y<16 || y>=height) return false;
        camera.wheel(steps,width/2.0,(height+16)/2.0);
    }
    else if (y<16 || !camera.contains(x,y)) return false;
    else camera.wheel(steps,x,y);
    viewportX=camera.tileX();viewportY=camera.tileY();
    torusView.rebaseViewport(viewportX,viewportY);
    clientRequests.publishDisplaySize(std::ceil(camera.visibleW()+camera.fractionX()),
                                      std::ceil(camera.visibleH()+camera.fractionY()));
    view.mouseX=mapMouseX(mouseX);view.mouseY=mapMouseY(mouseY);
    return true;
}

void GameGUI::configureLiveSpectatorView()
{
	minimap.setMinimapMode(Minimap::HideFOW);
}

void GameGUI::setColonySkins(std::unique_ptr<Online::SkinDownloads> downloads)
{
    view.render.skinPreview().setDownloads(std::move(downloads));
}

void GameGUI::swapColonyAppearance(MapRenderState &state)
{
    view.render.swapSkinPreview(state);
}

// Assistant orders share the human input channel. Allocate the whole batch before
// publishing any order; allocation failure must not submit a partial command.
bool GameGUI::enqueueCommanderOrders(const std::vector<std::shared_ptr<Order>> &orders, const std::function<bool()> &commit)
{
 if(globalContainer->replaying || globalContainer->liveSpectating || orders.size()>32 || orderQueue.size()+orders.size()>64)
  return false;
 std::list<std::shared_ptr<Order>> prepared(orders.begin(),orders.end());
 if(!commit())return false;
 for (const auto& order : prepared) stampClientOrder(order, true);
 orderQueue.append(prepared);
 return true;
}

void GameGUI::updateCommander(bool caughtUp)
{
 if(!hive)return;
 hive->update(caughtUp);
 if(!hiveCards && hive->available() && !globalContainer->runNoX) {
 hiveCards=std::make_unique<Hive::Dialog>(hive); hiveCards->compose=[this]{openCommander();}; hiveCards->attach(*globalContainer->gfx);
 }
 if(hiveCards)hiveCards->invalidate();
}
