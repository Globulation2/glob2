// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#ifndef __YOGServerFileDistributor_h
#define __YOGServerFileDistributor_h

#include "boost/date_time/posix_time/posix_time.hpp"
#include <memory>
#include <tuple>
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
	void loadFromPlayer(std::shared_ptr<YOGServerPlayer> player);
	
	///This tells the file distributor to save all data in the file the given filename locally
	void saveToFile(const std::string& file);

	///This returns true if all of the chunks for the map are loaded, including the file
	///information chunk, false otherwise
	bool areAllChunksLoaded();
	
	///This returns true if the uploading from a player was canceled
	bool wasUploadingCanceled();

	///Updates the YOGServerFileDistributor
	void update();

	///Add the given player as one requesting the file
	void addMapRequestee(std::shared_ptr<YOGServerPlayer> player);
	
	///Removes the given player from requesting the map
	void removeMapRequestee(std::shared_ptr<YOGServerPlayer> player);

	///Handles the provided message
	void handleMessage(std::shared_ptr<NetMessage> message, std::shared_ptr<YOGServerPlayer> player);
private:
	///Loads from the file
	void loadDataFromFile();
	///Requests the file from the player
	void requestDataFromPlayer();
	///Makes sure that the map has been requested, either from file or player
	void garunteeDataRequested();

	Uint16 fileID;
	bool startedLoading;
	bool downloadFromPlayerCanceled;
	std::string fileName;
	std::shared_ptr<YOGServerPlayer> player;
	std::shared_ptr<NetSendFileInformation> fileInfo;
	std::vector<std::shared_ptr<NetSendFileChunk> > chunks;
	std::vector<std::tuple<std::shared_ptr<YOGServerPlayer>, boost::posix_time::ptime, int> > players;

};




#endif
