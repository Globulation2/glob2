// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "GameGUIMessageManager.h"
#include "GlobalContainer.h"
#include "SDLCompat.h"

InGameMessage::InGameMessage(const std::string& text, const GAGCore::Color& color, int time)
 : timeLeft(time), text(text), color(color)
{
	lastTime = 0;
}



std::string InGameMessage::getText() const
{
	return text;
}



void InGameMessage::draw(int x, int y)
{
	Uint64 newTime = SDL_GetTicks64();
	if(lastTime != 0)
	{
		timeLeft -= std::max<Sint64>(static_cast<Sint64>(newTime) - static_cast<Sint64>(lastTime), 0);
		timeLeft = std::max(0, timeLeft);
	}
	lastTime = newTime;

	globalContainer->standardFont->pushStyle(Font::Style(Font::STYLE_BOLD, color));
	globalContainer->gfx->drawString(x, y, globalContainer->standardFont, text.c_str());
	globalContainer->standardFont->popStyle();
}



GameGUIMessageManager::GameGUIMessageManager()
{

}


	
void GameGUIMessageManager::addGameMessage(const InGameMessage& message)
{
	historyGame.push_front(message);
}


	
void GameGUIMessageManager::addChatMessage(const InGameMessage& message)
{
	historyChat.push_front(message);
}



void GameGUIMessageManager::drawAllGameMessages(int x, int y)
{
	for (std::list <InGameMessage>::iterator i=historyGame.begin(); i!=historyGame.end(); ++i)
	{
		if(i->timeLeft != 0)
		{
			i->draw(x, y);
			y += 20;
		}
	}
}



void GameGUIMessageManager::drawAllChatMessages(int x, int y)
{
	for (std::list <InGameMessage>::iterator i=historyChat.begin(); i!=historyChat.end(); ++i)
	{
		if(i->timeLeft != 0)
		{
			i->draw(x, y);
			y += 20;
		}
	}
}



InGameScrollableHistory* GameGUIMessageManager::createScrollableHistoryScreen()
{
	return new InGameScrollableHistory(historyChat);
}

InGameScrollableHistory::InGameScrollableHistory(const std::list<InGameMessage>& messageHistory)
	: history(messageHistory), lastSize(messageHistory.size())
{
}

void InGameScrollableHistory::onUpdate(Uint32)
{
	if (lastSize != history.size())
	{
		lastSize = history.size();
		invalidate();
	}
}

GAGGUI::ui::Rect InGameScrollableHistory::place(GAGGUI::ui::Size measured, GAGGUI::ui::Rect area)
{
	const int h = std::min(area.h, measured.h);
	return {area.x, area.bottom() - h, area.w, h};
}

Glob2UI::Element InGameScrollableHistory::build(const Glob2UI::Presentation &p)
{
	std::string log;
	for (const auto &message : history)
		log += message.getText() + "\n";
	Glob2UI::TextEditorOptions options;
	options.readOnly = true;
	options.lines = 6;
	options.scrollToEnd = true;
	return Glob2UI::column({Glob2UI::label(Glob2UI::tr("[Message history]"), {Glob2UI::FontRole::Support, true}),
							Glob2UI::textEditor("history", log, {}, options),
							dialogActions({{"ok", Glob2UI::tr("[ok]"), [this] { finish(0); }, true, SDLK_ESCAPE}}, p)},
						   {p.pt(6)});
}
