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

#include "BaseTeam.h"
#include "Marshaling.h"

using namespace GAGCore;

BaseTeam::BaseTeam()
{
	teamNumber=0;
	numberOfPlayer=0;
	playersMask=0;
	type=T_AI;

	disableRecursiveDestruction=false;
}




bool BaseTeam::load(GAGCore::InputStream *stream, Sint32 versionMinor)
{
	// loading baseteam
	stream->readEnterSection("BaseTeam");
	type = (TeamType)stream->readUint32("type");
	teamNumber = stream->readSint32("teamNumber");
	numberOfPlayer = stream->readSint32("numberOfPlayer");
	stream->read(&color.r, 1, "colorR");
	stream->read(&color.g, 1, "colorG");
	stream->read(&color.b, 1, "colorB");
	stream->read(&color.a, 1, "colorPAD");
	color.a = Color::ALPHA_OPAQUE;
	playersMask = stream->readUint32("playersMask");
	stream->readLeaveSection();
	if (!race.load(stream, versionMinor))
		return false;
	// TODO : overwrite only when it is a certain type, but require carefull thinking. For now, overwrite
	// if (type == T_HUMAN)
	race.load();
	return true;
}




void BaseTeam::save(GAGCore::OutputStream *stream)
{
	// saving baseteam
	stream->writeEnterSection("BaseTeam");
	stream->writeUint32((Uint32)type, "type");
	stream->writeSint32(teamNumber, "teamNumber");
	stream->writeSint32(numberOfPlayer, "numberOfPlayer");
	stream->write(&color.r, 1, "colorR");
	stream->write(&color.g, 1, "colorG");
	stream->write(&color.b, 1, "colorB");
	stream->write(&color.a, 1, "colorPAD");
	stream->writeUint32(playersMask, "playersMask");
	stream->writeLeaveSection();
	race.save(stream);
}




Uint8 *BaseTeam::getData()
{
	addSint32(data, teamNumber, 0);
	addSint32(data, numberOfPlayer, 4);
	addUint8(data, color.r, 8);
	addUint8(data, color.g, 9);
	addUint8(data, color.b, 10);
	addUint8(data, color.a, 11);
	addSint32(data, playersMask, 12);
	// TODO : give race to the network here.

	return data;
}




bool BaseTeam::setData(const Uint8 *data, int dataLength)
{
	if (dataLength!=getDataLength())
		return false;

	teamNumber=getSint32(data, 0);
	numberOfPlayer=getSint32(data, 4);
	color.r=getUint8(data, 8);
	color.g=getUint8(data, 9);
	color.b=getUint8(data, 10);
	color.a=getUint8(data, 11);
	playersMask=getSint32(data, 12);
	// TODO : create the race from the network here.

	return true;
}




int BaseTeam::getDataLength()
{
	return 16;
}




Uint32 BaseTeam::checkSum()
{
	Uint32 cs=0;

	cs^=teamNumber;
	cs=(cs<<31)|(cs>>1);
	cs^=numberOfPlayer;
	cs=(cs<<31)|(cs>>1);
	cs^=playersMask;
	cs=(cs<<31)|(cs>>1);

	return cs;
}

