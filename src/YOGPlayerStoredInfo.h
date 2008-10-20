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

#ifndef YOGPlayerStoredInfo_h
#define YOGPlayerStoredInfo_h

namespace GAGCore
{
	class OutputStream;
	class InputStream;
}

///This class represents information stored on the server about players
class YOGPlayerStoredInfo
{
public:
	///Constructs a default YOGPlayerStoredInfo
	YOGPlayerStoredInfo();
	
	///Sets this player to be muted
	void setMuted();
	
	///Sets this player to be unmuted
	void setUnmuted();
	
	///Returns true if this player is muted, false otherwise
	bool isMuted();

	///Encodes this YOGPlayerStoredInfo into a bit stream
	void encodeData(GAGCore::OutputStream* stream) const;

	///Decodes this YOGPlayerStoredInfo from a bit stream
	void decodeData(GAGCore::InputStream* stream);
	
	///Test for equality between two YOGPlayerStoredInfo
	bool operator==(const YOGPlayerStoredInfo& rhs) const;
	bool operator!=(const YOGPlayerStoredInfo& rhs) const;
private:
	bool muted;
};

#endif
