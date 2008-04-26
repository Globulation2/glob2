/*
  Copyright (C) 2008 Bradley Arsenault

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

#ifndef YOGServerMapDatabank_h
#define YOGServerMapDatabank_h

#include "YOGDownloadableMapInfo.h"
#include "YOGServerPlayer.h"
#include <vector>

///This stores maps that users can download on the server end
class YOGServerMapDatabank
{
public:
	///Constructs the class, loading the databank
	YOGServerMapDatabank();

	///Adds a map to the database
	void addMap(const YOGDownloadableMapInfo& map);
	
	///Sends the list of maps to the given player
	void sendMapListToPlayer(boost::shared_ptr<YOGServerPlayer> player);
private:
	///This does a full load of the map databank
	void load();
	///This does a full save of the map databank
	void save();
	
	std::vector<YOGDownloadableMapInfo> maps;
};

#endif
