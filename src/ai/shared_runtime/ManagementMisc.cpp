#include "AIRuleOrders.h"
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "shared_runtime/Runtime.h"
#include "Game.h"
#include "Order.h"

#include <stdexcept>

using namespace AISharedRuntime;
using namespace AISharedRuntime::Management;
using std::shared_ptr;

namespace
{
	tribool readTribool(GAGCore::InputStream *stream, const char *field)
	{
		const Uint8 value=stream->readUint8(field);
		if(value==AI_SHARED_RUNTIME_TRIBOOL_TRUE) return true;
		if(value==AI_SHARED_RUNTIME_TRIBOOL_FALSE) return false;
		if(value==AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE) return indeterminate;
		throw std::runtime_error("Invalid AI alliance tri-state");
	}
}


ChangeAlliances::ChangeAlliances(int team, tribool is_allied, tribool is_enemy, tribool view_market, tribool view_inn, tribool view_other) : team(team), is_allied(is_allied), is_enemy(is_enemy), view_market(view_market), view_inn(view_inn), view_other(view_other)
{

}



void ChangeAlliances::modify(Runtime& runtime)
{
	if (team < 0 || team >= runtime.player->game->mapHeader.getNumberOfTeams() ||
		team >= Team::MAX_COUNT || !runtime.player->game->teams[team])
		return;
	Uint32 alliedmask=runtime.allies;
	Uint32 enemymask=runtime.enemies;
	Uint32 market_mask=runtime.market_view;
	Uint32 inn_mask=runtime.inn_view;
	Uint32 other_mask=runtime.other_view;
	Team* t=runtime.player->game->teams[team];
	// t->me is always a single bit (Team::teamNumberToMask = 1 << teamNumber),
	// so &= ~t->me clears it cleanly; the legacy `if(mask&t->me) mask^=t->me;`
	// pattern was equivalent but obscured the intent.
	if(is_allied)
		alliedmask|=t->me;
	else if(!is_allied)
		alliedmask&=~t->me;

	if(is_enemy)
		enemymask|=t->me;
	else if(!is_enemy)
		enemymask&=~t->me;

	if(view_market)
		market_mask|=t->me;
	else if(!view_market)
		market_mask&=~t->me;

	if(view_inn)
		inn_mask|=t->me;
	else if(!view_inn)
		inn_mask&=~t->me;

	if(view_other)
		other_mask|=t->me;
	else if(!view_other)
		other_mask&=~t->me;

	runtime.allies=alliedmask;
	runtime.enemies=enemymask;
	runtime.market_view=market_mask;
	runtime.inn_view=inn_mask;
	runtime.other_view=other_mask;

	runtime.push_order(shared_ptr<Order>(new SetAllianceOrder(runtime.player->team->teamNumber, alliedmask, enemymask, market_mask, inn_mask, other_mask)));
}



tribool ChangeAlliances::wait(Runtime& runtime)
{
	return team >= 0 && team < runtime.player->game->mapHeader.getNumberOfTeams() &&
		team < Team::MAX_COUNT && runtime.player->game->teams[team];
}



bool ChangeAlliances::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeAlliances");
	ManagementOrder::load(stream, player, versionMinor);
	const Uint32 rawTeam=stream->readUint32("team");
	if (rawTeam >= static_cast<Uint32>(player->game->mapHeader.getNumberOfTeams()) ||
		rawTeam >= Team::MAX_COUNT || !player->game->teams[rawTeam])
		throw std::runtime_error("Invalid AI alliance team");
	team=static_cast<int>(rawTeam);
	is_allied=readTribool(stream, "is_allied");
	is_enemy=readTribool(stream, "is_enemy");
	view_market=readTribool(stream, "view_market");
	view_inn=readTribool(stream, "view_inn");
	view_other=readTribool(stream, "view_other");

	stream->readLeaveSection();
	return true;
}



void ChangeAlliances::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeAlliances");
	ManagementOrder::save(stream);
	stream->writeUint32(team, "team");

	if(is_allied)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_TRUE, "is_allied");
	else if(!is_allied)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_FALSE, "is_allied");
	else
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE, "is_allied");

	if(is_enemy)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_TRUE, "is_enemy");
	else if(!is_enemy)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_FALSE, "is_enemy");
	else
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE, "is_enemy");

	if(view_market)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_TRUE, "view_market");
	else if(!view_market)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_FALSE, "view_market");
	else
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE, "view_market");

	if(view_inn)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_TRUE, "view_inn");
	else if(!view_inn)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_FALSE, "view_inn");
	else
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE, "view_inn");

	if(view_other)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_TRUE, "view_other");
	else if(!view_other)
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_FALSE, "view_other");
	else
		stream->writeUint8(AI_SHARED_RUNTIME_TRIBOOL_INDETERMINATE, "view_other");

	stream->writeLeaveSection();
}

UpgradeRepair::UpgradeRepair(int id) : id(id)
{

}



void UpgradeRepair::modify(Runtime& runtime)
{
	auto* building=runtime.get_building_register().get_building(id);
	// Construction means repair for damaged buildings and upgrade for healthy
	// ones. Do not register an upgrade wait when authoritative rules reject it.
	if(building->hp<building->getEffectiveMaxHp()) { if(!building->type->semantics.repairable) return; }
 else if(runtime.player->game->gameHeader.isUnitUpgradesDisabled() || !building->isUpgradeAvailable()) return;
	runtime.push_order(AIRules::constructionOrder(*runtime.player->game, *building,1,1));
	runtime.get_building_register().set_upgrading(id);
}



tribool UpgradeRepair::wait(Runtime& runtime)
{
	return wait_for_building(runtime, id);
}



ManagementOrderType UpgradeRepair::get_type()
{
	return MUpgradeRepair;
}



bool UpgradeRepair::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("UpgradeRepair");
	ManagementOrder::load(stream, player, versionMinor);
	id=stream->readUint32("id");
	stream->readLeaveSection();
	return true;
}



void UpgradeRepair::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("UpgradeRepair");
	ManagementOrder::save(stream);
	stream->writeUint32(id, "id");
	stream->writeLeaveSection();
}


SendMessage::SendMessage(const std::string& message) : message(message)
{

}



void SendMessage::modify(Runtime& runtime)
{
	runtime.runtimeai->handle_message(runtime, message);
}



tribool SendMessage::wait(Runtime& runtime)
{
	return true;
}



ManagementOrderType SendMessage::get_type()
{
	return MSendMessage;
}



bool SendMessage::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("SendMessage");
	ManagementOrder::load(stream, player, versionMinor);
	message=stream->readText("message");
	stream->readLeaveSection();
	return true;
}



void SendMessage::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("SendMessage");
	ManagementOrder::save(stream);
	stream->writeText(message, "message");
	stream->writeLeaveSection();
}
