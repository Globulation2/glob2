// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file ParserSummon.cpp
	\brief SGSL parser: statements that place units, flags and areas or set alliances
*/

#include <iostream>
#include <string>

#include "SGSL.h"
#include "SGSLParseContext.h"

bool MapScriptSGSL::areaNameDefined(const Game *game, const std::string &name) const
{
	return mapAreaNumber(game, name) || areas.find(name) != areas.end();
}

// summonUnits(flag_name , globules_amount , globule_type , globule_level , team_int)
SGSLParseStatus MapScriptSGSL::parseSummonUnits(SGSLParseContext &ctx)
{
	//<-summon
	ctx.pushToken();

	if (!ctx.openArguments()) // <- flag_name
		return SGSLParseStatus::Aborted;
	if (!ctx.requireString())
		return SGSLParseStatus::Failed;
	if (!areaNameDefined(ctx.game, ctx.token().msg))
		return ctx.fail(ErrorReport::ET_UNDEFINED_AREA_NAME);
	ctx.pushToken();

	if (!ctx.nextArgument()) //<- globules_amount
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	if (ctx.token().value > 30) //Max number of globules to summon
		return ctx.fail(ErrorReport::ET_INVALID_VALUE);
	ctx.pushToken();

	if (!ctx.nextArgument()) //<- globules type
		return SGSLParseStatus::Aborted;
	if ((ctx.token().type != SGSLToken::S_WARRIOR) && (ctx.token().type != SGSLToken::S_WORKER) && (ctx.token().type != SGSLToken::S_EXPLORER))
		return ctx.fail(ErrorReport::ET_SYNTAX_ERROR);
	ctx.pushToken();

	if (!ctx.nextArgument()) //<- globules level
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	if (ctx.token().value > 3)
		return ctx.fail(ErrorReport::ET_INVALID_VALUE);
	ctx.pushToken();

	if (!ctx.nextArgument()) //<- team
		return SGSLParseStatus::Aborted;
	if (!ctx.requireTeam())
		return SGSLParseStatus::Failed;
	ctx.pushToken();

	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// setArea(string name, int x , int y , int r)
SGSLParseStatus MapScriptSGSL::parseSetArea(SGSLParseContext &ctx)
{
	Area area;

	std::cerr << "SGSL : Use of setArea is deprecated. Use newer script areas in the map editor instead." << std::endl;

	ctx.pushToken();

	if (!ctx.openArguments()) // <- string name
		return SGSLParseStatus::Aborted;
	if (!ctx.requireString())
		return SGSLParseStatus::Failed;
	if (areas.find(ctx.token().msg) != areas.end())
		return ctx.fail(ErrorReport::ET_DUPLICATED_AREA_NAME);
	std::string name=ctx.token().msg;
	ctx.pushToken();

	if (!ctx.nextArgument()) // <- int x
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	area.x=ctx.token().value;
	ctx.pushToken();

	if (!ctx.nextArgument()) //<- int y
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	area.y=ctx.token().value;
	ctx.pushToken();

	if (!ctx.nextArgument()) // <- int r
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	if (ctx.token().value == 0)
		return ctx.fail(ErrorReport::ET_INVALID_VALUE);
	area.r=ctx.token().value;
	ctx.pushToken();

	areas[name] = area;

	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// summonFlag(string name, int x, int y, int r, int unitcount, int team)
SGSLParseStatus MapScriptSGSL::parseSummonFlag(SGSLParseContext &ctx)
{
	ctx.pushToken();

	if (!ctx.openArguments()) // <- string name
		return SGSLParseStatus::Aborted;
	if (!ctx.requireString())
		return SGSLParseStatus::Failed;
	ctx.pushToken();

	// <- int x, int y, int r, int unitcount
	for (int i = 0; i < 4; i++)
	{
		if (!ctx.nextArgument())
			return SGSLParseStatus::Aborted;
		if (!ctx.requireInt())
			return SGSLParseStatus::Failed;
		ctx.pushToken();
	}

	if (!ctx.nextArgument()) // <- int team
		return SGSLParseStatus::Aborted;
	if (!ctx.requireTeam())
		return SGSLParseStatus::Failed;
	ctx.pushToken();

	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// destroyFlag(string name)
SGSLParseStatus MapScriptSGSL::parseDestroyFlag(SGSLParseContext &ctx)
{
	ctx.pushToken();

	if (!ctx.openArguments()) // <- string name
		return SGSLParseStatus::Aborted;
	if (!ctx.requireString())
		return SGSLParseStatus::Failed;
	ctx.pushToken();

	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// alliance(team1, team2, level)
SGSLParseStatus MapScriptSGSL::parseAlliance(SGSLParseContext &ctx)
{
	ctx.pushToken();

	// team 1
	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireTeam())
		return SGSLParseStatus::Failed;
	ctx.pushToken();

	// team 2
	if (!ctx.nextArgument())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireTeam())
		return SGSLParseStatus::Failed;
	ctx.pushToken();

	// level
	if (!ctx.nextArgument())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	if (ctx.token().value > 3)
		return ctx.fail(ErrorReport::ET_INVALID_ALLIANCE_LEVEL);
	ctx.pushToken();

	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}
