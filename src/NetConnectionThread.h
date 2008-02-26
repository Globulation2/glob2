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

#ifndef NetConnectionThread_h
#define NetConnectionThread_h

#include "NetConnectionThreadMessage.h"
#include <boost/shared_ptr.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <queue>

///IRC thread manages IRC
class NetConnectionThread
{
public:
	NetConnectionThread(std::queue<boost::shared_ptr<NetConnectionThreadMessage> >& outgoing, boost::recursive_mutex& outgoingMutex);
	
	~NetConnectionThread();
	
	///Runs the net thread
	void operator()();

	///Sends this net thread a message
	void sendMessage(boost::shared_ptr<NetConnectionThreadMessage> message);

	///This returns whether the thread has exited
	bool hasThreadExited();

	///Returns true if this object is connected
	bool isConnected();
private:

	///Closes the connection
	void closeConnection();

	///Sends this net message back to the main thread
	void sendToMainThread(boost::shared_ptr<NetConnectionThreadMessage> message);
	IPaddress address;
	TCPsocket socket;
	SDLNet_SocketSet set;
	bool connected;
	
	std::queue<boost::shared_ptr<NetConnectionThreadMessage> > incoming;
	std::queue<boost::shared_ptr<NetConnectionThreadMessage> >& outgoing;
	boost::recursive_mutex incomingMutex;
	boost::recursive_mutex& outgoingMutex;
	bool hasExited;
	//static Uint32 lastTime;
	//static Uint32 amount;
};


#endif
