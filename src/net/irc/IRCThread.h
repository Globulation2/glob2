// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#pragma once

#include "IRC.h"
#include "ThreadMessageQueues.h"

class IRCThreadMessage;

///IRC thread manages IRC
class IRCThread : public ThreadMessageQueues<IRCThreadMessage>
{
public:
	IRCThread(std::queue<std::shared_ptr<IRCThreadMessage> >& outgoing, std::recursive_mutex& outgoingMutex);

	///Runs the IRC thread
	void operator()();

private:
	IRC irc;
	std::string channel;
};
