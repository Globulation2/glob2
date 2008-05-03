/*
  Copyright (C) 2008 Bradley Arsenault

  Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière
  for any question or comment contact us at <stephane at magnenat dot net> or <NuageBleu at gmail dot com>

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

#ifndef MapThumbnail_h
#define MapThumbnail_h

#include <string>

namespace GAGCore
{
	class DrawableSurface;
};

///This class encapsulates everything about map thumbnails, which are shown when choosing a map before you start a game
class MapThumbnail
{
public:
	///Constructs a thumbnail
	MapThumbnail();
	
	///Deletes a thumbnail
	~MapThumbnail();
	
	///Loads the thumbnail from the map with the given map name
	void loadFromMap(const std::string& map);

	///Returns a surface representing the thumbnail. Returns NULL when theres no thumbnail loaded
	GAGCore::DrawableSurface *getThumbnailSurface();
	
	///Returns the map width
	int getMapWidth();
	
	///Returns the map height
	int getMapHeight();

private:
	GAGCore::DrawableSurface *mapThumbnail;
	int lastW;
	int lastH;
};

#endif
