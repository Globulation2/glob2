// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include "YOGServerFileDistributationManager.h"


YOGServerFileDistributationManager::YOGServerFileDistributationManager()
{
	currentID=1;
}



int YOGServerFileDistributationManager::allocateFileDistributor()
{
	int id = chooseTransferID();
	files[id] = std::shared_ptr<YOGServerFileDistributor>(new YOGServerFileDistributor(id));
	return id;
}



void YOGServerFileDistributationManager::update()
{
	for(std::map<Uint16, std::shared_ptr<YOGServerFileDistributor> >::iterator i = files.begin(); i!=files.end(); ++i)
	{
		if(i->second)
			i->second->update();
	}
}



std::shared_ptr<YOGServerFileDistributor> YOGServerFileDistributationManager::getDistributor(Uint16 transferID)
{
	return files[transferID];
}



void YOGServerFileDistributationManager::removeDistributor(Uint16 transferID)
{
	std::map<Uint16, std::shared_ptr<YOGServerFileDistributor> >::iterator i = files.find(transferID);
	if(i != files.end())
	{
		files.erase(i);
	}
}



Uint16 YOGServerFileDistributationManager::chooseTransferID()
{
	while(files.find(currentID) != files.end())
	{
		currentID+=1;
	}
	return currentID;
}

