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

#ifndef YOGPlayerPrivateInfo_h
#define YOGPlayerPrivateInfo_h

#include <string>
#include "SDL2/SDL_net.h"

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class stores information about players that isn't not sent to the client
class YOGPlayerPrivateInfo
{
public:
	///Constructs a default YOGPlayerPrivateInfo
	YOGPlayerPrivateInfo();
	
	///Encodes this YOGPlayerPrivateInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGPlayerPrivateInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGPlayerPrivateInfo
	bool operator==(const YOGPlayerPrivateInfo& rhs) const;
	bool operator!=(const YOGPlayerPrivateInfo& rhs) const;
private:
};

#endif
