// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef IRCThread_h
#define IRCThread_h

#include "IRC.h"
#include <memory>
#include <mutex>
#include <queue>

class IRCThreadMessage;

///IRC thread manages IRC
class IRCThread
{
public:
	IRCThread(std::queue<std::shared_ptr<IRCThreadMessage> >& outgoing, std::recursive_mutex& outgoingMutex);
	
	///Runs the IRC thread
	void operator()();

	///Sends this IRC thread a message
	void sendMessage(std::shared_ptr<IRCThreadMessage> message);

	///This returns whether the thread has exited
	bool hasThreadExited();
private:
	///Sends this IRC message back to the main thread
	void sendToMainThread(std::shared_ptr<IRCThreadMessage> message);

	IRC irc;
	std::string channel;
	
	std::queue<std::shared_ptr<IRCThreadMessage> > incoming;
	std::queue<std::shared_ptr<IRCThreadMessage> >& outgoing;
	std::recursive_mutex incomingMutex;
	std::recursive_mutex& outgoingMutex;
	bool hasExited;
};

#endif
