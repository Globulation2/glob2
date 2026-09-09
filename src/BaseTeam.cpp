// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include "BaseTeam.h"
#include "Marshaling.h"
#include "Race.h"
#include "Stream.h"
#include "Utilities.h"

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
	// loading base team
	stream->readEnterSection("BaseTeam");
	type = (TeamType)stream->readUint32("type");
	teamNumber = stream->readSint32("teamNumber");
	numberOfPlayer = stream->readSint32("numberOfPlayer");
	stream->read(&color.r, 1, "colorR");
	stream->read(&color.g, 1, "colorG");
	stream->read(&color.b, 1, "colorB");
	// "colorPAD" is a wire padding byte, not persisted state: team alpha is
	// always Color::ALPHA_OPAQUE in memory, so the byte is discarded on load
	// and written as ALPHA_OPAQUE on save. Keep both sides symmetric so
	// save->load->save round-trips byte-identically.
	Uint8 colorPad;
	stream->read(&colorPad, 1, "colorPAD");
	color.a = Color::ALPHA_OPAQUE;
	playersMask = stream->readUint32("playersMask");
	if(versionMinor < 73)
	{
		Race race;
		race.load(stream, versionMinor);
	}
	stream->readLeaveSection();
	return true;
}




void BaseTeam::save(GAGCore::OutputStream *stream) const
{
	// saving base team
	stream->writeEnterSection("BaseTeam");
	stream->writeUint32((Uint32)type, "type");
	stream->writeSint32(teamNumber, "teamNumber");
	stream->writeSint32(numberOfPlayer, "numberOfPlayer");
	stream->write(&color.r, 1, "colorR");
	stream->write(&color.g, 1, "colorG");
	stream->write(&color.b, 1, "colorB");
	// See load(): "colorPAD" is a padding byte pinned to ALPHA_OPAQUE, not
	// the live alpha value, so the round-trip cannot drift.
	const Uint8 colorPad = Color::ALPHA_OPAQUE;
	stream->write(&colorPad, 1, "colorPAD");
	stream->writeUint32(playersMask, "playersMask");
	stream->writeLeaveSection();
}




Uint32 BaseTeam::checkSum()
{
	Uint32 cs=0;

	cs^=teamNumber;
	cs=rotr1(cs);
	cs^=numberOfPlayer;
	cs=rotr1(cs);
	cs^=playersMask;
	cs=rotr1(cs);

	return cs;
}

