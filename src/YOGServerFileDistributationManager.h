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

#ifndef YOGServerFileDistributionManager_h
#define YOGServerFileDistributionManager_h

#include <map>
#include "SDL_net.h"
#include "YOGServerFileDistributor.h"

///This class manages all file transfers on the server
class YOGServerFileDistributionManager
{
public:
	///Constructs a distributor
	YOGServerFileDistributionManager();

	///Allocates a file distributor, returns the transfer ID
	int allocateFileDistributor();
	
	///This updates this distributor
	void update();
	
	///This returns the file distributor for the given id
	boost::shared_ptr<YOGServerFileDistributor> getDistributor(Uint16 transferID);
	
	///This removes the file distributor
	void removeDistributor(Uint16 transferID);
private:
	///Finds an available transfer id
	Uint16 chooseTransferID();

	std::map<Uint16, boost::shared_ptr<YOGServerFileDistributor> > files;
	Uint16 currentID;
};

#endif
