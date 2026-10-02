// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2004 Stephane Magnenat & Luc-Olivier de Charrière

#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#include <optional>
#include <type_traits>
#include <variant>

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
#include <glob2/BuildConfig.h>
#include "Order.h"
#include "net/message/MessageRecipients.h"


using std::shared_ptr;
using std::static_pointer_cast;

void GameGUI::reconcileBuildingGuiState(const std::shared_ptr<Order>& order)
{
	// When an order executes that updates the authoritative Building state,
	// drop the corresponding pending shadow so the display falls back to
	// authoritative. For the LOCAL player's own orders during live play the
	// shadow is only dropped when the order carries the pending value: a
	// newer change the user queued past the one that just landed stays
	// visible, while a landed request no longer masks later changes the
	// simulation makes on its own, such as a finished construction site
	// taking its finished-building count. Replays clear pending
	// unconditionally because every order represents the authoritative
	// timeline.
	const bool replaying = globalContainer->replaying;
	auto landed = [&](const Order& o) { return o.sender != localPlayer || replaying; };
	switch (order->getOrderType())
	{
		case ORDER_MOVE_FLAG:
		{
			auto omf = std::static_pointer_cast<OrderMoveFlag>(order);
			auto it = buildingGuiState.find(omf->gid);
			if (it != buildingGuiState.end()
				&& (landed(*omf) || (it->second.pendingPosX == omf->x && it->second.pendingPosY == omf->y)))
			{
				it->second.pendingPosX.reset();
				it->second.pendingPosY.reset();
			}
			break;
		}
		case ORDER_MODIFY_BUILDING:
		{
			auto omb = std::static_pointer_cast<OrderModifyBuilding>(order);
			auto it = buildingGuiState.find(omb->gid);
			if (it != buildingGuiState.end()
				&& (landed(*omb) || it->second.pendingMaxUnitWorking == omb->numberRequested))
				it->second.pendingMaxUnitWorking.reset();
			break;
		}
		case ORDER_MODIFY_FLAG:
		{
			auto omf = std::static_pointer_cast<OrderModifyFlag>(order);
			auto it = buildingGuiState.find(omf->gid);
			if (it != buildingGuiState.end()
				&& (landed(*omf) || it->second.pendingUnitStayRange == omf->range))
				it->second.pendingUnitStayRange.reset();
			break;
		}
		case ORDER_CHANGE_PRIORITY:
		{
			auto ocp = std::static_pointer_cast<OrderChangePriority>(order);
			auto it = buildingGuiState.find(ocp->gid);
			if (it != buildingGuiState.end()
				&& (landed(*ocp) || it->second.pendingPriority == ocp->priority))
				it->second.pendingPriority.reset();
			break;
		}
		case ORDER_MODIFY_CLEARING_FLAG:
		{
			auto omcf = std::static_pointer_cast<OrderModifyClearingFlag>(order);
			auto it = buildingGuiState.find(omcf->gid);
			if (it != buildingGuiState.end()
				&& (landed(*omcf) || (it->second.pendingClearingResources
					&& std::equal(omcf->clearingResources, omcf->clearingResources + BASIC_COUNT, it->second.pendingClearingResources->begin()))))
				it->second.pendingClearingResources.reset();
			break;
		}
		case ORDER_MODIFY_MIN_LEVEL_TO_FLAG:
		{
			auto omw = std::static_pointer_cast<OrderModifyMinLevelToFlag>(order);
			auto it = buildingGuiState.find(omw->gid);
			if (it != buildingGuiState.end()
				&& (landed(*omw) || it->second.pendingMinLevelToFlag == omw->minLevelToFlag))
				it->second.pendingMinLevelToFlag.reset();
			break;
		}
		case ORDER_MODIFY_SWARM:
		{
			auto oms = std::static_pointer_cast<OrderModifySwarm>(order);
			auto it = buildingGuiState.find(oms->gid);
			if (it != buildingGuiState.end()
				&& (landed(*oms) || (it->second.pendingRatio
					&& std::equal(oms->ratio, oms->ratio + NB_UNIT_TYPE, it->second.pendingRatio->begin()))))
				it->second.pendingRatio.reset();
			break;
		}
		default:
			break;
	}
}

void GameGUI::executeOrder(std::shared_ptr<Order> order)
{
	// The simulation executes the order and publishes what the client should
	// show; react to it right away so pause and quit take effect before the
	// engine decides whether to run the next tick, exactly as before.
	game.executeOrderAndNotify(order, localPlayer);
	if (!simulationThreaded)
	{
		consumeClientEvents();
		return;
	}
	// On the simulation thread, apply now only what decides the next tick (pause,
	// the local player leaving); the GUI consumes the notices, including these
	// again, in threadedClientStep.
	if (order->getOrderType() == ORDER_PAUSE_GAME)
		gamePaused = std::static_pointer_cast<PauseGameOrder>(order)->pause;
	else if (order->getOrderType() == ORDER_PLAYER_QUIT_GAME && order->sender == localPlayer)
		isRunning = false;
}

void GameGUI::handleClientEvent(ClientEventVariant&& event)
{
	std::visit([this](auto&& e)
	{
		using T = std::decay_t<decltype(e)>;
		if constexpr (std::is_same_v<T, ClientEvent::TeamEvent>)
		{
			if (e.team >= 0 && e.team < Team::MAX_COUNT)
				pendingTeamEvents[e.team].push_back(std::move(e.event));
		}
		else if constexpr (std::is_same_v<T, ClientEvent::ChatMessage>)
		{
			if (e.messageOrderType==MessageOrder::NORMAL_MESSAGE_TYPE)
			{
				if (e.recipientsMask &(1<<localPlayer))
					addMessage(Color(230, 230, 230), FormattableString("%0 : %1").arg(e.senderName).arg(e.text), true);
			}
			else if (e.messageOrderType==MessageOrder::PRIVATE_MESSAGE_TYPE)
			{
				if (e.recipientsMask &(1<<localPlayer))
					addMessage(Color(99, 255, 242), FormattableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[from:]")).arg(e.senderName).arg(e.text), true);
				else if (e.sender==localPlayer)
				{
					// Echo the outgoing private message once per recipient. The
					// mask can carry several recipients; the simulation resolved
					// each live recipient's name (messageRecipientPlayers).
					for (const std::string& recipient : e.recipientNames)
						addMessage(Color(99, 255, 242), FormattableString("<%0%1> %2").arg(Toolkit::getStringTable()->getString("[to:]")).arg(recipient).arg(e.text), true);
				}
			}
			else
				assert(false);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::VoiceData>)
		{
			if (e.order->recipientsMask & (1<<localPlayer))
				globalContainer->mix->addVoiceData(e.order);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::PlayerQuit>)
		{
			if (e.player==localPlayer)
				isRunning=false;
			addMessage(Color(200, 200, 200), FormattableString(Toolkit::getStringTable()->getString("[%0 has left the game]")).arg(e.name), true);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::MapMark>)
		{
			if (e.markingTeamAllies & (game.teams[localTeamNo]->me))
				addMark(e.order);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::PauseChanged>)
		{
			gamePaused=e.paused;
		}
		else if constexpr (std::is_same_v<T, ClientEvent::BuildingRequested>)
		{
			if (e.team == localTeamNo)
				ghostManager.removeBuilding(e.posX, e.posY);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::OrderExecuted>)
		{
			reconcileBuildingGuiState(e.order);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::BuildingRemoved>)
		{
			// Drop this building's pending GUI shadow. buildingGuiState is keyed by
			// gid, and gids are recycled by Game::addBuilding (lowest free slot), so a
			// leftover entry would be inherited by the next building created on the
			// same slot. For a dragged-then-destroyed flag that left pendingPosX/Y set,
			// a freshly placed flag reusing the gid would render at the dead flag's
			// position while the simulation used the real posX/posY.
			buildingGuiState.erase(e.gid);
		}
		else if constexpr (std::is_same_v<T, ClientEvent::UnitConverted>)
		{
			// The unit object survives a conversion with a new gid and generation;
			// keep it selected, as the pointer-based selection used to.
			if (const UnitRef *selected = std::get_if<UnitRef>(&selection); selected && *selected == e.from)
				selection = e.to;
		}
	}, std::move(event));
}
