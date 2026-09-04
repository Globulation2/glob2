// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef YOGPlayerPrivateInfo_h
#define YOGPlayerPrivateInfo_h

#include <string>
#include "SDL_net.h"

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
