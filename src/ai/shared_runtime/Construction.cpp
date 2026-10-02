// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include <memory>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Construction;


Constraint* Constraint::load_constraint(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	GAGCore::InputStream::NestedRead nesting(*stream);
	stream->readEnterSection("Constraint");
	const Uint32 type=stream->readUint32("type");
	std::unique_ptr<Constraint> constraint;
	switch(type)
	{
		case CTMinimumDistance:
			constraint.reset(new MinimumDistance);
			if (!constraint->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
		break;
		case CTMaximumDistance:
			constraint.reset(new MaximumDistance);
			if (!constraint->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
		break;
		case CTMinimizedDistance:
			constraint.reset(new MinimizedDistance);
			if (!constraint->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
		break;
		case CTMaximizedDistance:
			constraint.reset(new MaximizedDistance);
			if (!constraint->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
		break;
		case CTCenterOfBuilding:
			constraint.reset(new CenterOfBuilding);
			if (!constraint->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
		break;
		case CTSinglePosition:
			constraint.reset(new SinglePosition);
			if (!constraint->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
		break;
	}
	stream->readLeaveSection();
	if (!constraint) throw std::runtime_error("Unknown saved AI object type");
	return constraint.release();
}



void Constraint::save_constraint(Constraint* constraint, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("Constraint");
	stream->writeUint32(constraint->get_type(), "type");
	constraint->save(stream);
	stream->writeLeaveSection();
}
