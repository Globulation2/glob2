// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __MultiplayerGameScreen_h
#define __MultiplayerGameScreen_h

#include <vector>
#include "MultiplayerGame.h"
#include "AI.h"
#include "MapHeader.h"
#include "YOGClientChatChannel.h"
#include "YOGClientChatListener.h"
#include "MultiplayerGameEventListener.h"
#include "IRCTextMessageHandler.h"
#include "GUITabScreenWindow.h"

namespace GAGGUI
{
	class Text;
	class TextArea;
	class TextInput;
	class TextButton;
	class ColorButton;
	class OnOffButton;
	class ProgressBar;
}

using namespace GAGGUI;

///This screen is the setup screen for a multiplayer game. It functions both for the host
///and the joined player. It uses the information it gets from the given MultiplayerGame.
///This doesn't continue dispaying irc, it merely keeps it up to date and turns it on/off
///when starting and finishing games
class MultiplayerGameScreen : public TabScreenWindow, public YOGClientChatListener, public MultiplayerGameEventListener
{
public:
	///The screen must be provided with the client, the irc connection and the multiplayer game
	MultiplayerGameScreen(TabScreen* parent, std::shared_ptr<MultiplayerGame> game, std::shared_ptr<YOGClient> client, std::shared_ptr<IRCTextMessageHandler> ircChat = std::shared_ptr<IRCTextMessageHandler>());
	virtual ~MultiplayerGameScreen();

	enum
	{
		Cancelled,
		StartedGame,
		GameRefused,
		Kicked,
		GameCancelled,
		ServerDisconnected,
	};

private:
	enum
	{
		START = 1,
		CANCEL = 2,
		STARTED=3,
		OTHEROPTIONS=4,
		READY=5,
		
		COLOR_BUTTONS=32,
		CLOSE_BUTTONS=64,
		
		
		ADD_AI = 100
	};

	enum { MAX_NUMBER_OF_PLAYERS = 16};

	void onTimer(Uint32 tick);
	void onAction(Widget *source, Action action, int par1, int par2);

	void recieveTextMessage(std::shared_ptr<YOGMessage> message);

	void handleMultiplayerGameEvent(std::shared_ptr<MultiplayerGameEvent> event);

	///This function will update the list of joined players
	void updateJoinedPlayers();
	void updateVisibleButtons();
	
	virtual void onActivated();

	TextButton *startButton;
	TextButton *cancelButton;
	std::vector<TextButton *> addAI;
	ColorButton *color[MAX_NUMBER_OF_PLAYERS];
	Text *text[MAX_NUMBER_OF_PLAYERS];
	TextButton *kickButton[MAX_NUMBER_OF_PLAYERS];
	ProgressBar *percentDownloaded;
	TextButton *otherOptions;

	TextInput *textInput;
	TextArea *chatWindow;
	
	OnOffButton *isReady;
	Text *isReadyText;

	std::shared_ptr<MultiplayerGame> game;

	bool wasSlotUsed[MAX_NUMBER_OF_PLAYERS];
	Text *notReadyText;
	Text *gameStartWaitingText;

	std::shared_ptr<YOGClientChatChannel> gameChat;
	std::shared_ptr<IRCTextMessageHandler> ircChat;
};
#endif
