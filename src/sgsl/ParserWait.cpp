// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat
// Copyright (C) 2001-2008 Luc-Olivier de Charrière
// Copyright (C) 2001-2008 Martin S. Nyffenegger

/*!	\file ParserWait.cpp
	\brief SGSL parser: the wait(...) statement and its condition forms
*/

#include "SGSL.h"
#include "SGSLParseContext.h"

// wait(int) | wait(isdead(team)) | wait([not(] area("name", who) [)]) | wait([not(] [only] condition [)])
SGSLParseStatus MapScriptSGSL::parseWait(SGSLParseContext &ctx)
{
	bool negate = false;
	ctx.pushToken();
	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;

	// int
	if (ctx.token().type == SGSLToken::INT)
	{
		if (ctx.token().value <=0)
			return ctx.fail(ErrorReport::ET_INVALID_VALUE);
		ctx.pushToken();
	}
	// isdead( teamNumber )
	else if (ctx.token().type == SGSLToken::S_ISDEAD)
	{
		ctx.pushToken();
		if (!ctx.openArguments())
			return SGSLParseStatus::Aborted;
		if (!ctx.requireTeam())
			return SGSLParseStatus::Failed;
		ctx.pushToken();
		if (!ctx.expectParClose())
			return SGSLParseStatus::Aborted;
	}
	else
	{
		// following two arguments can be negated !
		if (ctx.token().type == SGSLToken::S_NOT)
		{
			negate = true;
			ctx.pushToken();
			if (!ctx.openArguments())
				return SGSLParseStatus::Aborted;
		}
		// area ("areaname" , who*)
		if (ctx.token().type == SGSLToken::S_AREA)
		{
			const SGSLParseStatus status = parseWaitArea(ctx);
			if (status != SGSLParseStatus::Ok)
				return status;
		}
		else
		{
			// only
			bool only = false;
			if (ctx.token().type == SGSLToken::S_ONLY)
			{
				only = true;
				ctx.pushToken();
				ctx.nextToken();
			}

			if ((ctx.token().type < SGSLToken::S_WORKER) || (ctx.token().type > SGSLToken::S_MARKET_B))
				return ctx.fail(ErrorReport::ET_SYNTAX_ERROR);
			const SGSLParseStatus status = parseWaitComparison(ctx, only);
			if (status != SGSLParseStatus::Ok)
				return status;
		}
	}

	if (negate)
	{
		if (!ctx.expectParClose())
			return SGSLParseStatus::Aborted;
	}
	if (!ctx.closeArguments())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// area("areaname", team | enemy(team) | ally(team))
SGSLParseStatus MapScriptSGSL::parseWaitArea(SGSLParseContext &ctx)
{
	ctx.pushToken();

	if (!ctx.openArguments())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireString())
		return SGSLParseStatus::Failed;
	if (!areaNameDefined(ctx.game, ctx.token().msg))
		return ctx.fail(ErrorReport::ET_UNDEFINED_AREA_NAME);
	ctx.pushToken();

	if (!ctx.nextArgument())
		return SGSLParseStatus::Aborted;
	ctx.pushToken();
	if (ctx.token().type != SGSLToken::INT)
	{
		if ((ctx.token().type != SGSLToken::S_ENEMY)
			&& (ctx.token().type != SGSLToken::S_ALLY))
			return ctx.fail(ErrorReport::ET_SYNTAX_ERROR);

		// we have enemy or ally
		if (!ctx.expectParOpen())
			return SGSLParseStatus::Aborted;
		ctx.nextToken();
		if (!ctx.requireInt())
			return SGSLParseStatus::Failed;
		ctx.pushToken();
		if (!ctx.expectParClose())
			return SGSLParseStatus::Aborted;
	}
	if (!ctx.expectParClose())
		return SGSLParseStatus::Aborted;
	return SGSLParseStatus::Ok;
}

// Comparison: Unit(team) cond value, or Building(team, level) cond value
SGSLParseStatus MapScriptSGSL::parseWaitComparison(SGSLParseContext &ctx, bool only)
{
	ctx.pushToken();
	if (ctx.token().type >= SGSLToken::S_SWARM_B)
	{
		// Buildings
		if (!ctx.openArguments())
			return SGSLParseStatus::Aborted;
		// team
		if (!ctx.requireTeam())
			return SGSLParseStatus::Failed;
		ctx.pushToken();
		if (!ctx.nextArgument())
			return SGSLParseStatus::Aborted;

		// level
		if (!ctx.requireInt())
			return SGSLParseStatus::Failed;
		if ((ctx.token().value < 0) || (ctx.token().value > 5))
			return ctx.fail(ErrorReport::ET_INVALID_VALUE);
		ctx.pushToken();
	}
	else
	{
		// only is invalid for units
		if (only)
			return ctx.fail(ErrorReport::ET_INVALID_ONLY);

		// Units
		if (!ctx.openArguments())
			return SGSLParseStatus::Aborted;
		// team
		if (!ctx.requireTeam())
			return SGSLParseStatus::Failed;
		ctx.pushToken();
	}
	if (!ctx.expectParClose())
		return SGSLParseStatus::Aborted;

	ctx.nextToken();
	if (!ctx.hasArgument())
		return SGSLParseStatus::Aborted;
	if ((ctx.token().type < SGSLToken::S_EQUAL) || (ctx.token().type > SGSLToken::S_LOWER))
		return ctx.fail(ErrorReport::ET_SYNTAX_ERROR);
	ctx.pushToken();
	ctx.nextToken();
	if (!ctx.hasArgument())
		return SGSLParseStatus::Aborted;
	if (!ctx.requireInt())
		return SGSLParseStatus::Failed;
	ctx.pushToken();
	return SGSLParseStatus::Ok;
}
