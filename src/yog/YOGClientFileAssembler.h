// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#pragma once

#include "boost/date_time/posix_time/posix_time.hpp"
#include <memory>
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
	YOGClientFileAssembler(std::weak_ptr<YOGClient> client, Uint16 fileID);
	
	///Updates the map assembler
	void update();
	
	///This starts sending the map file with the given map name
	void startSendingFile(std::string mapname);
	
	///This starts recieving a map with the given map name
	void startRecievingFile(std::string mapname);
	
	///This recieves a message from YOG
	void handleMessage(std::shared_ptr<NetMessage> message);

	///This cancels the sending of a file
	void cancelSendingFile();
	
	///This cancels the recieving of a file
	void cancelRecievingFile();

	///This tells the percentage the transfer has from completing, 100% is there was no transfer and/or its complete
	Uint8 getPercentage();
	
	///Tells true if the file information has been recieved. If it hasn't, percent completed is still 100%
	bool fileInformationRecieved();
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
	std::weak_ptr<YOGClient> client;
	GAGCore::MemoryStreamBackend* obackend;
	std::shared_ptr<GAGCore::BinaryOutputStream> ostream;
	std::shared_ptr<GAGCore::BinaryInputStream> istream;
	std::string filename;
	Uint16 fileID;
	boost::posix_time::ptime sendTime;
};





