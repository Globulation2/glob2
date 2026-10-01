// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
#include "MultiplayerGameScreen.h"
#include "AINames.h"
#include "CustomGameOtherOptions.h"
#include "Engine.h"
#include "GameLoadScreen.h"
#include "GameSessionScreen.h"
#include "GlobalContainer.h"
#include "MessageScreen.h"
#include "Order.h"
#include "YOGMessage.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <ScreenStack.h>
#include <StringTable.h>
#include <Toolkit.h>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;
using std::static_pointer_cast;

MultiplayerGameScreen::MultiplayerGameScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<MultiplayerGame> game,
											 std::shared_ptr<YOGClient> client, std::shared_ptr<IRCTextMessageHandler> ircChat)
	: screens(screens), client(client), game(game), gameChat(new YOGClientChatChannel(YOG_CHAT_CHANNEL_NONE, client)), ircChat(ircChat)
{
	game->addEventListener(this);
	gameChat->addListener(this);
}

MultiplayerGameScreen::~MultiplayerGameScreen()
{
	GAGCore::ApplicationHost::roomReady(false);
	game->removeEventListener(this);
	gameChat->removeListener(this);
}

std::string MultiplayerGameScreen::title() const { return fe::tr("[Game]"); }

void MultiplayerGameScreen::onTimer(Uint32)
{
	game->update();
	if (game->takeStartRequest())
		launchScheduledGame();
	if (ircChat)
		ircChat->update();
	const int percent = game->percentageDownloadFinished();
	const int shown = percent >= 0 && percent < 100 ? percent : -1;
	if (shown != downloadPercent)
	{
		downloadPercent = shown;
		refresh();
	}
}

void MultiplayerGameScreen::onActivated() { refresh(); }

bool MultiplayerGameScreen::onEscape()
{
	cancel();
	return true;
}

void MultiplayerGameScreen::cancel()
{
	game->leaveGame();
	finish(Cancelled);
}

void MultiplayerGameScreen::sendChat()
{
	if (chatDraft.empty())
		return;
	std::shared_ptr<YOGMessage> message(new YOGMessage(chatDraft, game->getUsername(), YOGNormalMessage));
	gameChat->sendMessage(message);
	chatDraft.clear();
	refresh();
}

Element MultiplayerGameScreen::build(const Presentation &p)
{
	GameHeader &gh = game->getGameHeader();
	MapHeader &mh = game->getMapHeader();
	const bool readyToGo = game->getGameJoinCreationState() == MultiplayerGame::ReadyToGo;
	gameChat->setChannelID(game->getChatChannel());
	std::vector<std::string> teams;
	for (int j = 0; j < mh.getNumberOfTeams(); ++j)
		teams.push_back(fe::tr("[Team]") + " " + std::to_string(j + 1));
	std::vector<Element> players;
	for (int i = 0; i < Team::MAX_COUNT; ++i)
	{
		BasePlayer &bp = gh.getBasePlayer(i);
		const std::string id = "player/" + std::to_string(i);
		if (bp.type != BasePlayer::P_NONE)
		{
			const bool playerReady = game->isReadyToStart(bp.playerID);
			fe::TextOptions nameStyle;
			if (!playerReady)
				nameStyle.color = fe::frontendTheme().palette.danger;
			std::vector<Element> cells;
			if (bp.teamNumber >= 0 && bp.teamNumber < mh.getNumberOfTeams())
				cells.push_back(fe::swatch(mh.getBaseTeam(bp.teamNumber).color, 20));
			fe::ChoiceOptions teamOptions;
			teamOptions.controlEnabled = hosting();
			cells.push_back(fe::width(p.pt(110), fe::choice(id + "/team", teams, bp.teamNumber, [this, i](int team) { game->changeTeam(i, team); }, teamOptions)));
			cells.push_back(fe::expanded(fe::label(bp.name, nameStyle)));
			if (hosting() && bp.number != game->getLocalPlayerNumber())
				cells.push_back(fe::button(id + "/kick", fe::tr("[kick]"), [this, i] { game->kickPlayer(i); }));
			players.push_back(fe::row(std::move(cells), {p.pt(8), fe::CrossAlign::Center}));
		}
		else if (i < mh.getNumberOfTeams())
			players.push_back(fe::caption(fe::tr("[open]")));
	}
	std::vector<Element> side;
	if (!gh.getExperiments().empty())
		side.push_back(fe::paragraph(fe::tr("[Experiments set by the host]") + ": " + experimentLabelList(gh.getExperiments()),
									 {fe::FontRole::Support}));
	if (readyToGo && hosting())
	{
		std::vector<Element> ais;
		for (std::size_t i = 1; i < AI::JAVASCRIPT; ++i)
			ais.push_back(fe::button("ai/" + std::to_string(i), AINames::getAIText(int(i)), [this, i] { game->addAIPlayer((AI::ImplementationID)i); }));
		side.push_back(fe::label(fe::tr("[Add AI]")));
		side.push_back(fe::wrap(std::move(ais), {-1, p.pt(140)}));
	}
	if (!hosting() && readyToGo)
		side.push_back(fe::toggle("ready", fe::tr("[ready?]"), ready, [this](bool v)
								  {
									  ready = v;
									  game->setHumanReady(v);
								  }));
	if (downloadPercent >= 0)
		side.push_back(fe::progress(downloadPercent, 100, GAGCore::FormattableString(fe::tr("[downloaded %0]")).arg(downloadPercent)));
	if (!game->isGameStarting())
		side.push_back(fe::button("options", fe::tr("[Other Options]"),
								  [this]
								  {
									  screens.push(std::make_unique<CustomGameOtherOptions>(game->getGameHeader(), game->getMapHeader(), !hosting()),
												   [this](GAGGUI::Screen &, int) { game->updateGameHeader(); });
								  }));
	if (game->isGameStarting())
		side.push_back(fe::paragraph(fe::tr("[Waiting]")));
	else if (!game->isGameReadyToStart())
		side.push_back(fe::paragraph(fe::tr("[not ready]"), {fe::FontRole::Body, true}));
	const bool canStart = game->isGameReadyToStart() && hosting() && !game->isGameStarting();
	GAGCore::ApplicationHost::roomReady(canStart);
	fe::TextFieldOptions chatOptions;
	chatOptions.maxLength = ORDER_TEXT_MESSAGE_MAX_LEN;
	chatOptions.submit = [this](const std::string &) { sendChat(); };
	auto chat = fe::column({fe::expanded(fe::textEditor("chat/log", chatLog, {}, {true, 8, false, true})),
							fe::textField("chat/input", chatDraft, [this](const std::string &v) { chatDraft = v; }, chatOptions)},
						   {p.pt(6)});
	auto playerColumn = fe::column({fe::heading(fe::tr("[awaiting players]")), fe::column(std::move(players), {p.pt(4)}), fe::column(std::move(side), {p.pt(8)})}, {p.pt(8)});
	Element body = fe::adaptive(
		[playerColumn, chat](const fe::LayoutContext &ctx, fe::Size available) -> fe::Element
		{
			if (available.w < ctx.presentation.pt(720))
				return fe::scroll("room/scroll", fe::column({playerColumn, fe::height(ctx.presentation.pt(240), chat)}, {ctx.presentation.pt(12)}));
			return fe::row({fe::expanded(fe::scroll("room/scroll", playerColumn)), fe::expanded(chat)}, {ctx.presentation.pt(16), fe::CrossAlign::Stretch});
		});
	std::vector<fe::MenuAction> buttons;
	if (readyToGo)
		buttons.push_back({"cancel", fe::tr(hosting() ? "[Cancel]" : "[Leave Game]"), [this] { cancel(); }, false, SDLK_ESCAPE});
	if (canStart)
		buttons.push_back({"start", fe::tr("[Start]"), [this]
						   {
							   // MultiplayerGame will send an event when the game is over.
							   game->startGame();
							   refresh();
						   },
						   true});
	return fe::column({fe::expanded(body), fe::divider(), fe::actions(std::move(buttons), p)}, {p.pt(8)});
}

void MultiplayerGameScreen::receiveTextMessage(std::shared_ptr<YOGMessage> message)
{
	chatLog += message->formatForReading() + "\n";
	refresh();
}

void MultiplayerGameScreen::handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event)
{
	const Uint8 type = event->getEventType();
	if (type == MGEGameStarted)
	{
		if (ircChat)
			ircChat->stopIRC();
	}
	else if (type == MGEGameExit)
	{
		if (ircChat)
			ircChat->startIRC(game->getUsername());
		finish(-1);
		game->leaveGame();
	}
	else if (type == MGEGameEndedNormally)
	{
		if (ircChat)
			ircChat->startIRC(game->getUsername());
		finish(StartedGame);
		game->leaveGame();
	}
	else if (type == MGEGameRefused)
		finish(GameRefused);
	else if (type == MGEKickedByHost)
		finish(Kicked);
	else if (type == MGEHostCancelledGame)
		finish(GameCancelled);
	else if (type == MGEServerDisconnected)
		finish(ServerDisconnected);
	// Every other event changes something the room shows (players, readiness, download).
	refresh();
}

void MultiplayerGameScreen::launchScheduledGame()
{
	GAGCore::ApplicationHost::roomReady(false);
	screens.push(std::make_unique<GameLoadScreen>([game = game, client = client](Engine &engine)
												  { return engine.initMultiplayerTask(game, client, game->getLocalPlayer()); }),
				 [this](GAGGUI::Screen &load, int result)
				 {
					 if (result != 1)
					 {
						 game->sessionEnded(false);
						 if (result == 2)
							 screens.push(std::make_unique<MessageScreen>(fe::tr("[ERROR_CANT_LOAD_MAP]"), std::vector<std::string>{fe::tr("[ok]")}));
						 return;
					 }
					 auto engine = static_cast<GameLoadScreen &>(load).takeEngine();
					 game->sessionStarted();
					 screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)),
								  [this](GAGGUI::Screen &, int result) { game->sessionEnded(result == GAGGUI::Screen::QUIT_APPLICATION); });
				 });
}
