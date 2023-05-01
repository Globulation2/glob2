/*
  Copyright 2008 Bradley Arsenault

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

#include "YOGServerFileDistributationManager.h"


YOGServerFileDistributionManager::YOGServerFileDistributionManager()
{
	currentID=1;
}



int YOGServerFileDistributionManager::allocateFileDistributor()
{
	int id = chooseTransferID();
	files[id] = boost::shared_ptr<YOGServerFileDistributor>(new YOGServerFileDistributor(id));
	return id;
}



void YOGServerFileDistributionManager::update()
{
	for(std::map<Uint16, boost::shared_ptr<YOGServerFileDistributor> >::iterator i = files.begin(); i!=files.end(); ++i)
	{
		if(i->second)
			i->second->update();
	}
}



boost::shared_ptr<YOGServerFileDistributor> YOGServerFileDistributionManager::getDistributor(Uint16 transferID)
{
	return files[transferID];
}



void YOGServerFileDistributionManager::removeDistributor(Uint16 transferID)
{
	std::map<Uint16, boost::shared_ptr<YOGServerFileDistributor> >::iterator i = files.find(transferID);
	if(i != files.end())
	{
		files.erase(i);
	}
}



Uint16 YOGServerFileDistributionManager::chooseTransferID()
{
	while(files.find(currentID) != files.end())
	{
		currentID+=1;
	}
	return currentID;
}

