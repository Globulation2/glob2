/*
  Copyright (C) 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "GameGUIMessageManager.h"
#include "GlobalContainer.h"
#include "GUIList.h"
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
	return new InGameScrollableHistory(globalContainer->gfx, historyChat);
}


InGameScrollableHistory::InGameScrollableHistory(GraphicContext *context, const std::list<InGameMessage>& messageHistory)
: OverlayScreen(context, (globalContainer->gfx->getW()-172), 100), history(messageHistory)
{
	messageList=new List(0, 0, (globalContainer->gfx->getW()-172), 100, 0, 0, "standard");
	addWidget(messageList);
	updateList();
	dispatchInit();
}



void InGameScrollableHistory::onAction(Widget *source, Action action, int par1, int par2)
{

}


void InGameScrollableHistory::onTimer(Uint32 tick)
{
	if(lastSize != history.size())
		updateList();
}


void InGameScrollableHistory::updateList()
{
	messageList->clear();
	for(std::list<InGameMessage>::const_iterator i = history.begin(); i!=history.end(); ++i)
	{
		messageList->addText(i->getText());
	}
	lastSize = history.size();
}
