/*
  Copyright 2007 Bradley Arsenault

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __MapAssembler_h
#define __MapAssembler_h

#include "YOGClient.h"
#include "Stream.h"
#include "BinaryStream.h"

///This class holds the responsibility of sending and recieving maps over the network.
class MapAssembler
{
public:
	///Contructs a MapAssembler connected to the given client
	MapAssembler(boost::shared_ptr<YOGClient> client);
	
	///Updates the map assembler
	void update();
	
	///This starts sending the map file with the given map name
	void startSendingFile(std::string mapname);
	
	///This starts recieving a map with the given map name
	void startRecievingFile(std::string mapname);
	
	///This tells whether the file transfer, going or recieving, is done
	bool isTransferComplete();
	
	///This recieves a message from YOG
	void handleMessage(boost::shared_ptr<NetMessage> message);
private:
	void sendNextChunk();
	void requestNextChunk();

	enum TransferMode
	{
		NoTransfer,
		SendingFile,
		RecivingFile,
	};
	
	TransferMode mode;
	Uint32 size;
	Uint32 finished;
	boost::shared_ptr<YOGClient> client;
	boost::shared_ptr<GAGCore::BinaryOutputStream> ostream;
	boost::shared_ptr<GAGCore::BinaryInputStream> istream;
	std::string filename;
};





#endif
