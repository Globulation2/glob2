// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2006 Bradley Arsenault

#include "echo/Echo.h"
#include "Game.h"
#include "Order.h"

using namespace AIEcho;
using namespace AIEcho::Management;
using namespace boost::logic;
using std::shared_ptr;


ChangeAlliances::ChangeAlliances(int team, boost::logic::tribool is_allied, boost::logic::tribool is_enemy, boost::logic::tribool view_market, boost::logic::tribool view_inn, boost::logic::tribool view_other) : team(team), is_allied(is_allied), is_enemy(is_enemy), view_market(view_market), view_inn(view_inn), view_other(view_other)
{

}



void ChangeAlliances::modify(Echo& echo)
{
	Uint32 alliedmask=echo.allies;
	Uint32 enemymask=echo.enemies;
	Uint32 market_mask=echo.market_view;
	Uint32 inn_mask=echo.inn_view;
	Uint32 other_mask=echo.other_view;
	Team* t=echo.player->game->teams[team];
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

	echo.allies=alliedmask;
	echo.enemies=enemymask;
	echo.market_view=market_mask;
	echo.inn_view=inn_mask;
	echo.other_view=other_mask;

	echo.push_order(shared_ptr<Order>(new SetAllianceOrder(echo.player->team->teamNumber, alliedmask, enemymask, market_mask, inn_mask, other_mask)));
}



boost::logic::tribool ChangeAlliances::wait(Echo& echo)
{
	return true;
}



bool ChangeAlliances::load(GAGCore::InputStream *stream, Player *player, Sint32 versionMinor)
{
	stream->readEnterSection("ChangeAlliances");
	ManagementOrder::load(stream, player, versionMinor);
	team=stream->readUint32("team");

	Uint8 tmp=stream->readUint8("is_allied");
	if(tmp==AI_ECHO_TRIBOOL_TRUE)
		is_allied=true;
	else if(tmp==AI_ECHO_TRIBOOL_FALSE)
		is_allied=false;
	else if(tmp==AI_ECHO_TRIBOOL_INDETERMINATE)
		is_allied=indeterminate;

	tmp=stream->readUint8("is_enemy");
	if(tmp==AI_ECHO_TRIBOOL_TRUE)
		is_enemy=true;
	else if(tmp==AI_ECHO_TRIBOOL_FALSE)
		is_enemy=false;
	else if(tmp==AI_ECHO_TRIBOOL_INDETERMINATE)
		is_enemy=indeterminate;

	tmp=stream->readUint8("view_market");
	if(tmp==AI_ECHO_TRIBOOL_TRUE)
		view_market=true;
	else if(tmp==AI_ECHO_TRIBOOL_FALSE)
		view_market=false;
	else if(tmp==AI_ECHO_TRIBOOL_INDETERMINATE)
		view_market=indeterminate;

	tmp=stream->readUint8("view_inn");
	if(tmp==AI_ECHO_TRIBOOL_TRUE)
		view_inn=true;
	else if(tmp==AI_ECHO_TRIBOOL_FALSE)
		view_inn=false;
	else if(tmp==AI_ECHO_TRIBOOL_INDETERMINATE)
		view_inn=indeterminate;

	tmp=stream->readUint8("view_other");
	if(tmp==AI_ECHO_TRIBOOL_TRUE)
		view_other=true;
	else if(tmp==AI_ECHO_TRIBOOL_FALSE)
		view_other=false;
	else if(tmp==AI_ECHO_TRIBOOL_INDETERMINATE)
		view_other=indeterminate;

	stream->readLeaveSection();
	return true;
}



void ChangeAlliances::save(GAGCore::OutputStream *stream)
{
	stream->writeEnterSection("ChangeAlliances");
	ManagementOrder::save(stream);
	stream->writeUint32(team, "team");

	if(is_allied)
		stream->writeUint8(AI_ECHO_TRIBOOL_TRUE, "is_allied");
	else if(!is_allied)
		stream->writeUint8(AI_ECHO_TRIBOOL_FALSE, "is_allied");
	else
		stream->writeUint8(AI_ECHO_TRIBOOL_INDETERMINATE, "is_allied");

	if(is_enemy)
		stream->writeUint8(AI_ECHO_TRIBOOL_TRUE, "is_enemy");
	else if(!is_enemy)
		stream->writeUint8(AI_ECHO_TRIBOOL_FALSE, "is_enemy");
	else
		stream->writeUint8(AI_ECHO_TRIBOOL_INDETERMINATE, "is_enemy");

	if(view_market)
		stream->writeUint8(AI_ECHO_TRIBOOL_TRUE, "view_market");
	else if(!view_market)
		stream->writeUint8(AI_ECHO_TRIBOOL_FALSE, "view_market");
	else
		stream->writeUint8(AI_ECHO_TRIBOOL_INDETERMINATE, "view_market");

	if(view_inn)
		stream->writeUint8(AI_ECHO_TRIBOOL_TRUE, "view_inn");
	else if(!view_inn)
		stream->writeUint8(AI_ECHO_TRIBOOL_FALSE, "view_inn");
	else
		stream->writeUint8(AI_ECHO_TRIBOOL_INDETERMINATE, "view_inn");

	if(view_other)
		stream->writeUint8(AI_ECHO_TRIBOOL_TRUE, "view_other");
	else if(!view_other)
		stream->writeUint8(AI_ECHO_TRIBOOL_FALSE, "view_other");
	else
		stream->writeUint8(AI_ECHO_TRIBOOL_INDETERMINATE, "view_other");

	stream->writeLeaveSection();
}

UpgradeRepair::UpgradeRepair(int id) : id(id)
{

}



void UpgradeRepair::modify(Echo& echo)
{
	echo.push_order(shared_ptr<Order>(new OrderConstruction(echo.get_building_register().get_building(id)->gid,1,1)));
	echo.get_building_register().set_upgrading(id);
}



boost::logic::tribool UpgradeRepair::wait(Echo& echo)
{
	return wait_for_building(echo, id);
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



void SendMessage::modify(Echo& echo)
{
	echo.echoai->handle_message(echo, message);
}



boost::logic::tribool SendMessage::wait(Echo& echo)
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

