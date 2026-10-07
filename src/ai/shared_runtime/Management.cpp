// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include <memory>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Management;


ManagementOrder* ManagementOrder::load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	GAGCore::InputStream::NestedRead nesting(*stream);
	stream->readEnterSection("ManagementOrder");
	const Uint32 mot=stream->readUint32("type");
	std::unique_ptr<ManagementOrder> mo;
	switch(mot)
	{
		case MAssignWorkers:
			mo.reset(new AssignWorkers);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MChangeSwarm:
			mo.reset(new ChangeSwarm);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MDestroyBuilding:
			mo.reset(new DestroyBuilding);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
        case MRetireAttraction:
            mo.reset(new RetireAttraction);
            if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
            break;
        case MRetireFeeding:
            mo.reset(new RetireFeeding);
            if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
            break;
		case MAddMaterialTracker:
			mo.reset(new AddMaterialTracker);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MPauseMaterialTracker:
			mo.reset(new PauseMaterialTracker);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MUnPauseMaterialTracker:
			mo.reset(new UnPauseMaterialTracker);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MChangeFlagSize:
			mo.reset(new ChangeFlagSize);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MChangeFlagMinimumLevel:
			mo.reset(new ChangeFlagMinimumLevel);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MAddArea:
			mo.reset(new AddArea);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MRemoveArea:
			mo.reset(new RemoveArea);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MChangeAlliances:
			mo.reset(new ChangeAlliances);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MUpgradeRepair:
			mo.reset(new UpgradeRepair);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MSendMessage:
			mo.reset(new SendMessage);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MChangeFlagPosition:
			mo.reset(new ChangeFlagPosition);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
		case MAdjustPriority:
			mo.reset(new AdjustPriority);
			if (!mo->load(stream, player, versionMinor)) throw std::runtime_error("Invalid saved AI object");
			break;
	}
	stream->readLeaveSection();
	if (!mo) throw std::runtime_error("Unknown saved AI object type");
	return mo.release();
}



void ManagementOrder::save_order(ManagementOrder* mo, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ManagementOrder");
	stream->writeUint32(mo->get_type(), "type");
	mo->save(stream);
	stream->writeLeaveSection();
}



tribool ManagementOrder::wait_for_building(Runtime& runtime, int building_id)
{
	if(runtime.get_building_register().is_building_found(building_id))
		return true;
	if(runtime.get_building_register().is_building_pending(building_id))
		return false;
	return indeterminate;
}
