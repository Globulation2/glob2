// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2007 Bradley Arsenault

#include "LANGameInformation.h"
#include "Stream.h"

LANGameInformation::LANGameInformation(const YOGGameInfo& information)
{

}



LANGameInformation::LANGameInformation()
{

}



void LANGameInformation::encodeData(GAGCore::OutputStream* stream) const
{
	stream->writeEnterSection("LANGameInformation");
	gameInfo.encodeData(stream);
	stream->writeLeaveSection();
}



void LANGameInformation::decodeData(GAGCore::InputStream* stream)
{
	stream->readEnterSection("LANGameInformation");
	gameInfo.decodeData(stream);
	stream->readLeaveSection();
}



const YOGGameInfo& LANGameInformation::getGameInformation() const
{
	return gameInfo;
}



YOGGameInfo& LANGameInformation::getGameInformation()
{
	return gameInfo;
}


