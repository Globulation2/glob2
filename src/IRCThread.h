/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef IRCThread_h
#define IRCThread_h

#include "IRCThreadMessage.h"
#include "IRC.h"
#include <boost/shared_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <queue>

///IRC thread manages IRC
class IRCThread
{
public:
	IRCThread(std::queue<boost::shared_ptr<IRCThreadMessage> >& outgoing, boost::recursive_mutex& outgoingMutex);
	
	///Runs the IRC thread
	void operator()();

	///Sends this IRC thread a message
	void sendMessage(boost::shared_ptr<IRCThreadMessage> message);

	///This returns whether the thread has exited
	bool hasThreadExited();
private:
	///Sends this IRC message back to the main thread
	void sendToMainThread(boost::shared_ptr<IRCThreadMessage> message);

	IRC irc;
	std::queue<boost::shared_ptr<IRCThreadMessage> > incoming;
	std::queue<boost::shared_ptr<IRCThreadMessage> >& outgoing;
	boost::recursive_mutex incomingMutex;
	boost::recursive_mutex& outgoingMutex;
	bool hasExited;
	std::string channel;
};

#endif
