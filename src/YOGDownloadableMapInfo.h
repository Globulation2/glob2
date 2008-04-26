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

#ifndef YOGDownloadableMapInfo_h
#define YOGDownloadableMapInfo_h

#include "MapHeader.h"
#include <string>
#include "SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This is all of the information for a map that can be downloaded
class YOGDownloadableMapInfo
{
public:
	///Constructs a blank info
	YOGDownloadableMapInfo();
	
	///Constructs the map info
	YOGDownloadableMapInfo(MapHeader& header);
	
	///Sets the map header
	void setMapHeader(const MapHeader& header);
	
	///Returns the map header
	const MapHeader& getMapHeader() const;

	///Encodes this YOGGameInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///Test for equality between two YOGGameInfo
	bool operator==(const YOGDownloadableMapInfo& rhs) const;
	bool operator!=(const YOGDownloadableMapInfo& rhs) const;
private:
	mutable MapHeader mapHeader;
};

#endif
