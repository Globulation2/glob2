// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"

using namespace AIEcho;
using namespace AIEcho::Construction;


Constraint* Constraint::load_constraint(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("Constraint");
	ConstraintType type=static_cast<ConstraintType>(stream->readUint32("type"));
	Constraint* constraint=NULL;
	switch(type)
	{
		case CTMinimumDistance:
			constraint=new MinimumDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTMaximumDistance:
			constraint=new MaximumDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTMinimizedDistance:
			constraint=new MinimizedDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTMaximizedDistance:
			constraint=new MaximizedDistance;
			constraint->load(stream, player, versionMinor);
		break;
		case CTCenterOfBuilding:
			constraint=new CenterOfBuilding;
			constraint->load(stream, player, versionMinor);
		break;
		case CTSinglePosition:
			constraint=new SinglePosition;
			constraint->load(stream, player, versionMinor);
		break;
	}
	stream->readLeaveSection();
	return constraint;
}



void Constraint::save_constraint(Constraint* constraint, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Constraint");
	stream->writeUint32(constraint->get_type(), "type");
	constraint->save(stream);
	stream->writeLeaveSection();
}
