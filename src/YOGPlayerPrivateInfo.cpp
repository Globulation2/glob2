// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#include <assert.h>

#include "YOGPlayerPrivateInfo.h"
#include "SDL_net.h"
#include "Stream.h"

YOGPlayerPrivateInfo::YOGPlayerPrivateInfo()
{

}


void YOGPlayerPrivateInfo::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("YOGPlayerPrivateInfo");
	stream->writeLeaveSection();
}



void YOGPlayerPrivateInfo::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("YOGPlayerPrivateInfo");
	stream->readLeaveSection();
}



bool YOGPlayerPrivateInfo::operator==(const YOGPlayerPrivateInfo& rhs) const
{
	//TODO: what's the point of this? -Wall found it
	assert(false);
}



bool YOGPlayerPrivateInfo::operator!=(const YOGPlayerPrivateInfo& rhs) const
{
	assert(false);
}

