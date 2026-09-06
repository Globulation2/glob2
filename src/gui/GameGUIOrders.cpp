// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <optional>

#include <StringTable.h>
#include <Toolkit.h>
#include <FormatableString.h>

#include "Game.h"
#include "GameGUI.h"
#include "GameGUIDialog.h"
#include "GameUtilities.h"
#include "GlobalContainer.h"
#include "Unit.h"
#include "Utilities.h"
#include "SoundMixer.h"
#include "VoiceRecorder.h"
#include "Player.h"
#include "ReplayReader.h"
#include "ReplayWriter.h"
#include "config.h"
#include "Order.h"
#include "net/message/MessageRecipients.h"


using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::reconcileBuildingGuiState(const std::shared_ptr<Order>& order)
{
	// When an order executes that updates the authoritative Building state,
	// drop the corresponding pending shadow so the display falls back to
	// authoritative. For the LOCAL player's own orders during live play we
	// leave pending alone — the user may have already queued a newer change
	// past the one that just landed, and we want the display to track the
	// latest user intent, not flicker back to the now-stale authoritative
	// value. Replays clear pending unconditionally because every order
	// represents the authoritative timeline.
	const bool replaying = globalContainer->replaying;
	switch (order->getOrderType())
	{
		case ORDER_MOVE_FLAG:
		{
			auto omf = std::static_pointer_cast<OrderMoveFlag>(order);
			if (omf->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omf->gid);
				if (it != buildingGuiState.end())
				{
					it->second.pendingPosX.reset();
					it->second.pendingPosY.reset();
				}
			}
			break;
		}
		case ORDER_MODIFY_BUILDING:
		{
			auto omb = std::static_pointer_cast<OrderModifyBuilding>(order);
			if (omb->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omb->gid);
				if (it != buildingGuiState.end())
					it->second.pendingMaxUnitWorking.reset();
			}
			break;
		}
		case ORDER_MODIFY_FLAG:
		{
			auto omf = std::static_pointer_cast<OrderModifyFlag>(order);
			if (omf->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omf->gid);
				if (it != buildingGuiState.end())
					it->second.pendingUnitStayRange.reset();
			}
			break;
		}
		case ORDER_CHANGE_PRIORITY:
		{
			auto ocp = std::static_pointer_cast<OrderChangePriority>(order);
			if (ocp->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(ocp->gid);
				if (it != buildingGuiState.end())
					it->second.pendingPriority.reset();
			}
			break;
		}
		case ORDER_MODIFY_CLEARING_FLAG:
		{
			auto omcf = std::static_pointer_cast<OrderModifyClearingFlag>(order);
			if (omcf->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omcf->gid);
				if (it != buildingGuiState.end())
					it->second.pendingClearingRessources.reset();
			}
			break;
		}
		case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
		{
			auto omw = std::static_pointer_cast<OrderModifyMinLevelToFlag>(order);
			if (omw->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(omw->gid);
				if (it != buildingGuiState.end())
					it->second.pendingMinLevelToFlag.reset();
			}
			break;
		}
		case ORDER_MODIFY_SWARM:
		{
			auto oms = std::static_pointer_cast<OrderModifySwarm>(order);
			if (oms->sender != localPlayer || replaying)
			{
				auto it = buildingGuiState.find(oms->gid);
				if (it != buildingGuiState.end())
					it->second.pendingRatio.reset();
			}
			break;
		}
		default:
			break;
	}
}

void GameGUI::executeOrder(std::shared_ptr<Order> order)
{
	switch (order->getOrderType())
	{
		case ORDER_TEXT_MESSAGE :
		{
			std::shared_ptr<MessageOrder> mo=static_pointer_cast<MessageOrder>(order);
			int sp=mo->sender;
			Uint32 messageOrderType=mo->messageOrderType;

			if (messageOrderType==MessageOrder::NORMAL_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(Color(230, 230, 230), FormatableString("%0 : %1").arg(game.players[sp]->name).arg(mo->getText()), true);
			}
			else if (messageOrderType==MessageOrder::PRIVATE_MESSAGE_TYPE)
			{
				if (mo->recepientsMask &(1<<localPlayer))
					addMessage(Color(99, 255, 242), FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[from:]")).arg(game.players[sp]->name).arg(mo->getText()), true);
				else if (sp==localPlayer)
				{
					// Echo the outgoing private message once per recipient. The
					// mask can carry several recipients, so iterate every set
					// bit; messageRecipientPlayers drops any bit outside the
					// live player range instead of indexing an empty slot.
					for (int k : messageRecipientPlayers(mo->recepientsMask, game.gameHeader.getNumberOfPlayers()))
						addMessage(Color(99, 255, 242), FormatableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[to:]")).arg(game.players[k]->name).arg(mo->getText()), true);
				}
			}
			else
				assert(false);

			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_VOICE_DATA:
		{
			std::shared_ptr<OrderVoiceData> ov = static_pointer_cast<OrderVoiceData>(order);
			if (ov->recepientsMask & (1<<localPlayer))
				globalContainer->mix->addVoiceData(ov);
			game.executeOrder(order, localPlayer);
		}
		break;
		case ORDER_PLAYER_QUIT_GAME :
		{
			int qp=order->sender;
			if (qp==localPlayer)
				isRunning=false;
			addMessage(Color(200, 200, 200), FormatableString(Toolkit::getStringTable()->getString("[%0 has left the game]")).arg(game.players[qp]->name), true);
			game.executeOrder(order, localPlayer);
		}
		break;

		case ORDER_MAP_MARK:
		{
			std::shared_ptr<MapMarkOrder> mmo=static_pointer_cast<MapMarkOrder>(order);

			assert(game.teams[mmo->teamNumber]->teamNumber<game.mapHeader.getNumberOfTeams());
			if (game.teams[mmo->teamNumber]->allies & (game.teams[localTeamNo]->me))
				addMark(mmo);
		}
		break;
		case ORDER_PAUSE_GAME:
		{
			std::shared_ptr<PauseGameOrder> pgo=static_pointer_cast<PauseGameOrder>(order);
			gamePaused=pgo->pause;
		}
		break;
		case ORDER_CREATE:
		{
			std::shared_ptr<OrderCreate> pgo=static_pointer_cast<OrderCreate>(order);
			if(pgo->teamNumber == localTeamNo)
				ghostManager.removeBuilding(pgo->posX, pgo->posY);
			game.executeOrder(order, localPlayer);
		}
		break;
		default:
		{
			game.executeOrder(order, localPlayer);
		}
	}
	reconcileBuildingGuiState(order);
}
