// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGServerFileDistributationManager_h
#define YOGServerFileDistributationManager_h

#include <map>
#include "SDL_net.h"
#include "YOGServerFileDistributor.h"

///This class manages all file transfers on the server
class YOGServerFileDistributationManager
{
public:
	///Constructs a distributor
	YOGServerFileDistributationManager();

	///Allocates a file distributor, returns the transfer ID
	int allocateFileDistributor();
	
	///This updates this distributor
	void update();
	
	///This returns the file distributor for the given id
	std::shared_ptr<YOGServerFileDistributor> getDistributor(Uint16 transferID);
	
	///This removes the file distributor
	void removeDistributor(Uint16 transferID);
private:
	///Finds an available transfer id
	Uint16 chooseTransferID();

	std::map<Uint16, std::shared_ptr<YOGServerFileDistributor> > files;
	Uint16 currentID;
};

#endif
