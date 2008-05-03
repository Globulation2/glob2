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
	
	///This sets the rating total.
	void setRatingTotal(Uint32 total);
	
	///This returns the rating total.
	Uint32 getRatingTotal() const;
	
	///This sets the number of ratings
	void setNumberOfRatings(Uint32 numberOfRatings);
	
	///This returns the number of ratings
	Uint32 getNumberOfRatings() const;
	
	///This sets the author name
	void setAuthorName(const std::string& authorname);
	
	///This returns the author name
	std::string getAuthorName() const;
	
	///This is the fileID this downloadable map can be obtained from
	Uint16 getFileID() const;
	
	///This sets the fileID that this downloadable map is obtained from
	void setFileID(Uint16 fileID);

	///Encodes this YOGGameInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGGameInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream, Uint32 versionMinor);
	
	///Test for equality between two YOGDownloadableMapInfo
	bool operator==(const YOGDownloadableMapInfo& rhs) const;
	bool operator!=(const YOGDownloadableMapInfo& rhs) const;
private:
	mutable MapHeader mapHeader;
	Uint32 total;
	Uint32 numberOfRatings;
	std::string author;
	Uint16 fileID;
};

#endif
