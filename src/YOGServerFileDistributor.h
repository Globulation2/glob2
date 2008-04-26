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

#ifndef __YOGServerFileDistributor_h
#define __YOGServerFileDistributor_h

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/shared_ptr.hpp"
#include "boost/tuple/tuple.hpp"
#include "SDL_net.h"
#include <vector>

class NetSendFileInformation;
class NetSendFileChunk;
class YOGServerGame;
class YOGServerPlayer;
class NetMessage;

///This class assumes the responsibility of sending, transfering, and recieving files to and from clients
class YOGServerFileDistributor
{
public:
	///Constructs a YOGServerFileDistributor
	YOGServerFileDistributor(Uint16 fileID);

	///Sets this file distributor to load the given file locally
	void loadFromLocally(const std::string& file);
	
	///Tells this file distributor to load from the given player
	void loadFromPlayer(boost::shared_ptr<YOGServerPlayer> player);

	///Updates the YOGServerFileDistributor
	void update();

	///Add the given player as one requesting the file
	void addMapRequestee(boost::shared_ptr<YOGServerPlayer> player);
	
	///Removes the given player from requesting the map
	void removeMapRequestee(boost::shared_ptr<YOGServerPlayer> player);

	///Handles the provided message
	void handleMessage(boost::shared_ptr<NetMessage> message, boost::shared_ptr<YOGServerPlayer> player);
private:
	///Loads from the file
	void loadDataFromFile();
	///Requests the file from the player
	void requestDataFromPlayer();
	///Makes sure that the map has been requested, either from file or player
	void garunteeDataRequested();

	Uint16 fileID;
	bool startedLoading;
	std::string fileName;
	boost::shared_ptr<YOGServerPlayer> player;
	boost::shared_ptr<NetSendFileInformation> fileInfo;
	std::vector<boost::shared_ptr<NetSendFileChunk> > chunks;
	std::vector<boost::tuple<boost::shared_ptr<YOGServerPlayer>, boost::posix_time::ptime, int> > players;

};




#endif
