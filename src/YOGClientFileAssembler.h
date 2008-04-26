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

#ifndef __YOGClientFileAssembler_h
#define __YOGClientFileAssembler_h

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/weak_ptr.hpp"
#include "SDL_net.h"
#include <string>

class YOGClient;
class NetMessage;

namespace GAGCore
{
	class MemoryStreamBackend;
	class BinaryOutputStream;
	class BinaryInputStream;
}

///This class holds the responsibility of sending and recieving maps over the network.
class YOGClientFileAssembler
{
public:
	///Contructs a YOGClientFileAssembler connected to the given client, and the given fileID
	YOGClientFileAssembler(boost::weak_ptr<YOGClient> client, Uint16 fileID);
	
	///Updates the map assembler
	void update();
	
	///This starts sending the map file with the given map name
	void startSendingFile(std::string mapname);
	
	///This starts recieving a map with the given map name
	void startRecievingFile(std::string mapname);
	
	///This recieves a message from YOG
	void handleMessage(boost::shared_ptr<NetMessage> message);

	///This tells the percentage the transfer has from completing, 100% is there was no transfer and/or its complete
	Uint8 getPercentage();
private:
	void sendNextChunk();

	enum TransferMode
	{
		NoTransfer,
		SendingFile,
		RecivingFile,
	};
	
	TransferMode mode;
	Uint32 size;
	Uint32 finished;
	boost::weak_ptr<YOGClient> client;
	GAGCore::MemoryStreamBackend* obackend;
	boost::shared_ptr<GAGCore::BinaryOutputStream> ostream;
	boost::shared_ptr<GAGCore::BinaryInputStream> istream;
	std::string filename;
	Uint16 fileID;
	boost::posix_time::ptime sendTime;
};





#endif
