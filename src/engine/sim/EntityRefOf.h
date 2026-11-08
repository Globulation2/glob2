// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Building.h"
#include "Unit.h"
#include "sim/EntityRef.h"

/// Client handle of a live entity (null gives an empty ref). Inline, because
/// snapshot capture and the hiring and turret pipelines call these per entity
/// and release builds do not use link-time optimization.
inline BuildingRef refOf(const Building *b)
{
	BuildingRef ref;
	if (b)
	{
		ref.gid = b->gid;
		ref.generation = b->scriptIdentity;
	}
	return ref;
}

inline UnitRef refOf(const Unit *u)
{
	UnitRef ref;
	if (u)
	{
		ref.gid = u->gid;
		ref.generation = u->scriptIdentity;
	}
	return ref;
}
