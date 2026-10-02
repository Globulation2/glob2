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
#include "YogRoom.h"
#include <ApplicationHost.h>
#include <FormatableString.h>
#include <ScreenStack.h>
#include <StringTable.h>
#include <Toolkit.h>

namespace fe = Glob2UI;
using fe::Element;
using fe::Presentation;

MultiplayerGameScreen::MultiplayerGameScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<RoomBackend> room)
	: screens(screens), room(std::move(room))
{
}

MultiplayerGameScreen::MultiplayerGameScreen(GAGGUI::ScreenStack &screens, std::shared_ptr<MultiplayerGame> game,
											 std::shared_ptr<YOGClient> client, std::shared_ptr<IRCTextMessageHandler> ircChat)
	: MultiplayerGameScreen(screens, std::make_shared<YogRoom>(std::move(game), std::move(client), std::move(ircChat)))
{
}

MultiplayerGameScreen::~MultiplayerGameScreen()
{
	GAGCore::ApplicationHost::roomReady(false);
}

std::string MultiplayerGameScreen::title() const { return fe::tr("[Game]"); }

void MultiplayerGameScreen::onTimer(Uint32)
{
	room->update();
	bool changed = false;
	while (auto event = room->takeEvent())
	{
		switch (event->kind)
		{
		case RoomBackend::Event::Changed:
			changed = true;
			break;
		case RoomBackend::Event::Chat:
			chatLog += event->text + "\n";
			changed = true;
			break;
		case RoomBackend::Event::Launch:
			launchScheduledGame();
			break;
		case RoomBackend::Event::Message:
			screens.push(std::make_unique<MessageScreen>(event->text, std::vector<std::string>{fe::tr("[ok]")}));
			break;
		case RoomBackend::Event::Finished:
			finish(event->code);
			changed = true;
			break;
		}
	}
	const int shown = room->downloadPercent();
	if (shown != downloadPercent)
	{
		downloadPercent = shown;
		changed = true;
	}
	if (changed)
		refresh();
}

void MultiplayerGameScreen::onActivated() { refresh(); }

bool MultiplayerGameScreen::onEscape()
{
	cancel();
	return true;
}

void MultiplayerGameScreen::cancel()
{
	room->leave();
	finish(Cancelled);
}

void MultiplayerGameScreen::sendChat()
{
	if (chatDraft.empty())
		return;
	room->sendChat(chatDraft);
	chatDraft.clear();
	refresh();
}

Element MultiplayerGameScreen::build(const Presentation &p)
{
	const bool readyToGo = room->lobbyReady();
	const bool hosting = room->isHost();
	std::vector<std::string> teams;
	for (int j = 0; j < room->teamCount(); ++j)
		teams.push_back(fe::tr("[Team]") + " " + std::to_string(j + 1));
	std::vector<Element> players;
	for (const auto &slot : room->slots())
	{
		const std::string id = "player/" + std::to_string(slot.index);
		if (slot.open)
		{
			players.push_back(fe::caption(fe::tr("[open]")));
			continue;
		}
		fe::TextOptions nameStyle;
		if (!slot.ready)
			nameStyle.color = fe::frontendTheme().palette.danger;
		std::vector<Element> cells;
		if (auto color = room->teamColor(slot.team))
			cells.push_back(fe::swatch(GAGCore::Color((*color)[0], (*color)[1], (*color)[2]), 20));
		fe::ChoiceOptions teamOptions;
		teamOptions.controlEnabled = room->canChangeTeam(slot);
		const int index = slot.index;
		cells.push_back(fe::width(p.pt(110), fe::choice(id + "/team", teams, slot.team, [this, index](int team) { room->changeTeam(index, team); }, teamOptions)));
		cells.push_back(fe::expanded(fe::label(slot.name, nameStyle)));
		if (room->canKick(slot))
			cells.push_back(fe::button(id + "/kick", fe::tr("[kick]"), [this, index] { room->kick(index); }));
		players.push_back(fe::row(std::move(cells), {p.pt(8), fe::CrossAlign::Center}));
	}
	std::vector<Element> side;
	if (const auto pairing = room->shareText(); !pairing.empty())
	{
		side.push_back(fe::paragraph(fe::tr("[lan share pairing]")));
		side.push_back(fe::paragraph(pairing, {fe::FontRole::Support}));
		side.push_back(fe::button("lan/copy-pairing", fe::tr("[lan copy pairing]"), [pairing] {
			SDL_SetClipboardText(pairing.c_str());
		}));
	}
	if (const auto experiments = room->experimentsLabel(); !experiments.empty())
		side.push_back(fe::paragraph(fe::tr("[Experiments set by the host]") + ": " + experiments, {fe::FontRole::Support}));
	if (readyToGo && room->canAddAI())
	{
		std::vector<Element> ais;
		for (std::size_t i = 1; i < AI::JAVASCRIPT; ++i)
			ais.push_back(fe::button("ai/" + std::to_string(i), AINames::getAIText(int(i)), [this, i] { room->addAI((AI::ImplementationID)i); }));
		side.push_back(fe::label(fe::tr("[Add AI]")));
		side.push_back(fe::wrap(std::move(ais), {-1, p.pt(140)}));
	}
	if (!hosting && readyToGo)
		side.push_back(fe::toggle("ready", fe::tr("[ready?]"), ready, [this](bool v)
								  {
									  ready = v;
									  room->setReady(v);
								  }));
	if (downloadPercent >= 0)
		side.push_back(fe::progress(downloadPercent, 100, GAGCore::FormattableString(fe::tr("[downloaded %0]")).arg(downloadPercent)));
	if (!room->starting())
		side.push_back(fe::button("options", fe::tr("[Other Options]"),
								  [this]
								  {
									  GameHeader *header = room->optionsHeader();
									  if (!header)
										  return;
									  screens.push(std::make_unique<CustomGameOtherOptions>(*header, *room->optionsMap(), room->optionsReadOnly()),
												   [this](GAGGUI::Screen &, int) { room->optionsChanged(); });
								  }));
	if (room->starting())
		side.push_back(fe::paragraph(fe::tr("[Waiting]")));
	else if (!room->everyoneReady())
		side.push_back(fe::paragraph(fe::tr("[not ready]"), {fe::FontRole::Body, true}));
	const bool canStart = room->canStart();
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
		buttons.push_back({"cancel", fe::tr(hosting ? "[Cancel]" : "[Leave Game]"), [this] { cancel(); }, false, SDLK_ESCAPE});
	if (canStart)
		buttons.push_back({"start", fe::tr("[Start]"), [this]
						   {
							   room->start();
							   refresh();
						   },
						   true});
	return fe::column({fe::expanded(body), fe::divider(), fe::actions(std::move(buttons), p)}, {p.pt(8)});
}

void MultiplayerGameScreen::launchScheduledGame()
{
	GAGCore::ApplicationHost::roomReady(false);
	screens.push(std::make_unique<GameLoadScreen>([room = room](Engine &engine) { return room->initGame(engine); }),
				 [this](GAGGUI::Screen &load, int result)
				 {
					 if (result != 1)
					 {
						 room->gameStarted(false);
						 if (result == 2)
							 screens.push(std::make_unique<MessageScreen>(static_cast<GameLoadScreen &>(load).failureMessage(), std::vector<std::string>{fe::tr("[ok]")}));
						 return;
					 }
					 auto engine = static_cast<GameLoadScreen &>(load).takeEngine();
					 room->gameStarted(true);
					 screens.push(std::make_unique<GameSessionScreen>(screens, std::move(engine)),
								  [this](GAGGUI::Screen &, int result) { room->gameEnded(result == GAGGUI::Screen::QUIT_APPLICATION); });
				 });
}
