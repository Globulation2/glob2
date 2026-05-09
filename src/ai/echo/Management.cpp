// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Building.h"
#include <stack>
#include <queue>
#include <map>
#include <limits>
#include <algorithm>
#include "BuildingType.h"
#include "IntBuildingType.h"
#include "Game.h"
#include "GlobalContainer.h"
#include "Order.h"
#include <iterator>
#include "Utilities.h"
#include <tuple>
#include "Brush.h"

using namespace AIEcho;
using namespace AIEcho::Gradients;
using namespace AIEcho::Construction;
using namespace AIEcho::Management;
using namespace AIEcho::Conditions;
using namespace AIEcho::SearchTools;
using namespace boost::logic;
using std::shared_ptr;


ManagementOrder* ManagementOrder::load_order(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ManagementOrder");
	ManagementOrderType mot=static_cast<ManagementOrderType>(stream->readUint32("type"));
	ManagementOrder* mo=NULL;
	switch(mot)
	{
		case MAssignWorkers:
			mo=new AssignWorkers;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeSwarm:
			mo=new ChangeSwarm;
			mo->load(stream, player, versionMinor);
			break;
		case MDestroyBuilding:
			mo=new DestroyBuilding;
			mo->load(stream, player, versionMinor);
			break;
		case MAddRessourceTracker:
			mo=new AddRessourceTracker;
			mo->load(stream, player, versionMinor);
			break;
		case MPauseRessourceTracker:
			mo=new PauseRessourceTracker;
			mo->load(stream, player, versionMinor);
			break;
		case MUnPauseRessourceTracker:
			mo=new UnPauseRessourceTracker;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeFlagSize:
			mo=new ChangeFlagSize;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeFlagMinimumLevel:
			mo=new ChangeFlagMinimumLevel;
			mo->load(stream, player, versionMinor);
			break;
		case MAddArea:
			mo=new AddArea;
			mo->load(stream, player, versionMinor);
			break;
		case MRemoveArea:
			mo=new RemoveArea;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeAlliances:
			mo=new ChangeAlliances;
			mo->load(stream, player, versionMinor);
			break;
		case MUpgradeRepair:
			mo=new UpgradeRepair;
			mo->load(stream, player, versionMinor);
			break;
		case MSendMessage:
			mo=new SendMessage;
			mo->load(stream, player, versionMinor);
			break;
		case MChangeFlagPosition:
			mo=new ChangeFlagPosition;
			mo->load(stream, player, versionMinor);
			break;
		case MAdjustPriority:
			mo=new AdjustPriority;
			mo->load(stream, player, versionMinor);
			break;
	}
	return mo;
}



void ManagementOrder::save_order(ManagementOrder* mo, GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ManagementOrder");
	stream->writeUint32(mo->get_type(), "type");
	mo->save(stream);
	stream->writeLeaveSection();
}

