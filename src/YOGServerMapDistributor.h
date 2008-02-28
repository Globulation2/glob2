/*
  Copyright 2007 Bradley Arsenault

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

#ifndef __YOGServerMapDistributor_h
#define __YOGServerMapDistributor_h

#include "boost/tuple/tuple.hpp"
#include "boost/shared_ptr.hpp"
#include <vector>

class NetSendFileInformation;
class NetSendFileChunk;
class YOGServerGame;
class YOGServerPlayer;
class NetMessage;

///This class has the responsibility of sharing a map between players in a YOG game.
class YOGServerMapDistributor
{
public:
	///Constructs a YOGServerMapDistributor with the given game and host
	YOGServerMapDistributor(boost::shared_ptr<YOGServerGame> game, boost::shared_ptr<YOGServerPlayer> host);

	///Updates the YOGServerMapDistributor
	void update();

	///Add the given player as one requesting the map
	void addMapRequestee(boost::shared_ptr<YOGServerPlayer> player);
	
	///Removes the given player from requesting the map
	void removeMapRequestee(boost::shared_ptr<YOGServerPlayer> player);

	///Handles the provided message
	void handleMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player);
private:
	bool sentRequest;
	boost::shared_ptr<YOGServerGame> game;
	boost::shared_ptr<YOGServerPlayer> host;
	boost::shared_ptr<NetSendFileInformation> fileInfo;
	std::vector<boost::shared_ptr<NetSendFileChunk> > chunks;
	std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, int, int> > players;

};




#endif
